#include <catch2/catch_all.hpp>

#include <filesystem>

#include "exchange/matching_engine.hpp"
#include "exchange/snapshot.hpp"
#include "test_helpers.hpp"

using namespace exchange;
using namespace exchange::test;

namespace {

struct Scratch {
    std::filesystem::path dir;
    Scratch() {
        dir = std::filesystem::temp_directory_path() /
              ("bx_recovery_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::string log() const { return (dir / "events.jsonl").string(); }
    std::string snaps() const { return (dir / "snapshots").string(); }
};

Order keyed(Order o, const std::string& key) {
    o.idempotency_key = key;
    return o;
}

}  // namespace

TEST_CASE("Recovery - idempotency keys survive a restart from the log", "[recovery]") {
    Scratch s;
    {
        MatchingEngine engine(s.log());
        REQUIRE(engine.place_order(keyed(limit("a", Side::BUY, 100, 5), "k1")).success);
    }

    MatchingEngine recovered(s.log());
    REQUIRE(recovered.recover());
    auto retry = recovered.place_order(keyed(limit("a", Side::BUY, 100, 5), "k1"));
    REQUIRE_FALSE(retry.success);
    REQUIRE(retry.error_code == ErrorCode::DUPLICATE_IDEMPOTENCY_KEY);
}

TEST_CASE("Recovery - a snapshot keeps the keys of orders that already finished",
          "[recovery][snapshot]") {
    Scratch s;
    {
        MatchingEngine engine(s.log(), s.snaps());
        engine.place_order(keyed(limit("a", Side::SELL, 100, 5), "filled-before-snapshot"));
        engine.place_order(keyed(limit("b", Side::SELL, 101, 5), "still-resting"));
        engine.place_order(limit("c", Side::BUY, 101, 5));  // fills the 100 offer
        SnapshotManager(s.snaps()).save(engine.create_snapshot());
    }

    MatchingEngine recovered(s.log(), s.snaps());
    REQUIRE(recovered.recover());
    for (const char* key : {"filled-before-snapshot", "still-resting"}) {
        auto retry = recovered.place_order(keyed(limit("a", Side::SELL, 100, 5), key));
        REQUIRE(retry.error_code == ErrorCode::DUPLICATE_IDEMPOTENCY_KEY);
    }
    REQUIRE(recovered.get_book("BTC-USD")->get_ask_levels()[0].price == px(101));
}

TEST_CASE("Recovery - event numbering continues after a restart", "[recovery]") {
    Scratch s;
    uint64_t last = 0;
    {
        MatchingEngine engine(s.log());
        engine.place_order(limit("a", Side::SELL, 100, 5));
        engine.place_order(limit("b", Side::BUY, 100, 2));
        last = engine.get_stats().event_sequence;
        REQUIRE(last == 3);  // two placements and one trade
    }

    MatchingEngine restarted(s.log());
    REQUIRE(restarted.get_stats().event_sequence == last);
    REQUIRE(restarted.recover());
    restarted.cancel_order(1);
    REQUIRE(restarted.get_stats().event_sequence == last + 1);
}

TEST_CASE("Recovery - replay skips orders it already has and inert events", "[recovery]") {
    MatchingEngine engine;
    auto placed = engine.place_order(limit("a", Side::BUY, 100, 5));

    Event again;
    again.type = EventType::ORDER_PLACED;
    again.payload = placed.order;
    Event marker;
    marker.type = EventType::SNAPSHOT_MARKER;
    Event replace_unknown;
    replace_unknown.type = EventType::ORDER_REPLACED;
    replace_unknown.payload = {{"order_id", 999}};

    engine.replay_events({again, marker, replace_unknown});

    auto* book = engine.get_book("BTC-USD");
    REQUIRE(book->bid_count() == 1);
    REQUIRE(book->get_bid_levels()[0].quantity == 5);
}
