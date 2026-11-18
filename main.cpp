// Limit Order Book & Matching Engine Simulator (C++17)
// Price-time priority, limit/market orders, partial fills, cancels,
// random order flow, inventory-aware market-making bot, latency benchmark.
//
// Build:  g++ -O2 -std=c++17 main.cpp -o lob && ./lob

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;

// Prices are integer ticks (1 tick = $0.01) to avoid floating point errors.
enum class Side : uint8_t { Buy, Sell };
enum Owner : int { FLOW = 0, MKT = 1, MM = 2 };

struct Order { uint64_t id; Side side; int price; int qty; int owner; };
struct Level { list<Order> orders; long long qty = 0; };   // FIFO queue = time priority
struct Loc   { Side side; int price; list<Order>::iterator it; };
struct Trade { int price; int qty; Side takerSide; int taker; int maker; };

class OrderBook {
public:
    map<int, Level, greater<int>> bids;   // best (highest) first
    map<int, Level> asks;                 // best (lowest) first
    unordered_map<uint64_t, Loc> index;   // id -> location, for O(1) cancel lookup
    uint64_t nextId = 1;
    long long tradeCount = 0, volume = 0;
    int lastPrice = -1;
    function<void(const Trade&)> onTrade;

    uint64_t add(Side side, int price, int qty, int owner, bool market = false) {
        uint64_t id = nextId++;
        int rem = (side == Side::Buy) ? match(asks, side, price, qty, owner, market)
                                      : match(bids, side, price, qty, owner, market);
        if (rem > 0 && !market) rest(id, side, price, rem, owner);
        return id;
    }

    bool cancel(uint64_t id) {
        auto f = index.find(id);
        if (f == index.end()) return false;
        Loc loc = f->second;
        if (loc.side == Side::Buy) eraseFrom(bids, loc); else eraseFrom(asks, loc);
        index.erase(f);
        return true;
    }

    int bestBid() const { return bids.empty() ? -1 : bids.begin()->first; }
    int bestAsk() const { return asks.empty() ? -1 : asks.begin()->first; }
    bool alive(uint64_t id) const { return index.count(id) > 0; }

private:
    template <class M>
    int match(M& opp, Side side, int price, int rem, int owner, bool market) {
        while (rem > 0 && !opp.empty()) {
            auto lvlIt = opp.begin();
            int lp = lvlIt->first;
            if (!market && (side == Side::Buy ? lp > price : lp < price)) break;
            Level& lvl = lvlIt->second;
            while (rem > 0 && !lvl.orders.empty()) {
                Order& m = lvl.orders.front();             // oldest order first
                int fill = min(rem, m.qty);
                m.qty -= fill; rem -= fill; lvl.qty -= fill;
                tradeCount++; volume += fill; lastPrice = lp;
                if (onTrade) onTrade(Trade{lp, fill, side, owner, m.owner});
                if (m.qty == 0) { index.erase(m.id); lvl.orders.pop_front(); }
            }
            if (lvl.orders.empty()) opp.erase(lvlIt);
        }
        return rem;
    }

    void rest(uint64_t id, Side side, int price, int qty, int owner) {
        Level& lvl = (side == Side::Buy) ? bids[price] : asks[price];
        lvl.orders.push_back(Order{id, side, price, qty, owner});
        lvl.qty += qty;
        index[id] = Loc{side, price, prev(lvl.orders.end())};
    }

    template <class M>
    void eraseFrom(M& m, const Loc& loc) {
        auto lit = m.find(loc.price);
        lit->second.qty -= loc.it->qty;
        lit->second.orders.erase(loc.it);
        if (lit->second.orders.empty()) m.erase(lit);
    }
};

// Random order flow: random-walk fair price, limits near fair, market orders, cancels.
struct Flow {
    OrderBook& b;
    mt19937_64 rng{42};
    uniform_real_distribution<double> U{0.0, 1.0};
    int fair = 10000;
    vector<uint64_t> live;

    explicit Flow(OrderBook& book) : b(book) {}

    void step() {
        if (U(rng) < 0.1) fair += (U(rng) < 0.5) ? -1 : 1;
        double r = U(rng);
        if (r < 0.3 && !live.empty()) {
            size_t k = (size_t)(U(rng) * live.size());
            uint64_t id = live[k];
            live[k] = live.back(); live.pop_back();
            b.cancel(id);
            return;
        }
        Side side = (U(rng) < 0.5) ? Side::Buy : Side::Sell;
        int qty = 1 + (int)(U(rng) * 20);
        if (r < 0.42) { b.add(side, 0, qty, MKT, true); return; }
        int off = (int)(-log(1.0 - U(rng)) * 3.0);
        int price = (side == Side::Buy) ? fair - off - 1 : fair + off + 1;
        uint64_t id = b.add(side, price, qty, FLOW);
        if (b.alive(id)) live.push_back(id);
    }
};

// Simple inventory-aware market maker.
struct MarketMaker {
    OrderBook& b;
    long long cash = 0;
    long long inv = 0;
    long long fills = 0;
    vector<uint64_t> ids;
    int halfSpread = 2, size = 10, maxInv = 200;
    double skew = 0.05;

    explicit MarketMaker(OrderBook& book) : b(book) {}

    void onTrade(const Trade& t) {
        bool isBuy;
        if (t.taker == MM) isBuy = (t.takerSide == Side::Buy);
        else if (t.maker == MM) isBuy = (t.takerSide == Side::Sell);
        else return;
        fills++;
        if (isBuy) { inv += t.qty; cash -= (long long)t.qty * t.price; }
        else       { inv -= t.qty; cash += (long long)t.qty * t.price; }
    }

    void requote(double mid) {
        for (uint64_t id : ids) b.cancel(id);
        ids.clear();
        int shift = (int)lround(inv * skew);   // long -> quote lower to sell; short -> quote higher to buy
        int m = (int)lround(mid);
        int bid = m - halfSpread - shift;
        int ask = m + halfSpread - shift;
        if (ask <= bid) ask = bid + 1;
        if (inv < maxInv)  ids.push_back(b.add(Side::Buy,  bid, size, MM));
        if (inv > -maxInv) ids.push_back(b.add(Side::Sell, ask, size, MM));
    }

    double pnl(double mid) const { return (cash + inv * mid) / 100.0; }
};

static double midOf(const OrderBook& b, const Flow& f) {
    int bb = b.bestBid(), ba = b.bestAsk();
    if (bb >= 0 && ba >= 0) return (bb + ba) / 2.0;
    return b.lastPrice >= 0 ? b.lastPrice : f.fair;
}

static void selfTest() {
    OrderBook b;
    vector<Trade> tr;
    b.onTrade = [&](const Trade& t) { tr.push_back(t); };
    b.add(Side::Sell, 101, 5, 10);
    b.add(Side::Sell, 101, 5, 11);
    uint64_t c = b.add(Side::Sell, 102, 5, 12);
    b.add(Side::Buy, 101, 7, 99);
    bool ok = tr.size() == 2 && tr[0].maker == 10 && tr[0].qty == 5 &&
              tr[1].maker == 11 && tr[1].qty == 2;                 // time priority + partial fill
    ok = ok && b.bestAsk() == 101 && b.asks[101].qty == 3;
    b.add(Side::Buy, 99, 4, 20);
    b.add(Side::Buy, 100, 4, 21);
    ok = ok && b.bestBid() == 100;                                  // price priority
    ok = ok && b.cancel(c) && !b.cancel(c);                         // cancel works once
    b.add(Side::Buy, 0, 100, 30, true);                             // market order sweeps the ask side
    ok = ok && b.asks.empty();
    printf("Self-test: %s\n\n", ok ? "PASSED" : "FAILED");
    if (!ok) exit(1);
}

int main() {
    selfTest();

    const int WARMUP = 20000;
    const int N = 1000000;

    // ---- Run 1: throughput (no per-message timing overhead) ----
    {
        OrderBook book; Flow flow(book); MarketMaker mm(book);
        book.onTrade = [&](const Trade& t) { mm.onTrade(t); };
        for (int i = 0; i < WARMUP; i++) flow.step();
        mm.requote(midOf(book, flow));

        long long msgs = 0;
        auto t0 = Clock::now();
        for (int i = 0; i < N; i++) {
            flow.step(); msgs++;
            if (i % 10 == 0) { mm.requote(midOf(book, flow)); msgs += 4; }  // 2 cancels + 2 adds
        }
        double sec = chrono::duration<double>(Clock::now() - t0).count();

        double mid = midOf(book, flow);
        printf("=== Throughput run ===\n");
        printf("Messages:        %lld (orders + cancels)\n", msgs);
        printf("Time:            %.1f ms\n", sec * 1000);
        printf("Throughput:      %.2f M msg/s\n", msgs / sec / 1e6);
        printf("Avg latency:     %.0f ns/msg\n", sec * 1e9 / msgs);
        printf("Trades:          %lld  (volume %lld)\n", book.tradeCount, book.volume);
        printf("Resting orders:  %zu\n", book.index.size());
        printf("Best bid/ask:    %.2f / %.2f\n", book.bestBid() / 100.0, book.bestAsk() / 100.0);
        printf("MM fills:        %lld\n", mm.fills);
        printf("MM inventory:    %lld\n", mm.inv);
        printf("MM PnL:          $%.2f\n\n", mm.pnl(mid));
    }

    // ---- Run 2: per-message latency distribution ----
    {
        OrderBook book; Flow flow(book);
        for (int i = 0; i < WARMUP; i++) flow.step();
        vector<uint32_t> lat; lat.reserve(N);
        for (int i = 0; i < N; i++) {
            auto a = Clock::now();
            flow.step();
            auto d = chrono::duration_cast<chrono::nanoseconds>(Clock::now() - a).count();
            lat.push_back((uint32_t)d);
        }
        sort(lat.begin(), lat.end());
        auto pct = [&](double p) { return lat[(size_t)(p * (lat.size() - 1))]; };
        printf("=== Latency distribution (per order/cancel, includes ~30ns timer overhead) ===\n");
        printf("p50:    %u ns\n", pct(0.50));
        printf("p90:    %u ns\n", pct(0.90));
        printf("p99:    %u ns\n", pct(0.99));
        printf("p99.9:  %u ns\n", pct(0.999));
        printf("max:    %u ns\n", lat.back());
    }
    return 0;
}
