#pragma once

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "exchange/types.hpp"

namespace exchange {

// Order book for a single symbol.
//
// Each price level is a doubly linked FIFO queue of resting orders, and an
// index maps every order id straight to its queue node. That makes the three
// hot operations independent of how deep the queue is:
//   - add at the back of a level         O(1), plus O(log L) to create a new level
//   - remove from anywhere (cancel/fill)  O(1), plus O(log L) to drop an empty level
//   - change quantity in place            O(1), keeps time priority
// where L is the number of price levels on that side. Every level also keeps a
// running total of its resting quantity, so depth snapshots cost O(depth)
// instead of walking every order.
//
// The book never owns orders. It holds pointers into storage owned by the
// matching engine, and nodes are recycled through a free list so steady-state
// trading does not allocate.
class OrderBook {
   public:
    struct Level;

    // One resting order in a price level's queue. Exposed read-only so the
    // matching loop can walk a level front to back without copying it.
    struct Node {
        Order* order = nullptr;
        Node* prev = nullptr;
        Node* next = nullptr;
        Level* level = nullptr;
    };

    struct Level {
        int64_t price = 0;
        Node* head = nullptr;
        Node* tail = nullptr;
        int64_t total_qty = 0;
        int order_count = 0;
    };

    explicit OrderBook(std::string symbol);

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = default;
    OrderBook& operator=(OrderBook&&) = default;

    // Appends the order to the back of its price level. The order must not
    // already be in the book.
    void add_order(Order* order);

    // Removes the order wherever it sits in its level. Returns false if the
    // order is not resting in this book.
    bool remove_order(uint64_t order_id);

    // Sets the order's remaining quantity without moving it in its queue.
    // Zero removes the order and marks it FILLED; anything else marks it
    // PARTIAL. Used by matching, where any reduction is a fill.
    void update_order_qty(uint64_t order_id, int64_t new_remaining_qty);

    // Sets the remaining quantity in place without touching the status. Used
    // by cancel/replace, where a smaller order keeps its queue position. The
    // new quantity must be positive.
    void resize_order(uint64_t order_id, int64_t new_remaining_qty);

    [[nodiscard]] std::optional<int64_t> best_bid_price() const;
    [[nodiscard]] std::optional<int64_t> best_ask_price() const;

    [[nodiscard]] std::vector<Order*> get_bids_at_best() const;
    [[nodiscard]] std::vector<Order*> get_asks_at_best() const;

    [[nodiscard]] std::vector<Order*> get_all_bids() const;
    [[nodiscard]] std::vector<Order*> get_all_asks() const;

    [[nodiscard]] std::vector<BookLevel> get_bid_levels(size_t depth = 10) const;
    [[nodiscard]] std::vector<BookLevel> get_ask_levels(size_t depth = 10) const;

    [[nodiscard]] bool is_crossed() const;
    [[nodiscard]] Order* get_order(uint64_t order_id) const;

    // Matching-path accessors. level_front returns the first node of the queue
    // at that price, or nullptr if there is no such level. A node stays valid
    // until its own order is removed, so callers that may remove the current
    // order must read node->next first.
    [[nodiscard]] const Node* level_front(Side side, int64_t price) const;
    [[nodiscard]] std::optional<int64_t> next_price_after(Side side, int64_t price) const;

    [[nodiscard]] const std::string& symbol() const { return symbol_; }
    [[nodiscard]] size_t bid_count() const { return bid_orders_; }
    [[nodiscard]] size_t ask_count() const { return ask_orders_; }

   private:
    std::string symbol_;

    // Both sides are ordered best price first.
    std::map<int64_t, Level, std::greater<>> bids_;
    std::map<int64_t, Level, std::less<>> asks_;

    std::unordered_map<uint64_t, Node*> index_;
    size_t bid_orders_ = 0;
    size_t ask_orders_ = 0;

    // Node storage. A deque never moves existing elements on push_back, so
    // node addresses stay stable; freed nodes are reused before growing.
    std::deque<Node> node_storage_;
    std::vector<Node*> free_nodes_;

    Node* alloc_node();
    void unlink(Node* node);

    template <typename Map>
    static std::vector<BookLevel> levels_of(const Map& side, size_t depth);
    template <typename Map>
    static void append_orders(const Map& side, std::vector<Order*>& out);
};

}  // namespace exchange
