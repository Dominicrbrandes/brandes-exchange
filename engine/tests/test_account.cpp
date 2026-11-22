#include <catch2/catch_all.hpp>
#include <filesystem>

#include "exchange/matching_engine.hpp"
#include "exchange/protocol.hpp"
#include "exchange/snapshot.hpp"
#include "test_helpers.hpp"

using namespace exchange;
using namespace exchange::test;

TEST_CASE("Account - positions follow fills on both sides", "[account]") {
    MatchingEngine engine;
    engine.place_order(limit("alice", Side::SELL, 100, 3 * PRICE_SCALE));
    engine.place_order(limit("bob", Side::BUY, 100, 2 * PRICE_SCALE));

    auto alice = engine.get_account("alice");
    auto bob = engine.get_account("bob");

    REQUIRE(bob.positions.at("BTC-USD") == Position{2 * PRICE_SCALE, -200 * PRICE_SCALE});
    REQUIRE(alice.positions.at("BTC-USD") == Position{-2 * PRICE_SCALE, 200 * PRICE_SCALE});

    REQUIRE(alice.open_orders.size() == 1);
    REQUIRE(alice.open_orders[0].remaining_qty == PRICE_SCALE);
    REQUIRE(bob.open_orders.empty());
}

TEST_CASE("Account - unknown account is empty", "[account]") {
    MatchingEngine engine;
    auto v = engine.get_account("nobody");
    REQUIRE(v.positions.empty());
    REQUIRE(v.open_orders.empty());
}

TEST_CASE("Account - notional does not overflow on large orders", "[account]") {
    // 1,000 units at 1,000,000.00: the raw fixed-point product is about 1e27.
    REQUIRE(notional(1'000'000 * PRICE_SCALE, 1'000 * PRICE_SCALE) == 1'000'000'000 * PRICE_SCALE);
    REQUIRE(notional(PRICE_SCALE / 2, 3) == 1);  // rounds toward zero
}

TEST_CASE("Account - positions survive log replay and snapshot recovery", "[account][recovery]") {
    auto dir = std::filesystem::temp_directory_path() /
               ("bx_account_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    const std::string log = (dir / "events.jsonl").string();
    const std::string snaps = (dir / "snapshots").string();

    AccountView before;
    {
        MatchingEngine engine(log, snaps);
        engine.place_order(limit("alice", Side::SELL, 100, 5));
        engine.place_order(limit("bob", Side::BUY, 100, 3));
        SnapshotManager(snaps).save(engine.create_snapshot());
        engine.place_order(limit("bob", Side::BUY, 100, 1));  // after the snapshot
        before = engine.get_account("alice");
    }

    {
        MatchingEngine from_log(log);
        REQUIRE(from_log.recover());
        REQUIRE(from_log.get_account("alice").positions == before.positions);
    }
    {
        MatchingEngine from_snapshot(log, snaps);
        REQUIRE(from_snapshot.recover());
        auto after = from_snapshot.get_account("alice");
        REQUIRE(after.positions == before.positions);
        REQUIRE(after.open_orders.size() == 1);
        REQUIRE(after.open_orders[0].remaining_qty == 1);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Account - get_account over the protocol", "[account][protocol]") {
    MatchingEngine engine;
    ProtocolHandler h(engine);
    engine.place_order(limit("alice", Side::SELL, 100, 5));
    engine.place_order(limit("bob", Side::BUY, 100, 2));

    auto out = nlohmann::json::parse(h.handle(R"({"cmd":"get_account","account_id":"bob"})"));
    REQUIRE(out["success"] == true);
    REQUIRE(out["data"]["positions"]["BTC-USD"]["base"] == 2);
    REQUIRE(out["data"]["open_orders"].empty());
}
