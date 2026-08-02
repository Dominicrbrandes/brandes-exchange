#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include "exchange/types.hpp"

using namespace exchange;

// The logging hot path renders orders and trades by hand instead of building an
// nlohmann DOM. If the two ever disagree, the event log silently stops matching
// what recovery expects, so pin them together.
//
// Compared as parsed JSON rather than raw bytes: nlohmann orders object keys
// alphabetically while the hand-rolled writer emits declaration order. Both are
// the same object, and every reader here goes through a real JSON parser.

TEST_CASE("Event format - hand-rolled order JSON matches DOM output", "[format]") {
    Order o;
    o.id = 4242;
    o.account_id = "acct7";
    o.symbol = "BTC-USD";
    o.side = Side::SELL;
    o.type = OrderType::LIMIT;
    o.price = 50000 * PRICE_SCALE;
    o.quantity = 100;
    o.remaining_qty = 40;
    o.timestamp_ns = 1700000000123456789ULL;
    o.status = OrderStatus::PARTIAL;

    SECTION("without optional fields") {
        std::string fast;
        append_json(fast, o);
        REQUIRE(nlohmann::json::parse(fast) == nlohmann::json(o));
    }

    SECTION("with optional fields") {
        o.idempotency_key = "idem-1";
        o.client_order_id = "cli-9";
        std::string fast;
        append_json(fast, o);
        REQUIRE(nlohmann::json::parse(fast) == nlohmann::json(o));
    }

    SECTION("characters that need escaping") {
        o.account_id = "quote\"back\\slash";
        o.client_order_id = "tab\tnewline\n";
        std::string fast;
        append_json(fast, o);
        REQUIRE(nlohmann::json::parse(fast) == nlohmann::json(o));
    }

    SECTION("round-trips back through the recovery parser") {
        std::string fast;
        append_json(fast, o);
        Order back = order_from_persisted(nlohmann::json::parse(fast));
        REQUIRE(back.id == o.id);
        REQUIRE(back.remaining_qty == o.remaining_qty);
        REQUIRE(back.status == o.status);
        REQUIRE(back.price == o.price);
        REQUIRE(back.account_id == o.account_id);
    }
}

TEST_CASE("Event format - hand-rolled trade JSON matches DOM output", "[format]") {
    Trade t;
    t.id = 9;
    t.buy_order_id = 3;
    t.sell_order_id = 4;
    t.symbol = "BTC-USD";
    t.price = 49999 * PRICE_SCALE;
    t.quantity = 25;
    t.timestamp_ns = 1700000000987654321ULL;
    t.buyer_account_id = "alice";
    t.seller_account_id = "bob";

    std::string fast;
    append_json(fast, t);
    REQUIRE(nlohmann::json::parse(fast) == nlohmann::json(t));

    Trade back = nlohmann::json::parse(fast).get<Trade>();
    REQUIRE(back.id == t.id);
    REQUIRE(back.price == t.price);
    REQUIRE(back.quantity == t.quantity);
    REQUIRE(back.buyer_account_id == t.buyer_account_id);
}
