#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace exchange {

// Fixed-point scale: 1e8 units = 1.0
constexpr int64_t PRICE_SCALE = 100000000;

enum class Side { BUY, SELL };
enum class OrderType { LIMIT, MARKET };

// How long an order may wait for liquidity.
//   GTC  good till cancelled: any unfilled remainder rests on the book
//   IOC  immediate or cancel: fill what is available now, cancel the rest
//   FOK  fill or kill: fill the whole quantity now or do nothing at all
// Market orders never rest, so a market GTC is treated as IOC.
enum class TimeInForce { GTC, IOC, FOK };
enum class OrderStatus { NEW, PARTIAL, FILLED, CANCELLED, REJECTED };

NLOHMANN_JSON_SERIALIZE_ENUM(Side, {
    {Side::BUY, "BUY"}, 
    {Side::SELL, "SELL"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderType, {
    {OrderType::LIMIT, "LIMIT"}, 
    {OrderType::MARKET, "MARKET"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(TimeInForce, {
    {TimeInForce::GTC, "GTC"},
    {TimeInForce::IOC, "IOC"},
    {TimeInForce::FOK, "FOK"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderStatus, {
    {OrderStatus::NEW, "NEW"},
    {OrderStatus::PARTIAL, "PARTIAL"},
    {OrderStatus::FILLED, "FILLED"},
    {OrderStatus::CANCELLED, "CANCELLED"},
    {OrderStatus::REJECTED, "REJECTED"}
})

struct Order {
    uint64_t id = 0;
    std::string account_id;
    std::string symbol;
    Side side = Side::BUY;
    OrderType type = OrderType::LIMIT;
    TimeInForce time_in_force = TimeInForce::GTC;
    int64_t price = 0;           // In fixed-point units
    int64_t quantity = 0;        // Original quantity
    int64_t remaining_qty = 0;   // Unfilled quantity
    uint64_t timestamp_ns = 0;
    OrderStatus status = OrderStatus::NEW;
    std::string idempotency_key;
    std::string client_order_id;

    [[nodiscard]] bool is_active() const {
        return status == OrderStatus::NEW || status == OrderStatus::PARTIAL;
    }

    [[nodiscard]] int64_t filled_qty() const { 
        return quantity - remaining_qty; 
    }
};

void to_json(nlohmann::json& j, const Order& o);

// Parses an untrusted client request. Deliberately ignores id, status and
// timestamp_ns, and forces remaining_qty = quantity, so a client cannot assign
// its own order id or claim a fill. Do not use this to restore persisted state.
void from_json(const nlohmann::json& j, Order& o);

// Full-fidelity restore for trusted state we wrote ourselves (event log and
// snapshots). Round-trips every field that to_json emits. Replay must go
// through this rather than from_json, or fills are silently resurrected.
[[nodiscard]] Order order_from_persisted(const nlohmann::json& j);

struct Trade {
    uint64_t id = 0;
    uint64_t buy_order_id = 0;
    uint64_t sell_order_id = 0;
    std::string symbol;
    int64_t price = 0;
    int64_t quantity = 0;
    uint64_t timestamp_ns = 0;
    std::string buyer_account_id;
    std::string seller_account_id;
};

void to_json(nlohmann::json& j, const Trade& t);
void from_json(const nlohmann::json& j, Trade& t);

struct Account {
    std::string id;
    std::unordered_map<std::string, int64_t> balances;
};

void to_json(nlohmann::json& j, const Account& a);
void from_json(const nlohmann::json& j, Account& a);

enum class EventType {
    ORDER_PLACED,
    ORDER_CANCELLED,
    ORDER_REJECTED,
    TRADE_EXECUTED,
    SNAPSHOT_MARKER,
    ORDER_REPLACED
};

NLOHMANN_JSON_SERIALIZE_ENUM(EventType, {
    {EventType::ORDER_PLACED, "ORDER_PLACED"},
    {EventType::ORDER_CANCELLED, "ORDER_CANCELLED"},
    {EventType::ORDER_REJECTED, "ORDER_REJECTED"},
    {EventType::TRADE_EXECUTED, "TRADE_EXECUTED"},
    {EventType::SNAPSHOT_MARKER, "SNAPSHOT_MARKER"},
    {EventType::ORDER_REPLACED, "ORDER_REPLACED"}
})

struct Event {
    uint64_t sequence = 0;
    uint64_t timestamp_ns = 0;
    EventType type = EventType::ORDER_PLACED;
    nlohmann::json payload;
};

void to_json(nlohmann::json& j, const Event& e);
void from_json(const nlohmann::json& j, Event& e);

// Direct serializers for the logging hot path. Building an nlohmann DOM for
// every order and trade, nesting it inside an Event DOM and then dumping it
// cost several microseconds per event and dominated the durable write path.
// These append the identical wire format straight into a reusable buffer.
// They must stay byte-compatible with the to_json overloads above, which
// test_event_format.cpp checks.
void append_json(std::string& out, const Order& o);
void append_json(std::string& out, const Trade& t);

[[nodiscard]] const char* to_string(Side s);
[[nodiscard]] const char* to_string(OrderType t);
[[nodiscard]] const char* to_string(TimeInForce t);
[[nodiscard]] const char* to_string(OrderStatus s);
[[nodiscard]] const char* to_string(EventType t);

// Error codes for API responses
enum class ErrorCode {
    NONE,
    INVALID_QUANTITY,
    INVALID_PRICE,
    INVALID_SYMBOL,
    INVALID_SIDE,
    INVALID_ORDER_TYPE,
    ORDER_NOT_FOUND,
    INSUFFICIENT_BALANCE,
    MAX_ORDER_SIZE_EXCEEDED,
    MAX_NOTIONAL_EXCEEDED,
    SELF_TRADE_PREVENTED,
    NO_LIQUIDITY,
    DUPLICATE_IDEMPOTENCY_KEY,
    INTERNAL_ERROR
};

NLOHMANN_JSON_SERIALIZE_ENUM(ErrorCode, {
    {ErrorCode::NONE, "NONE"},
    {ErrorCode::INVALID_QUANTITY, "INVALID_QUANTITY"},
    {ErrorCode::INVALID_PRICE, "INVALID_PRICE"},
    {ErrorCode::INVALID_SYMBOL, "INVALID_SYMBOL"},
    {ErrorCode::INVALID_SIDE, "INVALID_SIDE"},
    {ErrorCode::INVALID_ORDER_TYPE, "INVALID_ORDER_TYPE"},
    {ErrorCode::ORDER_NOT_FOUND, "ORDER_NOT_FOUND"},
    {ErrorCode::INSUFFICIENT_BALANCE, "INSUFFICIENT_BALANCE"},
    {ErrorCode::MAX_ORDER_SIZE_EXCEEDED, "MAX_ORDER_SIZE_EXCEEDED"},
    {ErrorCode::MAX_NOTIONAL_EXCEEDED, "MAX_NOTIONAL_EXCEEDED"},
    {ErrorCode::SELF_TRADE_PREVENTED, "SELF_TRADE_PREVENTED"},
    {ErrorCode::NO_LIQUIDITY, "NO_LIQUIDITY"},
    {ErrorCode::DUPLICATE_IDEMPOTENCY_KEY, "DUPLICATE_IDEMPOTENCY_KEY"},
    {ErrorCode::INTERNAL_ERROR, "INTERNAL_ERROR"}
})

// Get human-readable error message
[[nodiscard]] std::string error_message(ErrorCode code);

// Utility: current timestamp in nanoseconds
[[nodiscard]] uint64_t now_ns();

// Book level for order book snapshots
struct BookLevel {
    int64_t price = 0;
    int64_t quantity = 0;
    int order_count = 0;
};

void to_json(nlohmann::json& j, const BookLevel& l);

}  // namespace exchange