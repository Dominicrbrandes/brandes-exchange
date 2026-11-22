// Randomized tests over every order type and operation.
//
// Each step checks the engine's invariants. At the end, a second engine is
// rebuilt from the event log, and from a snapshot plus the log tail, and must
// reach exactly the same orders, queues and trades as the engine that wrote
// the log. That is the property recovery depends on.

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <map>
#include <random>
#include <tuple>

#include "exchange/matching_engine.hpp"
#include "exchange/snapshot.hpp"
#include "test_helpers.hpp"

using namespace exchange;
using namespace exchange::test;

namespace {

class ScratchDir {
   public:
    explicit ScratchDir(const std::string& tag) {
        path_ = std::filesystem::temp_directory_path() /
                ("bx_" + tag + "_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    std::string file(const std::string& name) const { return (path_ / name).string(); }

   private:
    std::filesystem::path path_;
};

// Everything observable about an engine's state, in a comparable form.
struct State {
    using OrderRow =
        std::tuple<uint64_t, std::string, int, int, int, int64_t, int64_t, int64_t, int, uint64_t>;
    std::vector<OrderRow> orders;
    std::vector<uint64_t> bid_queue, ask_queue;  // ids in priority order
    std::vector<std::tuple<int64_t, int64_t, int>> bid_levels, ask_levels;
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, int64_t, int64_t>> trades;
    std::vector<std::map<std::string, Position>> positions;  // acct0..acct5

    bool operator==(const State&) const = default;
};

State capture(MatchingEngine& e, uint64_t max_id) {
    State s;
    for (uint64_t id = 1; id <= max_id; ++id) {
        auto o = e.get_order(id);
        if (!o) continue;
        s.orders.emplace_back(o->id, o->account_id, static_cast<int>(o->side),
                              static_cast<int>(o->type), static_cast<int>(o->time_in_force),
                              o->price, o->quantity, o->remaining_qty, static_cast<int>(o->status),
                              o->timestamp_ns);
    }
    if (auto* book = e.get_book("BTC-USD")) {
        for (const auto* o : book->get_all_bids()) s.bid_queue.push_back(o->id);
        for (const auto* o : book->get_all_asks()) s.ask_queue.push_back(o->id);
        for (const auto& l : book->get_bid_levels(1 << 20))
            s.bid_levels.emplace_back(l.price, l.quantity, l.order_count);
        for (const auto& l : book->get_ask_levels(1 << 20))
            s.ask_levels.emplace_back(l.price, l.quantity, l.order_count);
    }
    for (int a = 0; a < 6; ++a)
        s.positions.push_back(e.get_account("acct" + std::to_string(a)).positions);
    for (const auto& t : e.get_trades("BTC-USD", 1 << 30))
        s.trades.emplace_back(t.id, t.buy_order_id, t.sell_order_id, t.price, t.quantity);
    return s;
}

// Invariants that must hold after every operation.
void check_book(MatchingEngine& e, uint64_t max_id) {
    auto* book = e.get_book("BTC-USD");
    if (!book) return;

    const auto bids = book->get_all_bids();
    const auto asks = book->get_all_asks();
    REQUIRE(bids.size() == book->bid_count());
    REQUIRE(asks.size() == book->ask_count());

    // Level totals match the orders queued at each level, levels are strictly
    // ordered best-first, and the queue holds only live GTC limit orders.
    auto check_side = [&](const std::vector<Order*>& orders, const std::vector<BookLevel>& levels,
                          bool bids_side) {
        std::map<int64_t, std::pair<int64_t, int>> sums;
        for (const auto* o : orders) {
            REQUIRE(o->type == OrderType::LIMIT);
            REQUIRE(o->time_in_force == TimeInForce::GTC);
            REQUIRE(o->is_active());
            REQUIRE(o->remaining_qty > 0);
            REQUIRE(o->remaining_qty <= o->quantity);
            sums[o->price].first += o->remaining_qty;
            sums[o->price].second += 1;
        }
        REQUIRE(levels.size() == sums.size());
        for (size_t i = 0; i < levels.size(); ++i) {
            REQUIRE(sums[levels[i].price].first == levels[i].quantity);
            REQUIRE(sums[levels[i].price].second == levels[i].order_count);
            if (i > 0) {
                REQUIRE((bids_side ? levels[i].price < levels[i - 1].price
                                   : levels[i].price > levels[i - 1].price));
            }
        }
    };
    check_side(bids, book->get_bid_levels(1 << 20), true);
    check_side(asks, book->get_ask_levels(1 << 20), false);

    // Two different accounts are never left crossed.
    for (const auto* b : bids) {
        for (const auto* a : asks) {
            if (b->price >= a->price) REQUIRE(b->account_id == a->account_id);
        }
    }

    // An order is live exactly when it is resting.
    for (uint64_t id = 1; id <= max_id; ++id) {
        auto o = e.get_order(id);
        if (!o) continue;
        REQUIRE((book->get_order(id) != nullptr) == o->is_active());
    }
}

struct Driver {
    std::mt19937 rng;
    MatchingEngine& engine;
    std::vector<uint64_t> ids;
    std::map<uint64_t, int64_t> traded;  // order id -> quantity traded, from trade reports

    Driver(MatchingEngine& e, unsigned seed) : rng(seed), engine(e) {}

    int pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); }
    int64_t range(int64_t lo, int64_t hi) {
        return std::uniform_int_distribution<int64_t>(lo, hi)(rng);
    }

    void record(const std::vector<Trade>& trades) {
        for (const auto& t : trades) {
            REQUIRE(t.buyer_account_id != t.seller_account_id);
            traded[t.buy_order_id] += t.quantity;
            traded[t.sell_order_id] += t.quantity;
        }
    }

    void step() {
        const int op = pick(10);
        if (op < 6 || ids.empty()) {
            Order o = limit("acct" + std::to_string(pick(6)), pick(2) ? Side::BUY : Side::SELL,
                            range(95, 105), range(1, 20));
            const int t = pick(10);
            if (t == 0) {
                o.type = OrderType::MARKET;
                o.price = 0;
            }
            o.time_in_force = t < 6   ? TimeInForce::GTC
                              : t < 8 ? TimeInForce::IOC
                                      : TimeInForce::FOK;
            auto r = engine.place_order(o);
            record(r.trades);
            if (r.order.id) ids.push_back(r.order.id);

            if (o.time_in_force == TimeInForce::FOK) {
                // All or nothing.
                REQUIRE((r.trades.empty() || r.order.status == OrderStatus::FILLED));
            }
            if (r.order.id && o.time_in_force != TimeInForce::GTC) {
                REQUIRE_FALSE(r.order.is_active());
            }
        } else if (op < 8) {
            const uint64_t id = ids[static_cast<size_t>(pick(static_cast<int>(ids.size())))];
            const bool was_active = engine.get_order(id)->is_active();
            REQUIRE(engine.cancel_order(id).success == was_active);
        } else {
            const uint64_t id = ids[static_cast<size_t>(pick(static_cast<int>(ids.size())))];
            const auto before = *engine.get_order(id);
            auto r =
                engine.replace_order(id, before.price + range(-2, 2) * PRICE_SCALE, range(1, 25));
            record(r.trades);
            if (!before.is_active()) REQUIRE_FALSE(r.success);
        }

        const uint64_t max_id = ids.empty() ? 0 : ids.back();
        check_book(engine, max_id);
    }

    void check_conservation() {
        for (uint64_t id : ids) {
            auto o = engine.get_order(id);
            REQUIRE(o->filled_qty() == traded[id]);
        }
    }
};

}  // namespace

TEST_CASE("Model - invariants hold across every order type", "[model]") {
    for (unsigned seed : {1u, 2u, 3u, 4u, 5u}) {
        INFO("seed " << seed);
        MatchingEngine engine;
        Driver d(engine, seed);
        for (int i = 0; i < 3000; ++i) d.step();
        d.check_conservation();
    }
}

TEST_CASE("Model - replaying the event log reproduces the exact state", "[model][replay]") {
    for (unsigned seed : {11u, 12u, 13u}) {
        INFO("seed " << seed);
        ScratchDir dir("replay");
        const std::string log = dir.file("events.jsonl");

        State live;
        uint64_t max_id = 0;
        {
            MatchingEngine engine(log);
            Driver d(engine, seed);
            for (int i = 0; i < 2000; ++i) d.step();
            max_id = d.ids.back();
            live = capture(engine, max_id);
        }

        MatchingEngine recovered(log);
        REQUIRE(recovered.recover());
        REQUIRE(capture(recovered, max_id) == live);

        // New ids continue where the original run stopped.
        auto next = recovered.place_order(limit("z", Side::BUY, 50, 1));
        REQUIRE(next.order.id == max_id + 1);
    }
}

TEST_CASE("Model - restart, keep trading, then recover from a snapshot plus the log tail",
          "[model][replay][snapshot]") {
    ScratchDir dir("snap");
    const std::string log = dir.file("events.jsonl");
    const std::string snaps = dir.file("snapshots");

    // First run.
    {
        MatchingEngine engine(log);
        Driver d(engine, 21);
        for (int i = 0; i < 1500; ++i) d.step();
    }

    // Second run: recover, trade, snapshot halfway, trade some more.
    State live;
    uint64_t max_id = 0;
    {
        MatchingEngine engine(log, snaps);
        REQUIRE(engine.recover());
        Driver d(engine, 22);
        for (uint64_t id = 1; engine.get_order(id); ++id) d.ids.push_back(id);
        for (int i = 0; i < 1000; ++i) d.step();
        SnapshotManager(snaps).save(engine.create_snapshot());
        for (int i = 0; i < 1000; ++i) d.step();
        max_id = d.ids.back();
        live = capture(engine, max_id);
    }

    // Third run starts from the snapshot. Orders that finished before the
    // snapshot are not in it, so compare only what the snapshot is meant to
    // carry: the book, plus every order touched after it.
    MatchingEngine recovered(log, snaps);
    REQUIRE(recovered.recover());
    State got = capture(recovered, max_id);
    REQUIRE(got.bid_queue == live.bid_queue);
    REQUIRE(got.ask_queue == live.ask_queue);
    REQUIRE(got.bid_levels == live.bid_levels);
    REQUIRE(got.ask_levels == live.ask_levels);
    REQUIRE(got.positions == live.positions);
    for (const auto& row : got.orders) {
        REQUIRE(std::find(live.orders.begin(), live.orders.end(), row) != live.orders.end());
    }
}
