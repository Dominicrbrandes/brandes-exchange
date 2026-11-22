# Architecture

## Components

```
Browser (React UI)
      |  HTTP, polled
      v
FastAPI service        auth, rate limits, validation, metrics, demo balances
      |  one JSON command per line over stdin/stdout
      v
C++ engine process     matching, order book, event log, recovery
```

The engine runs as a subprocess of the API. It is single-threaded and handles
one command at a time, so every run of the same command sequence produces the
same state. That determinism is what makes replay-based recovery work.

## Engine

| File | Role |
|---|---|
| `order_book.{hpp,cpp}` | Price levels and per-level queues for one symbol |
| `matching_engine.{hpp,cpp}` | Order lifecycle: risk checks, matching, time in force, cancel/replace, positions, recovery |
| `event_log.{hpp,cpp}` | Append-only JSON-lines log with continuous sequence numbers |
| `snapshot.{hpp,cpp}` | Snapshot of live orders, ids, idempotency keys and positions |
| `risk_checks.{hpp,cpp}` | Price, size, notional and symbol limits |
| `protocol.{hpp,cpp}` | JSON command handler used by `main.cpp` |

### Order book

Each side is a `std::map` from price to level, ordered best price first. Each
level is an intrusive doubly linked list of nodes in arrival order, and a hash
map takes an order id straight to its node:

- add to the back of a level: O(1), plus O(log L) when the level is new
- remove from anywhere in a level (cancel, or a fill at the front): O(1), plus
  O(log L) when the level empties
- change size in place: O(1), keeping time priority

L is the number of price levels on that side. Levels keep a running total of
resting quantity and an order count, so a depth snapshot costs O(depth). Nodes
come from a `std::deque` (stable addresses) and are recycled through a free
list, so steady-state trading does not allocate in the book.

### Matching

An incoming order walks the opposite side best price first and each level's
queue front to back, trading at the resting order's price until it is filled,
the next level is beyond its limit, or the book runs out.

- **Self-trade prevention:** resting orders from the same account are stepped
  over, not traded against and not treated as a barrier.
- **GTC:** any remainder rests on the book.
- **IOC:** any remainder is cancelled and logged. Market orders are always IOC
  or FOK.
- **FOK:** before matching, the engine sums the size it could fill, walking the
  book exactly as matching would (same limit and same self-trade rule). If that
  is short, the order is cancelled with no trades.
- **Market order with nothing to trade against:** rejected (`NO_LIQUIDITY`).

### Cancel/replace

`replace_order(id, price, quantity)` sets a new price and a new total quantity
(filled plus remaining) under the same id. It must leave something unfilled
and pass the same risk checks as a new order.

- Same price and the size does not grow: the order keeps its queue position.
- Otherwise it leaves the queue and joins the back of the new level. If the new
  price crosses the spread it matches first, like a new order.

### Event log and recovery

Every state change is appended to the log: `ORDER_PLACED`, `TRADE_EXECUTED`,
`ORDER_CANCELLED` (with a reason: user, IOC, FOK or market remainder),
`ORDER_REJECTED` and `ORDER_REPLACED`. Sequence numbers continue across
restarts.

On start, `recover()` loads the newest snapshot if there is one and replays the
log entries after it; otherwise it replays the whole log. Replay applies fills
through the book, so level totals and queue order come out identical. The
model tests in `engine/tests/test_model.cpp` check this against the live
engine after thousands of random operations, for both recovery paths.

Snapshots are written in queue order and carry every idempotency key (including
those of finished orders) and every account's positions. The server binary does
not take snapshots on a schedule yet; tests take them explicitly.

## Invariants checked by the tests

1. Two different accounts are never left crossed. (The same account can be,
   because self-trade prevention steps over its own orders.)
2. Every live order is resting in the book, and every resting order is a live
   GTC limit order with positive remaining size.
3. Each level's total equals the sum of its queue, and levels are strictly
   ordered.
4. An order's filled quantity equals the sum of its reported trades.
5. FOK fills completely or not at all; IOC and market orders never rest.
6. Replaying the log, or a snapshot plus the log tail, reproduces the book,
   queue order, orders, trades and positions exactly.

## API layer

`api/app` is a FastAPI service. `engine_client.py` owns the engine subprocess
and serializes commands to it. Routes under `/api/v1`:

| Method and path | Purpose |
|---|---|
| `POST /orders` | place an order (limit or market; GTC, IOC or FOK) |
| `PATCH /orders/{id}` | cancel/replace |
| `DELETE /orders/{id}` | cancel |
| `GET /orders/{id}` | order state |
| `GET /book/{symbol}` | depth by level |
| `GET /trades/{symbol}` | recent trades |
| `GET /accounts/{id}/balances` | demo balances from positions and open orders |
| `GET /stats`, `GET /health` | engine counters and liveness |
| `POST /admin/reset` | restart the engine with an empty log (admin key) |

Balances are a fixed demo grant plus the account's fills, with funds held by
resting orders shown as reserved. They are for display; the engine does not
enforce them.

## Trade-offs

| Decision | Reason |
|---|---|
| Single thread | Determinism makes replay exact and the code simple; one core already matches over a million orders per second |
| Subprocess with JSON lines | Crash isolation and easy debugging, at the cost of serialization per request |
| JSON event log | Readable and easy to diff; it is also most of the cost on the durable path |
| Fixed-point integers (1e8) | Exact, deterministic arithmetic; cash amounts use 128-bit intermediates |
