# Limit Order Book & Matching Engine Simulator

A simulator of the core of an electronic exchange, built in **C++17** and **JavaScript**. It matches orders with price-time priority, runs a market-making bot against random order flow, and benchmarks throughput and latency.

**Live demo:** https://harsha-naga-sai.github.io/Limit-Order-Book/  
(Works on a phone: tap **Start** to watch the book, then **Run Benchmark**.)

## Features
- **Limit order book** with price-time (FIFO) priority; bids and asks stored as sorted price levels
- **Matching engine** supporting limit orders, market orders, partial fills and cancels
- **Random order flow** generator: random-walk fair price, exponentially distributed order placement, cancels
- **Market-making bot** that quotes both sides, skews quotes against its inventory, caps position size and tracks mark-to-market PnL
- **Live dashboard** (web): messages/sec, average latency, spread, mid price, depth chart, order ladder, trade tape, PnL curve
- **Built-in benchmarks** in both versions

## Benchmark results

**Web version (JavaScript, run in a phone browser)**

| Metric | Result |
|---|---|
| Messages processed | 200,000 (orders + cancels) |
| Time | 97 ms |
| Throughput | 2,068,252 msg/s |
| Average latency | 0.48 µs per message |
| Trades executed | 113,432 |

**C++ version** (sample run, `g++ -O2 -std=c++17`, Linux server, 1M messages)

| Metric | Result |
|---|---|
| Throughput | ~6.8M msg/s |
| Average latency | ~150 ns per message |
| p50 / p99 / p99.9 | 160 ns / 449 ns / 868 ns |

Results vary by machine and compiler flags.

## How it works
- Prices are integer ticks ($0.01) to avoid floating-point errors.
- Each price level holds a FIFO queue of orders, so the oldest order at a price fills first.
- An incoming order walks the opposite side from the best price while it crosses; leftover limit quantity rests in the book, and market orders never rest.
- **C++:** `std::map` for price levels, `std::list` for queues, and an `unordered_map` from order ID to its list position for O(1) cancel lookup.
- **JavaScript:** sorted arrays of price levels with binary-search insertion.

## Run it

**Web version:** open `index.html` in any browser, or use the live demo above.

**C++ version:**
```bash
cd cpp
g++ -O2 -std=c++17 main.cpp -o lob
./lob
```
It runs a self-test (price-time priority, partial fills, cancels, market orders), then a throughput run and a latency-percentile run.

## Project structure
```
index.html     web simulator (engine + dashboard)
cpp/main.cpp   C++17 engine, market maker and benchmarks
README.md
```

## Possible extensions
- Replace `std::map` with a flat array-based book and compare latency
- Add IOC / FOK order types and self-trade prevention
- Add momentum or mean-reversion strategies and compare PnL
