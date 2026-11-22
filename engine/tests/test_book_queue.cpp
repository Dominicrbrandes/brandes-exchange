#include <catch2/catch_all.hpp>

#include "exchange/order_book.hpp"

using namespace exchange;

namespace {

Order resting(uint64_t id, Side side, int64_t price, int64_t qty) {
    Order o;
    o.id = id;
    o.side = side;
    o.price = price;
    o.quantity = qty;
    o.remaining_qty = qty;
    return o;
}

std::vector<uint64_t> ids_of(const std::vector<Order*>& orders) {
    std::vector<uint64_t> out;
    for (const auto* o : orders) out.push_back(o->id);
    return out;
}

}  // namespace

TEST_CASE("OrderBook - Remove from the middle, front and back keeps FIFO order",
          "[orderbook][queue]") {
    OrderBook book("BTC-USD");
    std::vector<Order> orders;
    for (uint64_t id = 1; id <= 5; ++id) orders.push_back(resting(id, Side::SELL, 100, 1));
    for (auto& o : orders) book.add_order(&o);

    REQUIRE(book.remove_order(3));
    REQUIRE(ids_of(book.get_asks_at_best()) == std::vector<uint64_t>{1, 2, 4, 5});
    REQUIRE(book.remove_order(1));
    REQUIRE(ids_of(book.get_asks_at_best()) == std::vector<uint64_t>{2, 4, 5});
    REQUIRE(book.remove_order(5));
    REQUIRE(ids_of(book.get_asks_at_best()) == std::vector<uint64_t>{2, 4});
    REQUIRE_FALSE(book.remove_order(5));

    auto levels = book.get_ask_levels();
    REQUIRE(levels.size() == 1);
    REQUIRE(levels[0].quantity == 2);
    REQUIRE(levels[0].order_count == 2);
}

TEST_CASE("OrderBook - Removing the last order drops the level", "[orderbook][queue]") {
    OrderBook book("BTC-USD");
    Order a = resting(1, Side::BUY, 100, 5);
    Order b = resting(2, Side::BUY, 99, 5);
    book.add_order(&a);
    book.add_order(&b);

    book.remove_order(1);
    REQUIRE(book.best_bid_price() == 99);
    REQUIRE(book.get_bid_levels().size() == 1);
    REQUIRE(book.level_front(Side::BUY, 100) == nullptr);
    REQUIRE(book.level_front(Side::BUY, 99)->order == &b);
}

TEST_CASE("OrderBook - Fills update level totals and remove filled orders", "[orderbook][queue]") {
    OrderBook book("BTC-USD");
    Order a = resting(1, Side::SELL, 100, 10);
    Order b = resting(2, Side::SELL, 100, 10);
    book.add_order(&a);
    book.add_order(&b);

    book.update_order_qty(1, 4);
    REQUIRE(a.status == OrderStatus::PARTIAL);
    REQUIRE(book.get_ask_levels()[0].quantity == 14);

    book.update_order_qty(1, 0);
    REQUIRE(a.status == OrderStatus::FILLED);
    REQUIRE(a.remaining_qty == 0);
    REQUIRE(book.get_order(1) == nullptr);
    REQUIRE(book.get_ask_levels()[0].quantity == 10);
    REQUIRE(book.get_ask_levels()[0].order_count == 1);

    // Unknown ids are ignored.
    book.update_order_qty(42, 1);
    book.resize_order(42, 1);
    REQUIRE(book.ask_count() == 1);
}

TEST_CASE("OrderBook - Resize keeps position and status", "[orderbook][queue]") {
    OrderBook book("BTC-USD");
    Order a = resting(1, Side::BUY, 100, 10);
    Order b = resting(2, Side::BUY, 100, 10);
    book.add_order(&a);
    book.add_order(&b);

    book.resize_order(1, 3);
    REQUIRE(a.remaining_qty == 3);
    REQUIRE(a.status == OrderStatus::NEW);
    REQUIRE(ids_of(book.get_bids_at_best()) == std::vector<uint64_t>{1, 2});
    REQUIRE(book.get_bid_levels()[0].quantity == 13);
}

TEST_CASE("OrderBook - Walking levels in priority order", "[orderbook][queue]") {
    OrderBook book("BTC-USD");
    Order b1 = resting(1, Side::BUY, 100, 1);
    Order b2 = resting(2, Side::BUY, 98, 1);
    Order a1 = resting(3, Side::SELL, 101, 1);
    Order a2 = resting(4, Side::SELL, 103, 1);
    for (Order* o : {&b1, &b2, &a1, &a2}) book.add_order(o);

    REQUIRE(book.next_price_after(Side::BUY, 100) == 98);
    REQUIRE(book.next_price_after(Side::BUY, 98) == std::nullopt);
    REQUIRE(book.next_price_after(Side::SELL, 101) == 103);
    REQUIRE(book.next_price_after(Side::SELL, 103) == std::nullopt);
    // Works from a price that is not (or no longer) a level.
    REQUIRE(book.next_price_after(Side::SELL, 102) == 103);
    REQUIRE(book.level_front(Side::SELL, 102) == nullptr);
    REQUIRE(book.level_front(Side::SELL, 103)->order == &a2);
    REQUIRE(book.get_asks_at_best().front() == &a1);
    REQUIRE(book.get_ask_levels(1).size() == 1);
    REQUIRE(book.get_bid_levels(1).size() == 1);
}

TEST_CASE("OrderBook - Empty book accessors", "[orderbook][queue]") {
    OrderBook book("BTC-USD");
    REQUIRE(book.get_bids_at_best().empty());
    REQUIRE(book.get_asks_at_best().empty());
    REQUIRE(book.get_all_asks().empty());
    REQUIRE(book.symbol() == "BTC-USD");
}

TEST_CASE("OrderBook - Nodes are reused after removal", "[orderbook][queue]") {
    OrderBook book("BTC-USD");
    std::vector<Order> orders;
    orders.reserve(1000);
    for (uint64_t id = 1; id <= 1000; ++id) {
        orders.push_back(resting(id, Side::BUY, 100 + static_cast<int64_t>(id % 7), 1));
    }

    // Churn: many more adds and removes than live orders at any time.
    for (int round = 0; round < 50; ++round) {
        for (auto& o : orders) book.add_order(&o);
        for (auto& o : orders) REQUIRE(book.remove_order(o.id));
    }
    REQUIRE(book.bid_count() == 0);
    REQUIRE(book.best_bid_price() == std::nullopt);
}
