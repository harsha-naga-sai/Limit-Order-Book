# Limit Order Book & Matching Engine Simulator

A browser-based simulator of the core of an electronic exchange, written in plain JavaScript with no dependencies.

**Live demo:** _add your GitHub Pages link here_

## Features
- **Limit order book** with price-time (FIFO) priority, using sorted price levels and binary-search insertion
- **Matching engine** supporting limit orders, market orders, partial fills and cancels
- **Random order flow** generator: random-walk fair price, exponentially distributed order placement, cancels
- **Market-making bot** that quotes both sides, skews quotes against inventory, caps position size and tracks mark-to-market PnL
- **Live dashboard:** messages/sec, average latency, spread, mid price, depth chart, order ladder, trade tape, PnL curve
- **Built-in benchmark:** pushes 200,000 messages through a fresh engine and reports throughput and µs per message

## How it works
- Prices are stored as integer ticks ($0.01) to avoid floating-point errors.
- Bids and asks are arrays of price levels; each level holds a FIFO queue of orders.
- An incoming order walks the opposite side from the best price while it crosses, filling the oldest orders first.
- Any unfilled limit quantity rests in the book; market orders never rest.

## Run locally
Open `index.html` in any browser.

## Possible extensions
- Port the engine to C++ and compare latency
- Add IOC / FOK order types and self-trade prevention
- Add a momentum or mean-reversion strategy and compare PnL
