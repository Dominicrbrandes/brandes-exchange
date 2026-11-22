// matching_engine.cpp
#include "exchange/matching_engine.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include <nlohmann/json.hpp>

#include "exchange/types.hpp"
#include "exchange/order_book.hpp"

namespace exchange {

MatchingEngine::MatchingEngine(const std::string& event_log_path,
                               const std::string& snapshot_path,
                               uint64_t snapshot_interval,
                               Durability durability)
    : event_log_(event_log_path, durability),
      snapshot_manager_(snapshot_path, snapshot_interval) {}

namespace {

Side opposite(Side s) { return s == Side::BUY ? Side::SELL : Side::BUY; }

// Whether an incoming order is willing to trade at a resting price level.
bool price_acceptable(const Order& incoming, int64_t level_price) {
    if (incoming.type == OrderType::MARKET) return true;
    return incoming.side == Side::BUY ? level_price <= incoming.price
                                      : level_price >= incoming.price;
}

std::optional<int64_t> best_price(const OrderBook& book, Side side) {
    return side == Side::BUY ? book.best_bid_price() : book.best_ask_price();
}

}  // namespace

PlaceOrderResult MatchingEngine::place_order(Order order) {
    PlaceOrderResult r;

    // idempotency check
    if (!order.idempotency_key.empty() &&
        idempotency_keys_.count(order.idempotency_key)) {
        r.success = false;
        r.error_code = ErrorCode::DUPLICATE_IDEMPOTENCY_KEY;
        stats_.total_rejects++;
        return r;
    }

    // Market orders never rest, so anything but fill-or-kill is IOC.
    if (order.type == OrderType::MARKET && order.time_in_force == TimeInForce::GTC) {
        order.time_in_force = TimeInForce::IOC;
    }

    // risk check
    auto risk = risk_checker_.check_order(order);
    if (!risk.passed) {
        r.success = false;
        r.error_code = risk.error_code;
        stats_.total_rejects++;
        return r;
    }

    // assign id, timestamp, remaining
    order.id = next_order_id_++;
    order.timestamp_ns = now_ns();
    order.remaining_qty = order.quantity;
    order.status = OrderStatus::NEW;

    if (!order.idempotency_key.empty()) {
        idempotency_keys_.insert(order.idempotency_key);
    }

    // store order
    auto ptr = std::make_unique<Order>(order);
    Order* raw = ptr.get();
    orders_[order.id] = std::move(ptr);

    // log placed event
    log_event(EventType::ORDER_PLACED, *raw);
    stats_.total_orders++;

    // Fill or kill: check the whole quantity is available before touching the
    // book, so a killed order leaves no trades behind.
    if (raw->time_in_force == TimeInForce::FOK &&
        fillable_quantity(*raw, raw->quantity) < raw->quantity) {
        expire(raw, "FOK");
        r.success = true;
        r.order = *raw;
        return r;
    }

    r.trades = match(raw);

    if (raw->remaining_qty == 0) {
        raw->status = OrderStatus::FILLED;
    } else if (raw->type == OrderType::MARKET && raw->remaining_qty == raw->quantity) {
        // A market order that found nothing to trade against is rejected.
        raw->status = OrderStatus::REJECTED;
        log_event(EventType::ORDER_REJECTED,
                  nlohmann::json{{"order_id", raw->id}, {"reason", "NO_LIQUIDITY"}});
        r.success = false;
        r.error_code = ErrorCode::NO_LIQUIDITY;
        stats_.total_rejects++;
        r.order = *raw;
        return r;
    } else if (raw->time_in_force != TimeInForce::GTC) {
        // IOC, which includes every market order: whatever did not fill now
        // is cancelled rather than left resting.
        expire(raw, raw->type == OrderType::MARKET ? "MARKET_REMAINDER" : "IOC");
    } else {
        // Anything still unfilled here is either priced away from the book or
        // blocked only by this account's own resting orders, which match()
        // stepped over. Both are legitimate reasons to rest, so the order is
        // never rejected at this point.
        get_or_create_book(raw->symbol).add_order(raw);
        if (raw->remaining_qty < raw->quantity) {
            raw->status = OrderStatus::PARTIAL;
        }
    }

    r.success = true;
    r.order = *raw;
    return r;
}

std::vector<Trade> MatchingEngine::match(Order* incoming) {
    std::vector<Trade> trades;
    auto& book = get_or_create_book(incoming->symbol);
    const Side opp = opposite(incoming->side);

    // Walk price levels best-first, and each level's queue front to back.
    std::optional<int64_t> level_price = best_price(book, opp);
    while (incoming->remaining_qty > 0 && level_price &&
           price_acceptable(*incoming, *level_price)) {
        const int64_t price = *level_price;

        for (const OrderBook::Node* node = book.level_front(opp, price);
             node && incoming->remaining_qty > 0;) {
            Order* best = node->order;
            // Read the successor first: filling `best` completely removes its
            // node from the queue.
            node = node->next;

            // Self-trade prevention: step over our own resting orders and keep
            // matching deeper in the book.
            if (best->account_id == incoming->account_id) continue;

            const int64_t qty = std::min(incoming->remaining_qty, best->remaining_qty);

            Trade t;
            t.id = next_trade_id_++;
            t.symbol = incoming->symbol;
            t.price = best->price;  // Trade at resting order's price
            t.quantity = qty;
            t.timestamp_ns = now_ns();

            if (incoming->side == Side::BUY) {
                t.buy_order_id = incoming->id;
                t.sell_order_id = best->id;
                t.buyer_account_id = incoming->account_id;
                t.seller_account_id = best->account_id;
            } else {
                t.buy_order_id = best->id;
                t.sell_order_id = incoming->id;
                t.buyer_account_id = best->account_id;
                t.seller_account_id = incoming->account_id;
            }

            trades.push_back(t);
            trades_.push_back(t);
            log_event(EventType::TRADE_EXECUTED, t);
            stats_.total_trades++;

            incoming->remaining_qty -= qty;
            book.update_order_qty(best->id, best->remaining_qty - qty);
        }

        // The level may be gone now; upper_bound still finds the next one.
        level_price = book.next_price_after(opp, price);
    }

    return trades;
}

int64_t MatchingEngine::fillable_quantity(const Order& incoming, int64_t needed) const {
    auto it = books_.find(incoming.symbol);
    if (it == books_.end()) return 0;
    const OrderBook& book = *it->second;
    const Side opp = opposite(incoming.side);

    int64_t available = 0;
    for (auto price = best_price(book, opp);
         price && available < needed && price_acceptable(incoming, *price);
         price = book.next_price_after(opp, *price)) {
        for (const OrderBook::Node* node = book.level_front(opp, *price);
             node && available < needed; node = node->next) {
            if (node->order->account_id != incoming.account_id) {
                available += node->order->remaining_qty;
            }
        }
    }
    return available;
}

void MatchingEngine::expire(Order* order, const char* reason) {
    order->status = OrderStatus::CANCELLED;
    log_event(EventType::ORDER_CANCELLED,
              nlohmann::json{{"order_id", order->id}, {"reason", reason}});
    stats_.total_expired++;
}

CancelOrderResult MatchingEngine::cancel_order(uint64_t order_id) {
    CancelOrderResult res{};

    auto it = orders_.find(order_id);
    if (it == orders_.end() || !it->second) {
        res.success = false;
        res.error_code = ErrorCode::ORDER_NOT_FOUND;
        return res;
    }

    Order& ord = *it->second;

    // Only live orders (NEW or PARTIAL) can be cancelled.
    if (!ord.is_active()) {
        res.success = false;
        res.error_code = ErrorCode::ORDER_NOT_FOUND;
        res.order = ord;
        return res;
    }

    get_or_create_book(ord.symbol).remove_order(order_id);
    ord.status = OrderStatus::CANCELLED;

    log_event(EventType::ORDER_CANCELLED,
              nlohmann::json{{"order_id", order_id}, {"reason", "USER"}});
    stats_.total_cancels++;

    res.success = true;
    res.error_code = ErrorCode::NONE;
    res.order = ord;
    return res;
}

ReplaceOrderResult MatchingEngine::replace_order(uint64_t order_id, int64_t new_price,
                                                 int64_t new_quantity) {
    ReplaceOrderResult r;

    auto it = orders_.find(order_id);
    if (it == orders_.end() || !it->second || !it->second->is_active()) {
        r.error_code = ErrorCode::ORDER_NOT_FOUND;
        if (it != orders_.end() && it->second) r.order = *it->second;
        return r;
    }
    Order& o = *it->second;
    r.order = o;

    // The new quantity is the new total, so it has to leave something unfilled.
    const int64_t filled = o.filled_qty();
    if (new_quantity <= filled) {
        r.error_code = ErrorCode::INVALID_QUANTITY;
        return r;
    }

    Order proposed = o;
    proposed.price = new_price;
    proposed.quantity = new_quantity;
    auto risk = risk_checker_.check_order(proposed);
    if (!risk.passed) {
        r.error_code = risk.error_code;
        return r;
    }

    OrderBook& book = get_or_create_book(o.symbol);
    const int64_t new_remaining = new_quantity - filled;
    const bool keeps_priority = new_price == o.price && new_quantity <= o.quantity;

    if (keeps_priority) {
        book.resize_order(o.id, new_remaining);
        o.quantity = new_quantity;
    } else {
        book.remove_order(o.id);
        o.price = new_price;
        o.quantity = new_quantity;
        o.remaining_qty = new_remaining;
        o.timestamp_ns = now_ns();
    }

    log_event(EventType::ORDER_REPLACED,
              nlohmann::json{{"order_id", o.id},
                             {"price", o.price},
                             {"quantity", o.quantity},
                             {"remaining_qty", o.remaining_qty},
                             {"timestamp_ns", o.timestamp_ns},
                             {"keeps_priority", keeps_priority}});
    stats_.total_replaces++;

    if (!keeps_priority) {
        r.trades = match(&o);
        if (o.remaining_qty > 0) book.add_order(&o);
    }

    if (o.remaining_qty == 0) {
        o.status = OrderStatus::FILLED;
    } else {
        o.status = o.remaining_qty < o.quantity ? OrderStatus::PARTIAL : OrderStatus::NEW;
    }

    r.success = true;
    r.order = o;
    return r;
}

OrderBook& MatchingEngine::get_or_create_book(const std::string& symbol) {
    auto it = books_.find(symbol);
    if (it == books_.end()) {
        books_[symbol] = std::make_unique<OrderBook>(symbol);
    }
    return *books_[symbol];
}

OrderBook* MatchingEngine::get_book(const std::string& symbol) {
    auto it = books_.find(symbol);
    return it == books_.end() ? nullptr : it->second.get();
}

std::optional<Order> MatchingEngine::get_order(uint64_t order_id) const {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) return std::nullopt;
    if (!it->second) return std::nullopt;
    return *it->second;
}

std::vector<Trade> MatchingEngine::get_trades(const std::string& symbol, size_t limit) const {
    std::vector<Trade> out;
    if (limit == 0) return out;

    // Iterate in reverse to get most recent trades first
    for (auto it = trades_.rbegin(); it != trades_.rend() && out.size() < limit; ++it) {
        if (it->symbol == symbol) {
            out.push_back(*it);
        }
    }

    // Reverse to return in chronological order (oldest first)
    std::reverse(out.begin(), out.end());
    return out;
}

EngineStats MatchingEngine::get_stats() const {
    EngineStats s = stats_;
    s.event_sequence = event_log_.current_sequence();
    return s;
}

bool MatchingEngine::recover() {
    // First, try to load from snapshot
    auto snap = snapshot_manager_.load_latest();
    
    if (snap) {
        // Restore from snapshot
        orders_.clear();
        books_.clear();
        idempotency_keys_.clear();

        for (const auto& o : snap->orders) {
            auto ptr = std::make_unique<Order>(o);
            Order* raw = ptr.get();
            
            // Only add active orders to the book
            if (o.status == OrderStatus::NEW || o.status == OrderStatus::PARTIAL) {
                if (o.remaining_qty > 0 && o.type == OrderType::LIMIT) {
                    get_or_create_book(o.symbol).add_order(raw);
                }
            }
            
            // Track idempotency keys
            if (!o.idempotency_key.empty()) {
                idempotency_keys_.insert(o.idempotency_key);
            }
            
            orders_[o.id] = std::move(ptr);
        }

        for (const auto& key : snap->idempotency_keys) idempotency_keys_.insert(key);

        next_order_id_ = snap->next_order_id;
        next_trade_id_ = snap->next_trade_id;
        
        // Now replay any events after the snapshot
        auto events = event_log_.read_from(snap->sequence + 1);
        replay_events(events);
        
        return true;
    }
    
    // No snapshot - try to replay from event log only
    auto events = event_log_.read_all();
    if (!events.empty()) {
        replay_events(events);
        return true;
    }
    
    // Nothing to recover from
    return false;
}

void MatchingEngine::replay_events(const std::vector<Event>& events) {
    for (const auto& event : events) {
        switch (event.type) {
            case EventType::ORDER_PLACED: {
                Order order = order_from_persisted(event.payload);
                
                // Check if order already exists (from snapshot)
                if (orders_.find(order.id) != orders_.end()) {
                    continue;  // Skip, already loaded from snapshot
                }
                
                // Store the order
                auto ptr = std::make_unique<Order>(order);
                Order* raw = ptr.get();
                
                // Add to book if active limit order with remaining qty
                if ((order.status == OrderStatus::NEW || order.status == OrderStatus::PARTIAL) &&
                    order.type == OrderType::LIMIT && order.remaining_qty > 0) {
                    get_or_create_book(order.symbol).add_order(raw);
                }
                
                if (!order.idempotency_key.empty()) {
                    idempotency_keys_.insert(order.idempotency_key);
                }
                
                orders_[order.id] = std::move(ptr);
                
                if (order.id >= next_order_id_) {
                    next_order_id_ = order.id + 1;
                }
                break;
            }
            
            case EventType::ORDER_CANCELLED:
            case EventType::ORDER_REJECTED: {
                uint64_t order_id = event.payload.value("order_id", 0ULL);
                auto it = orders_.find(order_id);
                if (it != orders_.end() && it->second) {
                    it->second->status = event.type == EventType::ORDER_CANCELLED
                                             ? OrderStatus::CANCELLED
                                             : OrderStatus::REJECTED;
                    auto* book = get_book(it->second->symbol);
                    if (book) {
                        book->remove_order(order_id);
                    }
                }
                break;
            }

            case EventType::ORDER_REPLACED: {
                const auto& p = event.payload;
                auto it = orders_.find(p.value("order_id", 0ULL));
                if (it == orders_.end() || !it->second) break;
                Order& o = *it->second;
                OrderBook& book = get_or_create_book(o.symbol);
                const int64_t remaining = p.at("remaining_qty").get<int64_t>();

                if (p.value("keeps_priority", false)) {
                    book.resize_order(o.id, remaining);
                } else {
                    // Same as live: leave the queue and rejoin at the back of
                    // the new level. Any trades it made follow as their own
                    // TRADE_EXECUTED events.
                    book.remove_order(o.id);
                    o.price = p.at("price").get<int64_t>();
                    o.remaining_qty = remaining;
                    o.timestamp_ns = p.at("timestamp_ns").get<uint64_t>();
                    book.add_order(&o);
                }
                o.quantity = p.at("quantity").get<int64_t>();
                o.remaining_qty = remaining;
                o.status = remaining < o.quantity ? OrderStatus::PARTIAL : OrderStatus::NEW;
                break;
            }

            case EventType::TRADE_EXECUTED: {
                Trade trade = event.payload.get<Trade>();
                trades_.push_back(trade);

                if (trade.id >= next_trade_id_) {
                    next_trade_id_ = trade.id + 1;
                }

                apply_fill(trade.buy_order_id, trade.quantity);
                apply_fill(trade.sell_order_id, trade.quantity);
                break;
            }

            case EventType::SNAPSHOT_MARKER:
                break;
        }
    }
}

void MatchingEngine::apply_fill(uint64_t order_id, int64_t qty) {
    auto it = orders_.find(order_id);
    if (it == orders_.end() || !it->second) return;
    Order& o = *it->second;
    const int64_t remaining = std::max<int64_t>(o.remaining_qty - qty, 0);

    OrderBook* book = get_book(o.symbol);
    if (book && book->get_order(order_id)) {
        book->update_order_qty(order_id, remaining);
        return;
    }
    o.remaining_qty = remaining;
    o.status = remaining == 0 ? OrderStatus::FILLED : OrderStatus::PARTIAL;
}

Snapshot MatchingEngine::create_snapshot() const {
    Snapshot s;
    s.sequence = event_log_.current_sequence();
    s.timestamp_ns = now_ns();
    s.next_order_id = next_order_id_;
    s.next_trade_id = next_trade_id_;

    // Every live order is resting, so the books hold all of them. Write them
    // in queue order: recovery re-adds orders in the order they appear here,
    // and walking orders_ (a hash map) used to scramble time priority within
    // each price level.
    for (const auto& [symbol, book] : books_) {
        for (const auto* o : book->get_all_bids()) s.orders.push_back(*o);
        for (const auto* o : book->get_all_asks()) s.orders.push_back(*o);
    }

    s.idempotency_keys.assign(idempotency_keys_.begin(), idempotency_keys_.end());
    return s;
}

}  // namespace exchange