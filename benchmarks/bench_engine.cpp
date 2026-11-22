// In-process matching engine benchmark.
//
// This measures the engine, not the transport. Orders are generated up front so
// no RNG, allocation or formatting happens inside the timed region, and each
// place_order call is timed individually with the cycle counter so we can report
// a real latency distribution rather than a wall-clock average.
//
// The previous benchmark shelled out to curl once per order over HTTP, which
// measured process spawn cost and told us nothing about matching.

#include <x86intrin.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "exchange/matching_engine.hpp"

using namespace exchange;

namespace {

inline uint64_t cycles() {
    _mm_lfence();
    const uint64_t c = __rdtsc();
    _mm_lfence();
    return c;
}

// The TSC runs at a fixed rate unrelated to the current core frequency, so it
// has to be calibrated against a real clock before we can report nanoseconds.
double calibrate_ghz() {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const uint64_t c0 = cycles();
    while (clock::now() - t0 < std::chrono::milliseconds(200)) {
    }
    const uint64_t c1 = cycles();
    const auto t1 = clock::now();
    const double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    return static_cast<double>(c1 - c0) / ns;
}

struct Stats {
    double p50, p90, p99, p999, max, mean;
    double throughput;
};

Stats summarize(std::vector<uint64_t>& samples, double ghz, double total_ns) {
    std::sort(samples.begin(), samples.end());
    const auto at = [&](double q) {
        const size_t i = static_cast<size_t>(q * (samples.size() - 1));
        return samples[i] / ghz;
    };
    double sum = 0;
    for (uint64_t s : samples) sum += s;

    Stats st{};
    st.p50 = at(0.50);
    st.p90 = at(0.90);
    st.p99 = at(0.99);
    st.p999 = at(0.999);
    st.max = samples.back() / ghz;
    st.mean = (sum / samples.size()) / ghz;
    st.throughput = samples.size() / (total_ns / 1e9);
    return st;
}

void report(const char* name, const Stats& s, size_t n, uint64_t trades) {
    std::printf("%-32s %8zu %8llu %10.0f %8.0f %8.0f %8.0f %9.0f %10.0f %10.0f\n", name, n,
                static_cast<unsigned long long>(trades), s.throughput, s.mean, s.p50, s.p90, s.p99,
                s.p999, s.max);
}

enum class Mode {
    Resting,   // bids strictly below asks, so nothing ever crosses
    Crossing,  // both sides drawn from one overlapping band, so most orders fill
};

// Pre-generated order stream. Accounts are drawn from a small pool so that
// self-trade prevention is genuinely exercised rather than never triggering.
std::vector<Order> make_orders(size_t n, Mode mode, int64_t band, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int64_t> offset(0, band);
    std::uniform_int_distribution<int64_t> qty(1, 20);
    std::uniform_int_distribution<int> side(0, 1);
    std::uniform_int_distribution<int> acct(0, 15);

    constexpr int64_t mid = 50000;

    std::vector<Order> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        Order o;
        o.account_id = "acct" + std::to_string(acct(rng));
        o.symbol = "BTC-USD";
        o.side = side(rng) ? Side::BUY : Side::SELL;
        o.type = OrderType::LIMIT;
        o.quantity = qty(rng);

        if (mode == Mode::Resting) {
            // Keep a hard gap between the sides so every order rests untouched
            // and we are timing the insert path alone.
            o.price =
                (o.side == Side::BUY) ? (mid - band - offset(rng)) : (mid + band + offset(rng));
        } else {
            o.price = mid - band / 2 + offset(rng);
        }
        o.price *= PRICE_SCALE;
        out.push_back(std::move(o));
    }
    return out;
}

void run(const char* name, size_t n, Mode mode, int64_t band, bool with_log, double ghz,
         Durability durability = Durability::PerEvent) {
    auto orders = make_orders(n, mode, band, 1234);

    const std::string log_path = with_log ? std::string("bench_events.jsonl") : std::string();
    if (with_log) std::remove(log_path.c_str());

    MatchingEngine engine(log_path, "", 1000000, durability);

    // Warm up caches and let the book reach a steady state before timing.
    const size_t warm = n / 10;
    for (size_t i = 0; i < warm; ++i) engine.place_order(orders[i]);

    std::vector<uint64_t> samples;
    samples.reserve(n - warm);

    const uint64_t start = cycles();
    for (size_t i = warm; i < n; ++i) {
        const uint64_t t0 = cycles();
        auto r = engine.place_order(orders[i]);
        const uint64_t t1 = cycles();
        samples.push_back(t1 - t0);
        // Keep the result observable so the optimizer cannot elide the call.
        if (r.order.id == ~0ULL) std::printf("unreachable\n");
    }
    const uint64_t end = cycles();

    const double total_ns = (end - start) / ghz;
    Stats s = summarize(samples, ghz, total_ns);
    report(name, s, samples.size(), engine.get_stats().total_trades);

    if (with_log) std::remove(log_path.c_str());
}

// Cancel latency against deep queues. Builds a book of `depth` resting orders
// spread over `levels` prices, then cancels a random resting order (timed) and
// places a fresh one (untimed) so the depth stays constant. A per-level vector
// pays for every cancel with a linear search and shift of that level.
void run_cancels(const char* name, size_t depth, int64_t levels, size_t n, double ghz) {
    MatchingEngine engine("", "", 1000000);
    std::mt19937 rng(99);
    std::uniform_int_distribution<int64_t> level(0, levels - 1);

    auto place = [&]() {
        Order o;
        o.account_id = "maker";
        o.symbol = "BTC-USD";
        o.side = Side::SELL;
        o.type = OrderType::LIMIT;
        o.quantity = 1;
        o.price = (50000 + level(rng)) * PRICE_SCALE;
        return engine.place_order(o).order.id;
    };

    std::vector<uint64_t> live;
    live.reserve(depth);
    for (size_t i = 0; i < depth; ++i) live.push_back(place());

    std::uniform_int_distribution<size_t> pick(0, depth - 1);
    std::vector<uint64_t> samples;
    samples.reserve(n);
    uint64_t timed = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t k = pick(rng);
        const uint64_t t0 = cycles();
        auto r = engine.cancel_order(live[k]);
        const uint64_t t1 = cycles();
        samples.push_back(t1 - t0);
        timed += t1 - t0;
        if (!r.success) std::printf("cancel failed\n");
        live[k] = place();
    }
    Stats s = summarize(samples, ghz, timed / ghz);
    report(name, s, samples.size(), 0);
}

// Takers sweeping a deep queue of one-lot resting orders at a single price.
// Every fill removes the order at the front of the level, which a per-level
// vector turns into a shift of the whole queue.
void run_sweep(const char* name, size_t depth, size_t n, double ghz) {
    MatchingEngine engine("", "", 1000000);
    const int64_t px = 50000 * PRICE_SCALE;
    size_t maker = 0;

    auto rest = [&]() {
        Order o;
        o.account_id = "maker" + std::to_string(maker++ % 16);
        o.symbol = "BTC-USD";
        o.side = Side::SELL;
        o.type = OrderType::LIMIT;
        o.quantity = 1;
        o.price = px;
        engine.place_order(o);
    };
    for (size_t i = 0; i < depth; ++i) rest();

    Order take;
    take.account_id = "taker";
    take.symbol = "BTC-USD";
    take.side = Side::BUY;
    take.type = OrderType::LIMIT;
    take.quantity = 1;
    take.price = px;

    std::vector<uint64_t> samples;
    samples.reserve(n);
    uint64_t timed = 0;
    uint64_t trades = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint64_t t0 = cycles();
        auto r = engine.place_order(take);
        const uint64_t t1 = cycles();
        samples.push_back(t1 - t0);
        timed += t1 - t0;
        trades += r.trades.size();
        rest();
    }
    Stats s = summarize(samples, ghz, timed / ghz);
    report(name, s, samples.size(), trades);
}

}  // namespace

int main() {
    const double ghz = calibrate_ghz();
    std::printf("TSC calibrated at %.3f GHz\n\n", ghz);

    std::printf("%-32s %8s %8s %10s %8s %8s %8s %9s %10s %10s\n", "scenario", "orders", "trades",
                "ops/sec", "mean ns", "p50 ns", "p90 ns", "p99 ns", "p99.9 ns", "max ns");
    std::printf("%s\n", std::string(126, '-').c_str());

    run("resting only (no crosses)", 200000, Mode::Resting, 500, false, ghz);
    run("heavy matching (tight book)", 200000, Mode::Crossing, 4, false, ghz);
    run("mixed (realistic depth)", 200000, Mode::Crossing, 100, false, ghz);
    run("mixed + log (flush per event)", 50000, Mode::Crossing, 100, true, ghz,
        Durability::PerEvent);
    run("mixed + log (buffered)", 200000, Mode::Crossing, 100, true, ghz, Durability::Buffered);
    run_cancels("cancel, 10k deep over 5 levels", 10000, 5, 100000, ghz);
    run_cancels("cancel, 100k deep over 5 levels", 100000, 5, 20000, ghz);
    run_sweep("sweep a 10k-deep level", 10000, 100000, ghz);

    return 0;
}
