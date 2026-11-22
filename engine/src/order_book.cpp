#include "exchange/order_book.hpp"

#include <cassert>

namespace exchange {

OrderBook::OrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

OrderBook::Node* OrderBook::alloc_node() {
    if (!free_nodes_.empty()) {
        Node* n = free_nodes_.back();
        free_nodes_.pop_back();
        return n;
    }
    return &node_storage_.emplace_back();
}

void OrderBook::add_order(Order* order) {
    assert(index_.find(order->id) == index_.end());

    Level* level = nullptr;
    if (order->side == Side::BUY) {
        level = &bids_[order->price];
        ++bid_orders_;
    } else {
        level = &asks_[order->price];
        ++ask_orders_;
    }
    level->price = order->price;

    Node* node = alloc_node();
    *node = Node{order, level->tail, nullptr, level};
    if (level->tail) {
        level->tail->next = node;
    } else {
        level->head = node;
    }
    level->tail = node;
    level->total_qty += order->remaining_qty;
    ++level->order_count;

    index_[order->id] = node;
}

void OrderBook::unlink(Node* node) {
    Level* level = node->level;
    const Order* order = node->order;

    if (node->prev) {
        node->prev->next = node->next;
    } else {
        level->head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        level->tail = node->prev;
    }
    level->total_qty -= order->remaining_qty;
    --level->order_count;

    const int64_t price = order->price;
    const bool empty = level->head == nullptr;
    if (order->side == Side::BUY) {
        --bid_orders_;
        if (empty) bids_.erase(price);
    } else {
        --ask_orders_;
        if (empty) asks_.erase(price);
    }

    *node = Node{};
    free_nodes_.push_back(node);
}

bool OrderBook::remove_order(uint64_t order_id) {
    auto it = index_.find(order_id);
    if (it == index_.end()) return false;
    Node* node = it->second;
    index_.erase(it);
    unlink(node);
    return true;
}

void OrderBook::update_order_qty(uint64_t order_id, int64_t new_remaining_qty) {
    auto it = index_.find(order_id);
    if (it == index_.end()) return;
    Order* order = it->second->order;

    if (new_remaining_qty == 0) {
        // unlink subtracts the order's remaining quantity from the level total,
        // so it must still hold the pre-fill value at this point.
        remove_order(order_id);
        order->remaining_qty = 0;
        order->status = OrderStatus::FILLED;
        return;
    }

    it->second->level->total_qty += new_remaining_qty - order->remaining_qty;
    order->remaining_qty = new_remaining_qty;
    order->status = OrderStatus::PARTIAL;
}

void OrderBook::resize_order(uint64_t order_id, int64_t new_remaining_qty) {
    assert(new_remaining_qty > 0);
    auto it = index_.find(order_id);
    if (it == index_.end()) return;
    Order* order = it->second->order;
    it->second->level->total_qty += new_remaining_qty - order->remaining_qty;
    order->remaining_qty = new_remaining_qty;
}

std::optional<int64_t> OrderBook::best_bid_price() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<int64_t> OrderBook::best_ask_price() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

namespace {

std::vector<Order*> queue_of(const OrderBook::Level& level) {
    std::vector<Order*> out;
    out.reserve(static_cast<size_t>(level.order_count));
    for (const OrderBook::Node* n = level.head; n; n = n->next) out.push_back(n->order);
    return out;
}

}  // namespace

std::vector<Order*> OrderBook::get_bids_at_best() const {
    if (bids_.empty()) return {};
    return queue_of(bids_.begin()->second);
}

std::vector<Order*> OrderBook::get_asks_at_best() const {
    if (asks_.empty()) return {};
    return queue_of(asks_.begin()->second);
}

template <typename Map>
void OrderBook::append_orders(const Map& side, std::vector<Order*>& out) {
    for (const auto& [price, level] : side) {
        for (const Node* n = level.head; n; n = n->next) out.push_back(n->order);
    }
}

std::vector<Order*> OrderBook::get_all_bids() const {
    std::vector<Order*> out;
    out.reserve(bid_orders_);
    append_orders(bids_, out);
    return out;
}

std::vector<Order*> OrderBook::get_all_asks() const {
    std::vector<Order*> out;
    out.reserve(ask_orders_);
    append_orders(asks_, out);
    return out;
}

template <typename Map>
std::vector<BookLevel> OrderBook::levels_of(const Map& side, size_t depth) {
    std::vector<BookLevel> out;
    for (const auto& [price, level] : side) {
        if (out.size() >= depth) break;
        out.push_back(BookLevel{price, level.total_qty, level.order_count});
    }
    return out;
}

std::vector<BookLevel> OrderBook::get_bid_levels(size_t depth) const {
    return levels_of(bids_, depth);
}

std::vector<BookLevel> OrderBook::get_ask_levels(size_t depth) const {
    return levels_of(asks_, depth);
}

const OrderBook::Node* OrderBook::level_front(Side side, int64_t price) const {
    if (side == Side::BUY) {
        auto it = bids_.find(price);
        return it == bids_.end() ? nullptr : it->second.head;
    }
    auto it = asks_.find(price);
    return it == asks_.end() ? nullptr : it->second.head;
}

std::optional<int64_t> OrderBook::next_price_after(Side side, int64_t price) const {
    // Both maps are ordered best-first, so "next" is simply the following key.
    if (side == Side::BUY) {
        auto it = bids_.upper_bound(price);
        if (it == bids_.end()) return std::nullopt;
        return it->first;
    }
    auto it = asks_.upper_bound(price);
    if (it == asks_.end()) return std::nullopt;
    return it->first;
}

bool OrderBook::is_crossed() const {
    auto bid = best_bid_price();
    auto ask = best_ask_price();
    if (!bid || !ask) return false;
    return *bid >= *ask;
}

Order* OrderBook::get_order(uint64_t order_id) const {
    auto it = index_.find(order_id);
    return it == index_.end() ? nullptr : it->second->order;
}

}  // namespace exchange
