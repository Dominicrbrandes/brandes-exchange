# Brandes Exchange

[![CI](https://github.com/Dominicrbrandes/brandes-exchange/actions/workflows/ci.yml/badge.svg)](https://github.com/Dominicrbrandes/brandes-exchange/actions/workflows/ci.yml)

A limit order book matching engine in C++20, with an event-sourced recovery
path, a FastAPI service in front of it, and a React trading UI.

The engine is single-threaded and deterministic: every state change is written
to an append-only event log, and replaying that log rebuilds the exact same
book, queue positions, orders and positions. Randomized tests check that
property across every order type.

## What it does

- **Price-time priority matching** for limit and market orders, with partial
  fills across any number of price levels.
- **Time in force:** GTC, IOC and FOK. FOK checks that the full size is
  available (within the limit price, ignoring the trader's own orders) before
  it touches the book, so a killed order leaves no trades behind.
- **Cancel and cancel/replace.** A replace keeps the order's place in the queue
  only when the price is unchanged and the size does not grow, the usual
  exchange rule. Otherwise it goes to the back of its new level, and if the new
  price crosses the spread it trades first.
- **Self-trade prevention.** An incoming order steps over the same account's
  resting orders and keeps matching deeper in the book.
- **Risk checks and idempotency keys.** Price, size, notional and symbol limits;
  a retried request with the same idempotency key is rejected, including after
  a restart.
- **Event log and recovery.** Every placement, trade, cancel, replace and
  rejection is logged as JSON lines. On start the engine replays the log.
  Recovery from a snapshot plus the log tail is also implemented and tested;
  the server does not take snapshots on its own yet.
- **Per-account positions** built from fills, used by the API to show demo
  balances.

## Performance

Single core of an Intel i7-13700H laptop, WSL2 Ubuntu, GCC 15, `-O3`, process
pinned with `taskset`. Each figure is the median of five runs; latency is
measured per call with the CPU timestamp counter. These are in-process numbers
for the engine alone, not through the API.

| Scenario | Orders/sec | p50 | p99 | p99.9 |
|---|---:|---:|---:|---:|
| Mixed flow, realistic depth | 1.60M | 374 ns | 1.32 µs | 3.05 µs |
| Heavy matching, tight book | 1.56M | 360 ns | 2.16 µs | 5.21 µs |
| Resting only, no crosses | 2.02M | 283 ns | 1.69 µs | 3.49 µs |
| Cancel, 100k resting orders | 1.03M | 905 ns | 1.46 µs | 15.5 µs |
| Takers sweeping a 10k-deep level | 3.24M | 295 ns | 498 ns | 869 ns |
| Mixed flow, event log buffered | 442K | 1.58 µs | 7.34 µs | 21.9 µs |
| Mixed flow, log flushed per event | 313K | 2.13 µs | 9.09 µs | 27.8 µs |

"Flushed per event" hands every event to the operating system before the call
returns; it does not `fsync`.

The order book used to keep each price level in a `std::vector`, so a cancel
searched and shifted the level and every fill at the front shifted the whole
queue. Each level is now an intrusive FIFO list with an id index, which makes
cancels and fills O(1). Same benchmark, before and after that change:

| Scenario | Before | After |
|---|---:|---:|
| Sweeping a 10k-deep level | 270K/sec, p50 3.55 µs | 3.24M/sec, p50 295 ns |
| Cancel, 100k resting orders | 146K/sec, p50 6.25 µs | 1.03M/sec, p50 905 ns |
| Heavy matching p99 | 6.51 µs | 2.16 µs |

Full method and raw tables: [docs/latency-report.md](docs/latency-report.md).

## Testing

- **82 C++ tests** (Catch2), run in CI under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- **Model tests** drive thousands of random placements, cancels and replaces
  across all order types and check invariants after every step: level totals
  match their queues, two different accounts are never left crossed, FOK is all
  or nothing, IOC and market orders never rest, and filled quantity equals the
  sum of reported trades. They then rebuild the engine from the event log, and
  from a snapshot plus the log tail, and require the identical state.
- **100% line coverage** of `matching_engine.cpp` and `order_book.cpp`,
  measured with `llvm-cov` and enforced in CI.
- **API tests** start the real engine and go through HTTP: trading, IOC and
  FOK, cancel/replace, balances and reset.

The model tests found two bugs in the original recovery code, both fixed:
snapshots were written in hash-map order, which scrambled time priority within
a price level on restore, and the event log restarted its sequence numbers
after a restart, so a later snapshot recovery replayed old events twice.

## Layout

```
engine/       C++ engine: order book, matching, event log, snapshots, protocol
  tests/      Catch2 unit, model and recovery tests
benchmarks/   latency and throughput benchmark
api/          FastAPI service; runs the engine as a subprocess over JSON lines
ui/           React + TypeScript UI with canvas price and depth charts
docs/         architecture, latency report, threat model
```

## Build and run

Requires CMake 3.16+, a C++20 compiler, Python 3.11+ and Node 18+.

```bash
# Engine and tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# Benchmark
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build -j --target exchange_benchmark
taskset -c 2 ./build/engine/exchange_benchmark

# API (finds the engine in build/engine)
cd api && pip install -r requirements.txt
uvicorn app.main:app --port 8000

# UI, in another terminal (proxies /api to port 8000)
cd ui && npm install && npm run dev
```

The engine also runs on its own and reads one JSON command per line:

```bash
./build/engine/exchange_engine --event-log data/events.jsonl
{"cmd":"place_order","order":{"account_id":"a","symbol":"BTC-USD","side":"SELL","type":"LIMIT","price":5000000000000,"quantity":100000000}}
{"cmd":"replace_order","order_id":1,"price":5010000000000,"quantity":50000000}
```

Prices and quantities are fixed-point integers with 1e8 = 1.0.

## Limits

- One thread and one process; no sharding or replication.
- Balances shown in the UI are a demo grant plus fills. The engine does not
  check balances before accepting an order.
- API keys are fixed demo values from `api/app/config.py`.

## License

MIT
