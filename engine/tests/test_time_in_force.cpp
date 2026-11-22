#include <catch2/catch_all.hpp>

#include "exchange/matching_engine.hpp"
#include "test_helpers.hpp"

using namespace exchange;
using namespace exchange::test;

TEST_CASE("IOC - fills what is available and cancels the rest", "[tif]") {
    MatchingEngine engine;
    engine.place_order(limit("maker", Side::SELL, 100, 30));

    auto r = engine.place_order(limit("taker", Side::BUY, 100, 50, TimeInForce::IOC));

    REQUIRE(r.success);
    REQUIRE(r.trades.size() == 1);
    REQUIRE(r.trades[0].quantity == 30);
    REQUIRE(r.order.status == OrderStatus::CANCELLED);
    REQUIRE(r.order.filled_qty() == 30);
    REQUIRE(r.order.remaining_qty == 20);

    // The remainder never rests.
    auto* book = engine.get_book("BTC-USD");
    REQUIRE(book->get_order(r.order.id) == nullptr);
    REQUIRE(book->bid_count() == 0);
    REQUIRE(engine.get_stats().total_expired == 1);
}

TEST_CASE("IOC - nothing to trade against is cancelled without trades", "[tif]") {
    MatchingEngine engine;
    engine.place_order(limit("maker", Side::SELL, 105, 10));

    auto r = engine.place_order(limit("taker", Side::BUY, 100, 10, TimeInForce::IOC));

    REQUIRE(r.success);
    REQUIRE(r.trades.empty());
    REQUIRE(r.order.status == OrderStatus::CANCELLED);
    REQUIRE(engine.get_book("BTC-USD")->bid_count() == 0);
}

TEST_CASE("IOC - full fill reports FILLED, not CANCELLED", "[tif]") {
    MatchingEngine engine;
    engine.place_order(limit("maker", Side::SELL, 100, 10));

    auto r = engine.place_order(limit("taker", Side::BUY, 100, 10, TimeInForce::IOC));

    REQUIRE(r.order.status == OrderStatus::FILLED);
    REQUIRE(engine.get_stats().total_expired == 0);
}

TEST_CASE("IOC - an expired order cannot be cancelled again", "[tif]") {
    MatchingEngine engine;
    auto r = engine.place_order(limit("taker", Side::BUY, 100, 10, TimeInForce::IOC));

    auto c = engine.cancel_order(r.order.id);
    REQUIRE_FALSE(c.success);
    REQUIRE(c.error_code == ErrorCode::ORDER_NOT_FOUND);
}

TEST_CASE("FOK - fills completely across several levels", "[tif]") {
    MatchingEngine engine;
    engine.place_order(limit("m1", Side::SELL, 100, 10));
    engine.place_order(limit("m2", Side::SELL, 101, 10));
    engine.place_order(limit("m3", Side::SELL, 102, 10));

    auto r = engine.place_order(limit("taker", Side::BUY, 102, 25, TimeInForce::FOK));

    REQUIRE(r.success);
    REQUIRE(r.order.status == OrderStatus::FILLED);
    REQUIRE(r.trades.size() == 3);
    REQUIRE(r.trades[2].quantity == 5);
}

TEST_CASE("FOK - kills without trading when the full size is not there", "[tif]") {
    MatchingEngine engine;
    engine.place_order(limit("m1", Side::SELL, 100, 10));
    engine.place_order(limit("m2", Side::SELL, 101, 10));

    auto r = engine.place_order(limit("taker", Side::BUY, 101, 25, TimeInForce::FOK));

    REQUIRE(r.success);
    REQUIRE(r.trades.empty());
    REQUIRE(r.order.status == OrderStatus::CANCELLED);
    REQUIRE(r.order.remaining_qty == 25);

    // The book is untouched.
    auto asks = engine.get_book("BTC-USD")->get_ask_levels();
    REQUIRE(asks.size() == 2);
    REQUIRE(asks[0].quantity == 10);
    REQUIRE(asks[1].quantity == 10);
}

TEST_CASE("FOK - liquidity beyond the limit price does not count", "[tif]") {
    MatchingEngine engine;
    engine.place_order(limit("m1", Side::SELL, 100, 10));
    engine.place_order(limit("m2", Side::SELL, 105, 100));

    auto r = engine.place_order(limit("taker", Side::BUY, 104, 20, TimeInForce::FOK));

    REQUIRE(r.order.status == OrderStatus::CANCELLED);
    REQUIRE(r.trades.empty());
}

TEST_CASE("FOK - the account's own resting orders do not count", "[tif][stp]") {
    MatchingEngine engine;
    engine.place_order(limit("taker", Side::SELL, 100, 50));  // own order, stepped over
    engine.place_order(limit("other", Side::SELL, 100, 10));

    auto killed = engine.place_order(limit("taker", Side::BUY, 100, 20, TimeInForce::FOK));
    REQUIRE(killed.order.status == OrderStatus::CANCELLED);
    REQUIRE(killed.trades.empty());

    auto filled = engine.place_order(limit("taker", Side::BUY, 100, 10, TimeInForce::FOK));
    REQUIRE(filled.order.status == OrderStatus::FILLED);
    REQUIRE(filled.trades.size() == 1);
    REQUIRE(filled.trades[0].seller_account_id == "other");
}

TEST_CASE("FOK - sell side checks bids from the top down", "[tif]") {
    MatchingEngine engine;
    engine.place_order(limit("m1", Side::BUY, 101, 10));
    engine.place_order(limit("m2", Side::BUY, 100, 10));

    auto killed = engine.place_order(limit("taker", Side::SELL, 101, 15, TimeInForce::FOK));
    REQUIRE(killed.order.status == OrderStatus::CANCELLED);

    auto filled = engine.place_order(limit("taker", Side::SELL, 100, 15, TimeInForce::FOK));
    REQUIRE(filled.order.status == OrderStatus::FILLED);
    REQUIRE(filled.trades.size() == 2);
    REQUIRE(filled.trades[0].price == px(101));
    REQUIRE(filled.trades[1].price == px(100));
}

TEST_CASE("FOK - an empty book kills the order", "[tif]") {
    MatchingEngine engine;
    auto r = engine.place_order(limit("taker", Side::BUY, 100, 5, TimeInForce::FOK));
    REQUIRE(r.success);
    REQUIRE(r.order.status == OrderStatus::CANCELLED);
}

TEST_CASE("Market - partial fill cancels the remainder instead of leaving it live",
          "[tif][market]") {
    MatchingEngine engine;
    engine.place_order(limit("maker", Side::SELL, 100, 30));

    auto r = engine.place_order(market("taker", Side::BUY, 50));

    REQUIRE(r.success);
    REQUIRE(r.order.status == OrderStatus::CANCELLED);
    REQUIRE(r.order.filled_qty() == 30);
    REQUIRE(r.order.time_in_force == TimeInForce::IOC);

    // It used to stay PARTIAL here: never on the book, yet cancellable and
    // carried into snapshots as a live order.
    REQUIRE_FALSE(engine.cancel_order(r.order.id).success);
    REQUIRE(engine.create_snapshot().orders.empty());
}

TEST_CASE("Market - FOK market order fills fully or not at all", "[tif][market]") {
    MatchingEngine engine;
    engine.place_order(limit("maker", Side::SELL, 100, 30));

    auto killed = engine.place_order(market("taker", Side::BUY, 50, TimeInForce::FOK));
    REQUIRE(killed.order.status == OrderStatus::CANCELLED);
    REQUIRE(killed.trades.empty());

    auto filled = engine.place_order(market("taker", Side::BUY, 30, TimeInForce::FOK));
    REQUIRE(filled.order.status == OrderStatus::FILLED);
}

TEST_CASE("Market - no liquidity is still a rejection", "[tif][market]") {
    MatchingEngine engine;
    auto r = engine.place_order(market("taker", Side::SELL, 10));
    REQUIRE_FALSE(r.success);
    REQUIRE(r.error_code == ErrorCode::NO_LIQUIDITY);
    REQUIRE(r.order.status == OrderStatus::REJECTED);
}

TEST_CASE("Risk - rejected orders get no id and leave no state", "[risk]") {
    MatchingEngine engine;
    auto r = engine.place_order(limit("a", Side::BUY, 0, 10));
    REQUIRE_FALSE(r.success);
    REQUIRE(r.error_code == ErrorCode::INVALID_PRICE);
    REQUIRE(engine.get_stats().total_rejects == 1);
    REQUIRE(engine.get_stats().total_orders == 0);
}
