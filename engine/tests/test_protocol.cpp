#include <catch2/catch_all.hpp>

#include "exchange/matching_engine.hpp"
#include "exchange/protocol.hpp"

using namespace exchange;
using nlohmann::json;

namespace {

json send(ProtocolHandler& h, const json& cmd) { return json::parse(h.handle(cmd.dump())); }

json order_cmd(const std::string& account, const std::string& side, int64_t price,
               int64_t qty, const std::string& tif = "GTC") {
    return {{"cmd", "place_order"},
            {"order",
             {{"account_id", account},
              {"symbol", "BTC-USD"},
              {"side", side},
              {"type", "LIMIT"},
              {"time_in_force", tif},
              {"price", price * PRICE_SCALE},
              {"quantity", qty}}}};
}

}  // namespace

TEST_CASE("Protocol - place, replace and cancel through JSON commands", "[protocol]") {
    MatchingEngine engine;
    ProtocolHandler h(engine);

    auto placed = send(h, order_cmd("a", "SELL", 100, 10));
    REQUIRE(placed["success"] == true);
    const uint64_t id = placed["data"]["order"]["id"];

    auto replaced = send(h, {{"cmd", "replace_order"},
                             {"order_id", id},
                             {"price", 101 * PRICE_SCALE},
                             {"quantity", 8}});
    REQUIRE(replaced["success"] == true);
    REQUIRE(replaced["data"]["order"]["price"] == 101 * PRICE_SCALE);
    REQUIRE(replaced["data"]["trades"].empty());

    auto bad = send(h, {{"cmd", "replace_order"},
                        {"order_id", 999},
                        {"price", PRICE_SCALE},
                        {"quantity", 1}});
    REQUIRE(bad["success"] == false);
    REQUIRE(bad["error"]["code"] == "ORDER_NOT_FOUND");

    auto ioc = send(h, order_cmd("b", "BUY", 101, 20, "IOC"));
    REQUIRE(ioc["data"]["order"]["status"] == "CANCELLED");
    REQUIRE(ioc["data"]["order"]["time_in_force"] == "IOC");
    REQUIRE(ioc["data"]["trades"].size() == 1);

    auto again = send(h, {{"cmd", "cancel_order"}, {"order_id", id}});
    REQUIRE(again["success"] == false);  // filled by the IOC
    REQUIRE(send(h, {{"cmd", "get_order"}, {"order_id", id}})["data"]["order"]["status"] ==
            "FILLED");
}

TEST_CASE("Protocol - book, trades, stats and housekeeping commands", "[protocol]") {
    MatchingEngine engine;
    ProtocolHandler h(engine);
    send(h, order_cmd("a", "SELL", 100, 5));
    auto resting = send(h, order_cmd("b", "BUY", 99, 7));
    send(h, order_cmd("c", "BUY", 100, 2));

    auto book = send(h, {{"cmd", "get_book"}, {"symbol", "BTC-USD"}, {"depth", 5}});
    REQUIRE(book["data"]["asks"][0]["quantity"] == 3);
    REQUIRE(book["data"]["bids"][0]["quantity"] == 7);

    auto empty = send(h, {{"cmd", "get_book"}, {"symbol", "ETH-USD"}});
    REQUIRE(empty["data"]["bids"].empty());

    auto trades = send(h, {{"cmd", "get_trades"}, {"symbol", "BTC-USD"}, {"limit", 10}});
    REQUIRE(trades["data"]["trades"].size() == 1);

    const uint64_t resting_id = resting["data"]["order"]["id"];
    auto cancel = send(h, {{"cmd", "cancel_order"}, {"order_id", resting_id}});
    REQUIRE(cancel["success"] == true);

    auto stats = send(h, {{"cmd", "get_stats"}});
    REQUIRE(stats["data"]["total_orders"] == 3);
    REQUIRE(stats["data"]["total_cancels"] == 1);

    REQUIRE(send(h, {{"cmd", "health"}})["success"] == true);
    REQUIRE(send(h, {{"cmd", "shutdown"}})["data"]["status"] == "shutting_down");
    REQUIRE(send(h, {{"cmd", "get_order"}, {"order_id", 999}})["success"] == false);
    REQUIRE(send(h, {{"cmd", "nope"}})["error"]["code"] == "UNKNOWN_COMMAND");

    auto rejected = send(h, order_cmd("a", "BUY", 0, 1));
    REQUIRE(rejected["error"]["code"] == "INVALID_PRICE");

    REQUIRE(json::parse(h.handle("{not json"))["error"]["code"] == "PARSE_ERROR");
    REQUIRE(send(h, {{"cmd", "cancel_order"}})["error"]["code"] == "PARSE_ERROR");
}
