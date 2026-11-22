# Latency Report

## What is measured

`benchmarks/bench_engine.cpp` calls the engine in-process. Orders are generated
before timing starts, so no random number generation, allocation for inputs or
formatting happens inside the timed region. Each call is timed on its own with
the CPU timestamp counter (`rdtsc` fenced with `lfence`), calibrated against
`steady_clock`, which gives a full latency distribution and not just an
average. The first 10% of each run warms caches and the book and is not timed.

The API and the JSON-over-pipes link to it are not included. An earlier
version of this report measured orders sent one at a time with `curl` over
HTTP (about 312 orders/sec, 2.4 ms median); that measured process start-up and
HTTP, not matching, and has been replaced.

## Setup

- Intel Core i7-13700H laptop, WSL2 Ubuntu, GCC 15.2
- `CMAKE_BUILD_TYPE=Release` (`-O3 -DNDEBUG`)
- Process pinned to one core: `taskset -c 2 ./exchange_benchmark`
- Event-log scenarios write to the WSL ext4 filesystem
- Five runs, alternating the old and new builds to share any thermal drift;
  tables show the median of the five

## Scenarios

| Scenario | Orders | What it stresses |
|---|---:|---|
| Resting only | 180,000 | insert path; bids and asks never overlap |
| Heavy matching | 180,000 | tight four-tick band; 161k trades |
| Mixed, realistic depth | 180,000 | 100-tick band; 150k trades |
| Mixed + log, flushed per event | 45,000 | as above, each event handed to the OS before returning |
| Mixed + log, buffered | 180,000 | as above, log written through a buffer |
| Cancel, 10k deep over 5 levels | 100,000 | cancel a random resting order, re-place it untimed |
| Cancel, 100k deep over 5 levels | 20,000 | same with ten times the depth |
| Sweep a 10k-deep level | 100,000 | one-lot takers against a queue of one-lot makers |

Orders come from 16 accounts, so self-trade prevention is exercised.

## Results

Current engine (intrusive per-level queues):

| Scenario | Orders/sec | p50 | p99 | p99.9 |
|---|---:|---:|---:|---:|
| Resting only | 2,015,271 | 283 ns | 1,690 ns | 3,488 ns |
| Heavy matching | 1,560,942 | 360 ns | 2,164 ns | 5,212 ns |
| Mixed, realistic depth | 1,604,721 | 374 ns | 1,318 ns | 3,050 ns |
| Mixed + log, flushed per event | 313,415 | 2,129 ns | 9,090 ns | 27,848 ns |
| Mixed + log, buffered | 442,414 | 1,584 ns | 7,340 ns | 21,939 ns |
| Cancel, 10k deep | 1,438,722 | 649 ns | 1,066 ns | 4,440 ns |
| Cancel, 100k deep | 1,034,693 | 905 ns | 1,458 ns | 15,507 ns |
| Sweep a 10k-deep level | 3,241,708 | 295 ns | 498 ns | 869 ns |

Previous engine (`std::vector` per price level), same machine and method:

| Scenario | Orders/sec | p50 | p99 | p99.9 |
|---|---:|---:|---:|---:|
| Resting only | 1,469,630 | 357 ns | 1,867 ns | 5,416 ns |
| Heavy matching | 1,093,771 | 430 ns | 6,508 ns | 16,698 ns |
| Mixed, realistic depth | 1,486,004 | 417 ns | 1,560 ns | 3,733 ns |
| Mixed + log, flushed per event | 303,949 | 2,164 ns | 9,666 ns | 32,769 ns |
| Mixed + log, buffered | 423,453 | 1,634 ns | 7,811 ns | 23,625 ns |
| Cancel, 10k deep | 935,436 | 995 ns | 1,497 ns | 13,388 ns |
| Cancel, 100k deep | 145,692 | 6,245 ns | 13,594 ns | 63,165 ns |
| Sweep a 10k-deep level | 270,409 | 3,553 ns | 4,964 ns | 31,196 ns |

## Reading the numbers

- The vector-based book paid for every cancel with a linear search and shift of
  the level, and for every fill at the front of a queue with a shift of the
  whole queue. Deep queues made both linear. The linked queues make them
  constant time, which is where the 7x (cancel, 100k deep) and 12x (sweep)
  gains come from.
- On a shallow, realistic book the gain is smaller (about 8% throughput),
  because levels are short and `std::map` lookups and trade bookkeeping
  dominate.
- With logging on, formatting and writing JSON dominates: about 1.2 µs of the
  1.6 µs median in buffered mode. A binary log format is the next step if the
  durable path matters.
- "Flushed per event" means each event reaches the operating system before the
  call returns. It does not `fsync`, so it survives a process crash but not a
  power loss.
- Maximum latencies (not shown) reach several milliseconds in every scenario,
  including resting-only, which points to the environment (WSL on a laptop)
  more than the matching code, so they are left out.

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build -j --target exchange_benchmark
cd /tmp && taskset -c 2 /path/to/build/engine/exchange_benchmark
```

Run it from a directory on a local Linux filesystem: the log scenarios write
`bench_events.jsonl` to the working directory, and a Windows-mounted path under
WSL is roughly 100 times slower.
