#pragma once

#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "exchange/event_log.hpp"
#include "exchange/order_book.hpp"
#include "exchange/risk_checks.hpp"
#include "exchange/snapshot.hpp"
#include "exchange/types.hpp"

namespace exchange {

struct PlaceOrderResult {
    bool success = false;
    ErrorCode error_code = ErrorCode::NONE;
    Order order;
    std::vector<Trade> trades;
};

struct ReplaceOrderResult {
    bool success = false;
    ErrorCode error_code = ErrorCode::NONE;
    Order order;
    std::vector<Trade> trades;  // non-empty when the new price crossed the book
};

struct CancelOrderResult {
    bool success = false;
    ErrorCode error_code = ErrorCode::ORDER_NOT_FOUND;
    Order order{};
};

struct EngineStats {
    uint64_t total_orders = 0;
    uint64_t total_trades = 0;
    uint64_t total_cancels = 0;
    uint64_t total_rejects = 0;
    uint64_t total_replaces = 0;
    uint64_t total_expired = 0;  // IOC, FOK and market remainders cancelled by the engine
    uint64_t event_sequence = 0;
};

// JSON serialization for EngineStats
inline void to_json(nlohmann::json& j, const EngineStats& s) {
    j = nlohmann::json{
        {"total_orders", s.total_orders},
        {"total_trades", s.total_trades},
        {"total_cancels", s.total_cancels},
        {"total_rejects", s.total_rejects},
        {"total_replaces", s.total_replaces},
        {"total_expired", s.total_expired},
        {"event_sequence", s.event_sequence}
    };
}

class MatchingEngine {
public:
    MatchingEngine(const std::string& event_log_path = "",
                   const std::string& snapshot_path = "",
                   uint64_t snapshot_interval = 1000,
                   Durability durability = Durability::PerEvent);

    PlaceOrderResult place_order(Order order);
    CancelOrderResult cancel_order(uint64_t order_id);

    // Cancel/replace for a resting order: sets a new price and a new total
    // quantity (filled + remaining) under the same order id. The order keeps
    // its place in the queue only when the price is unchanged and the quantity
    // does not grow; otherwise it moves to the back of its new price level, and
    // if the new price crosses the book it trades first, like a new order.
    ReplaceOrderResult replace_order(uint64_t order_id, int64_t new_price, int64_t new_quantity);
    bool recover();
    
    // Snapshot support
    Snapshot create_snapshot() const;
    void replay_events(const std::vector<Event>& events);

    [[nodiscard]] OrderBook* get_book(const std::string& symbol);
    [[nodiscard]] std::optional<Order> get_order(uint64_t order_id) const;
    [[nodiscard]] std::vector<Trade> get_trades(const std::string& symbol, size_t limit) const;
    [[nodiscard]] EngineStats get_stats() const;

private:
    std::unordered_map<std::string, std::unique_ptr<OrderBook>> books_;
    std::unordered_map<uint64_t, std::unique_ptr<Order>> orders_;
    // A deque rather than a vector: this holds every trade for the life of the
    // process, and vector reallocation was copying the whole history at
    // unpredictable moments, producing multi-millisecond latency spikes.
    std::deque<Trade> trades_;
    std::unordered_set<std::string> idempotency_keys_;

    uint64_t next_order_id_ = 1;
    uint64_t next_trade_id_ = 1;

    EventLog event_log_;
    SnapshotManager snapshot_manager_;
    RiskChecker risk_checker_;
    
    // Statistics tracking
    EngineStats stats_;

    std::vector<Trade> match(Order* incoming);
    OrderBook& get_or_create_book(const std::string& symbol);

    // How much of `needed` the incoming order could fill right now, walking the
    // book exactly the way match() would (price limit and self-trade
    // prevention included). Stops counting once `needed` is reached.
    [[nodiscard]] int64_t fillable_quantity(const Order& incoming, int64_t needed) const;

    // Cancels an order's unfilled remainder on the engine's own initiative
    // (IOC, FOK, market remainder) and logs it so replay reaches the same state.
    void expire(Order* order, const char* reason);

    // Replay helper: applies a fill to one side of a logged trade, going
    // through the book when the order is resting so level totals stay right.
    void apply_fill(uint64_t order_id, int64_t qty);

    // Templated so the payload is only converted to JSON when the log is
    // actually enabled. Taking nlohmann::json by reference meant every caller
    // paid a full serialization at the call site, plus a second copy here,
    // even when the result was immediately discarded.
    template <typename T>
    void log_event(EventType type, const T& payload_source) {
        // The sequence still advances when logging is off, so snapshots and
        // recovery agree on ordering regardless of configuration.
        const uint64_t seq = event_log_.next_sequence();
        if (!event_log_.enabled()) return;

        Event e;
        e.sequence = seq;
        e.timestamp_ns = now_ns();
        e.type = type;
        e.payload = payload_source;
        event_log_.append(e);
    }

    // Overloads for the two event types that dominate the log, rendered
    // directly into a reusable buffer instead of through a JSON DOM.
    void log_event(EventType type, const Order& o) {
        const uint64_t seq = event_log_.next_sequence();
        if (!event_log_.enabled()) return;
        log_buf_.clear();
        append_json(log_buf_, o);
        event_log_.append_preformatted(seq, now_ns(), type, log_buf_);
    }

    void log_event(EventType type, const Trade& t) {
        const uint64_t seq = event_log_.next_sequence();
        if (!event_log_.enabled()) return;
        log_buf_.clear();
        append_json(log_buf_, t);
        event_log_.append_preformatted(seq, now_ns(), type, log_buf_);
    }

    // Reused across events so the hot path does not allocate.
    std::string log_buf_;
};

}  // namespace exchange