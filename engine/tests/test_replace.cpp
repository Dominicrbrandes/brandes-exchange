#include <catch2/catch_all.hpp>

#include "exchange/matching_engine.hpp"
#include "test_helpers.hpp"

using namespace exchange;
using namespace exchange::test;

namespace {

std::vector<uint64_t> bid_queue(MatchingEngine& engine) {
    std::vector<uint64_t> ids;
    for (const auto* o : engine.get_book("BTC-USD")->get_bids_at_best()) ids.push_back(o->id);
    return ids;
}

}  // namespace

TEST_CASE("Replace - smaller size at the same price keeps queue position", "[replace]") {
    MatchingEngine engine;
    auto a = engine.place_order(limit("a", Side::BUY, 100, 10)).order.id;
    auto b = engine.place_order(limit("b", Side::BUY, 100, 10)).order.id;

    auto r = engine.replace_order(a, px(100), 4);

    REQUIRE(r.success);
    REQUIRE(r.trades.empty());
    REQUIRE(r.order.quantity == 4);
    REQUIRE(r.order.remaining_qty == 4);
    REQUIRE(r.order.status == OrderStatus::NEW);
    REQUIRE(bid_queue(engine) == std::vector<uint64_t>{a, b});
    REQUIRE(engine.get_book("BTC-USD")->get_bid_levels()[0].quantity == 14);
}

TEST_CASE("Replace - larger size loses queue position", "[replace]") {
    MatchingEngine engine;
    auto a = engine.place_order(limit("a", Side::BUY, 100, 10)).order.id;
    auto b = engine.place_order(limit("b", Side::BUY, 100, 10)).order.id;

    auto r = engine.replace_order(a, px(100), 15);

    REQUIRE(r.success);
    REQUIRE(bid_queue(engine) == std::vector<uint64_t>{b, a});
    REQUIRE(engine.get_book("BTC-USD")->get_bid_levels()[0].quantity == 25);
}

TEST_CASE("Replace - new price moves the order to the back of the new level", "[replace]") {
    MatchingEngine engine;
    auto a = engine.place_order(limit("a", Side::BUY, 100, 10)).order.id;
    auto b = engine.place_order(limit("b", Side::BUY, 101, 10)).order.id;

    auto r = engine.replace_order(a, px(101), 10);

    REQUIRE(r.success);
    REQUIRE(r.order.price == px(101));
    REQUIRE(bid_queue(engine) == std::vector<uint64_t>{b, a});
    REQUIRE(engine.get_book("BTC-USD")->get_bid_levels().size() == 1);
}

TEST_CASE("Replace - crossing the spread trades like a new order", "[replace]") {
    MatchingEngine engine;
    engine.place_order(limit("seller", Side::SELL, 101, 6));
    auto buy = engine.place_order(limit("buyer", Side::BUY, 100, 10)).order.id;

    auto r = engine.replace_order(buy, px(101), 10);

    REQUIRE(r.success);
    REQUIRE(r.trades.size() == 1);
    REQUIRE(r.trades[0].quantity == 6);
    REQUIRE(r.trades[0].price == px(101));
    REQUIRE(r.order.status == OrderStatus::PARTIAL);
    REQUIRE(r.order.remaining_qty == 4);

    auto* book = engine.get_book("BTC-USD");
    REQUIRE(book->best_ask_price() == std::nullopt);
    REQUIRE(book->best_bid_price() == px(101));
}

TEST_CASE("Replace - crossing replace that fills completely leaves nothing resting",
          "[replace]") {
    MatchingEngine engine;
    engine.place_order(limit("seller", Side::SELL, 101, 20));
    auto buy = engine.place_order(limit("buyer", Side::BUY, 100, 10)).order.id;

    auto r = engine.replace_order(buy, px(101), 10);

    REQUIRE(r.order.status == OrderStatus::FILLED);
    REQUIRE(engine.get_book("BTC-USD")->bid_count() == 0);
    REQUIRE(engine.get_book("BTC-USD")->get_ask_levels()[0].quantity == 10);
}

TEST_CASE("Replace - quantity is the new total including what already filled", "[replace]") {
    MatchingEngine engine;
    auto sell = engine.place_order(limit("seller", Side::SELL, 100, 10)).order.id;
    engine.place_order(limit("buyer", Side::BUY, 100, 4));  // fills 4 of the sell

    auto r = engine.replace_order(sell, px(100), 8);

    REQUIRE(r.success);
    REQUIRE(r.order.quantity == 8);
    REQUIRE(r.order.filled_qty() == 4);
    REQUIRE(r.order.remaining_qty == 4);
    REQUIRE(r.order.status == OrderStatus::PARTIAL);

    // Shrinking to what already filled (or below) is refused.
    auto bad = engine.replace_order(sell, px(100), 4);
    REQUIRE_FALSE(bad.success);
    REQUIRE(bad.error_code == ErrorCode::INVALID_QUANTITY);
    REQUIRE(engine.get_order(sell)->remaining_qty == 4);
}

TEST_CASE("Replace - unknown, filled and cancelled orders are refused", "[replace]") {
    MatchingEngine engine;
    REQUIRE(engine.replace_order(999, px(100), 1).error_code == ErrorCode::ORDER_NOT_FOUND);

    auto sell = engine.place_order(limit("s", Side::SELL, 100, 5)).order.id;
    engine.place_order(limit("b", Side::BUY, 100, 5));
    auto filled = engine.replace_order(sell, px(100), 10);
    REQUIRE_FALSE(filled.success);
    REQUIRE(filled.error_code == ErrorCode::ORDER_NOT_FOUND);
    REQUIRE(filled.order.status == OrderStatus::FILLED);

    auto resting = engine.place_order(limit("s", Side::SELL, 100, 5)).order.id;
    engine.cancel_order(resting);
    REQUIRE(engine.replace_order(resting, px(100), 5).error_code == ErrorCode::ORDER_NOT_FOUND);
}

TEST_CASE("Replace - risk checks apply to the new values", "[replace][risk]") {
    MatchingEngine engine;
    auto id = engine.place_order(limit("a", Side::BUY, 100, 10)).order.id;

    auto bad_price = engine.replace_order(id, 0, 10);
    REQUIRE_FALSE(bad_price.success);
    REQUIRE(bad_price.error_code == ErrorCode::INVALID_PRICE);

    auto too_big = engine.replace_order(id, px(100), 1001 * PRICE_SCALE);
    REQUIRE_FALSE(too_big.success);
    REQUIRE(too_big.error_code == ErrorCode::MAX_ORDER_SIZE_EXCEEDED);

    // A refused replace leaves the original order exactly as it was.
    auto o = engine.get_order(id);
    REQUIRE(o->price == px(100));
    REQUIRE(o->quantity == 10);
    REQUIRE(engine.get_book("BTC-USD")->get_bid_levels()[0].quantity == 10);
}

TEST_CASE("Replace - self-trade prevention still applies when the new price crosses",
          "[replace][stp]") {
    MatchingEngine engine;
    engine.place_order(limit("a", Side::SELL, 101, 5));
    auto buy = engine.place_order(limit("a", Side::BUY, 100, 5)).order.id;

    auto r = engine.replace_order(buy, px(101), 5);

    REQUIRE(r.success);
    REQUIRE(r.trades.empty());
    REQUIRE(r.order.status == OrderStatus::NEW);
}

TEST_CASE("Replace - stats count replaces", "[replace]") {
    MatchingEngine engine;
    auto id = engine.place_order(limit("a", Side::BUY, 100, 10)).order.id;
    engine.replace_order(id, px(100), 5);
    engine.replace_order(id, px(99), 5);
    REQUIRE(engine.get_stats().total_replaces == 2);
}
