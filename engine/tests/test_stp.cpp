#include <catch2/catch_all.hpp>

#include "exchange/matching_engine.hpp"

using namespace exchange;

namespace {

Order make_limit(const std::string& account, Side side, int64_t price, int64_t qty) {
    Order o;
    o.account_id = account;
    o.symbol = "BTC-USD";
    o.side = side;
    o.type = OrderType::LIMIT;
    o.price = price * PRICE_SCALE;
    o.quantity = qty;
    return o;
}

}  // namespace

// Regression: an order that filled against other accounts and then reached its
// own resting order used to come back REJECTED / success=false, even though the
// fills had already been executed, logged and applied to the counterparties.
TEST_CASE("STP - order that already filled is never rejected", "[stp]") {
    MatchingEngine engine;

    // Two counterparties rest at the best price, then our own order sits behind
    // them at a worse price that the incoming order would still reach.
    REQUIRE(engine.place_order(make_limit("alice", Side::SELL, 100, 30)).success);
    REQUIRE(engine.place_order(make_limit("bob", Side::SELL, 100, 30)).success);
    REQUIRE(engine.place_order(make_limit("me", Side::SELL, 101, 50)).success);

    // Buy 100 at 101: takes 30 from alice, 30 from bob, steps over our own ask
    // at 101, and rests with the remainder.
    auto r = engine.place_order(make_limit("me", Side::BUY, 101, 100));

    REQUIRE(r.success);
    REQUIRE(r.error_code == ErrorCode::NONE);
    REQUIRE(r.trades.size() == 2);
    REQUIRE(r.order.status != OrderStatus::REJECTED);

    // 60 filled against others, 40 left resting.
    REQUIRE(r.trades[0].quantity == 30);
    REQUIRE(r.trades[1].quantity == 30);
    REQUIRE(r.order.remaining_qty == 40);
    REQUIRE(r.order.status == OrderStatus::PARTIAL);

    // We never traded with ourselves.
    for (const auto& t : r.trades) {
        REQUIRE(t.buyer_account_id != t.seller_account_id);
    }
}

// The whole point of skipping rather than breaking: liquidity sitting behind
// our own resting order must stay reachable.
TEST_CASE("STP - own order does not shadow liquidity behind it", "[stp]") {
    MatchingEngine engine;

    // Our own ask is at the front of the book; a counterparty is one tick back.
    REQUIRE(engine.place_order(make_limit("me", Side::SELL, 100, 25)).success);
    REQUIRE(engine.place_order(make_limit("carol", Side::SELL, 101, 25)).success);

    auto r = engine.place_order(make_limit("me", Side::BUY, 101, 25));

    REQUIRE(r.success);
    REQUIRE(r.trades.size() == 1);
    REQUIRE(r.trades[0].seller_account_id == "carol");
    REQUIRE(r.trades[0].price == 101 * PRICE_SCALE);
    REQUIRE(r.order.status == OrderStatus::FILLED);
}

// A pure self-match must not generate a trade, and must not be rejected either:
// the order simply rests.
TEST_CASE("STP - pure self match rests instead of trading", "[stp]") {
    MatchingEngine engine;

    REQUIRE(engine.place_order(make_limit("me", Side::SELL, 100, 10)).success);
    auto r = engine.place_order(make_limit("me", Side::BUY, 100, 10));

    REQUIRE(r.success);
    REQUIRE(r.trades.empty());
    REQUIRE(r.order.remaining_qty == 10);
    REQUIRE(engine.get_stats().total_trades == 0);
}

// Documents a real consequence of skip-and-continue: the book can end up
// crossed, but only ever between orders belonging to the same account. A cross
// between two different accounts would be a genuine matching failure.
TEST_CASE("STP - book never crosses between different accounts", "[stp]") {
    MatchingEngine engine;

    REQUIRE(engine.place_order(make_limit("me", Side::SELL, 100, 10)).success);
    REQUIRE(engine.place_order(make_limit("me", Side::BUY, 100, 10)).success);

    auto* book = engine.get_book("BTC-USD");
    REQUIRE(book != nullptr);

    auto bid = book->best_bid_price();
    auto ask = book->best_ask_price();
    REQUIRE(bid.has_value());
    REQUIRE(ask.has_value());

    // Self-crossed, which is expected and allowed.
    REQUIRE(*bid >= *ask);

    // But every crossing pair belongs to the same account.
    for (const auto* b : book->get_all_bids()) {
        for (const auto* a : book->get_all_asks()) {
            if (b->price >= a->price) {
                REQUIRE(b->account_id == a->account_id);
            }
        }
    }
}
