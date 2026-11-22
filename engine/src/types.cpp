#include "exchange/types.hpp"

#include <cstdio>

namespace exchange {

void to_json(nlohmann::json& j, const Order& o) {
    j = nlohmann::json{{"id", o.id},
                       {"account_id", o.account_id},
                       {"symbol", o.symbol},
                       {"side", o.side},
                       {"type", o.type},
                       {"price", o.price},
                       {"quantity", o.quantity},
                       {"remaining_qty", o.remaining_qty},
                       {"timestamp_ns", o.timestamp_ns},
                       {"status", o.status}};
    if (o.time_in_force != TimeInForce::GTC) {
        j["time_in_force"] = o.time_in_force;
    }
    if (!o.idempotency_key.empty()) {
        j["idempotency_key"] = o.idempotency_key;
    }
    if (!o.client_order_id.empty()) {
        j["client_order_id"] = o.client_order_id;
    }
}

void from_json(const nlohmann::json& j, Order& o) {
    j.at("account_id").get_to(o.account_id);
    j.at("symbol").get_to(o.symbol);
    j.at("side").get_to(o.side);
    j.at("type").get_to(o.type);
    if (j.contains("time_in_force") && !j["time_in_force"].is_null()) {
        j.at("time_in_force").get_to(o.time_in_force);
    }
    if (j.contains("price")) {
        j.at("price").get_to(o.price);
    }
    j.at("quantity").get_to(o.quantity);
    o.remaining_qty = o.quantity;
    if (j.contains("idempotency_key") && !j["idempotency_key"].is_null()) {
        j.at("idempotency_key").get_to(o.idempotency_key);
    }
    if (j.contains("client_order_id") && !j["client_order_id"].is_null()) {
        j.at("client_order_id").get_to(o.client_order_id);
    }
}

Order order_from_persisted(const nlohmann::json& j) {
    Order o;
    j.at("id").get_to(o.id);
    j.at("account_id").get_to(o.account_id);
    j.at("symbol").get_to(o.symbol);
    j.at("side").get_to(o.side);
    j.at("type").get_to(o.type);
    o.time_in_force = j.value("time_in_force", TimeInForce::GTC);
    j.at("quantity").get_to(o.quantity);
    if (j.contains("price")) {
        j.at("price").get_to(o.price);
    }

    // These three are the reason this function exists. from_json drops them,
    // which silently restored filled orders to full size on recovery.
    o.remaining_qty = j.value("remaining_qty", o.quantity);
    o.status = j.value("status", OrderStatus::NEW);
    o.timestamp_ns = j.value("timestamp_ns", uint64_t{0});

    if (j.contains("idempotency_key") && !j["idempotency_key"].is_null()) {
        j.at("idempotency_key").get_to(o.idempotency_key);
    }
    if (j.contains("client_order_id") && !j["client_order_id"].is_null()) {
        j.at("client_order_id").get_to(o.client_order_id);
    }
    return o;
}

void to_json(nlohmann::json& j, const Trade& t) {
    j = nlohmann::json{{"id", t.id},
                       {"buy_order_id", t.buy_order_id},
                       {"sell_order_id", t.sell_order_id},
                       {"symbol", t.symbol},
                       {"price", t.price},
                       {"quantity", t.quantity},
                       {"timestamp_ns", t.timestamp_ns},
                       {"buyer_account_id", t.buyer_account_id},
                       {"seller_account_id", t.seller_account_id}};
}

void from_json(const nlohmann::json& j, Trade& t) {
    j.at("id").get_to(t.id);
    j.at("buy_order_id").get_to(t.buy_order_id);
    j.at("sell_order_id").get_to(t.sell_order_id);
    j.at("symbol").get_to(t.symbol);
    j.at("price").get_to(t.price);
    j.at("quantity").get_to(t.quantity);
    j.at("timestamp_ns").get_to(t.timestamp_ns);
    if (j.contains("buyer_account_id")) {
        j.at("buyer_account_id").get_to(t.buyer_account_id);
    }
    if (j.contains("seller_account_id")) {
        j.at("seller_account_id").get_to(t.seller_account_id);
    }
}

void to_json(nlohmann::json& j, const Account& a) {
    j = nlohmann::json{{"id", a.id}, {"balances", a.balances}};
}

void from_json(const nlohmann::json& j, Account& a) {
    j.at("id").get_to(a.id);
    j.at("balances").get_to(a.balances);
}

void to_json(nlohmann::json& j, const Event& e) {
    j = nlohmann::json{{"sequence", e.sequence},
                       {"timestamp_ns", e.timestamp_ns},
                       {"type", e.type},
                       {"payload", e.payload}};
}

void from_json(const nlohmann::json& j, Event& e) {
    j.at("sequence").get_to(e.sequence);
    j.at("timestamp_ns").get_to(e.timestamp_ns);
    j.at("type").get_to(e.type);
    j.at("payload").get_to(e.payload);
}

const char* to_string(Side s) {
    return s == Side::BUY ? "BUY" : "SELL";
}

const char* to_string(OrderType t) {
    return t == OrderType::LIMIT ? "LIMIT" : "MARKET";
}

const char* to_string(TimeInForce t) {
    switch (t) {
        case TimeInForce::GTC:
            return "GTC";
        case TimeInForce::IOC:
            return "IOC";
        case TimeInForce::FOK:
            return "FOK";
    }
    return "GTC";
}

const char* to_string(OrderStatus s) {
    switch (s) {
        case OrderStatus::NEW:
            return "NEW";
        case OrderStatus::PARTIAL:
            return "PARTIAL";
        case OrderStatus::FILLED:
            return "FILLED";
        case OrderStatus::CANCELLED:
            return "CANCELLED";
        case OrderStatus::REJECTED:
            return "REJECTED";
    }
    return "NEW";
}

const char* to_string(EventType t) {
    switch (t) {
        case EventType::ORDER_PLACED:
            return "ORDER_PLACED";
        case EventType::ORDER_CANCELLED:
            return "ORDER_CANCELLED";
        case EventType::ORDER_REJECTED:
            return "ORDER_REJECTED";
        case EventType::TRADE_EXECUTED:
            return "TRADE_EXECUTED";
        case EventType::SNAPSHOT_MARKER:
            return "SNAPSHOT_MARKER";
        case EventType::ORDER_REPLACED:
            return "ORDER_REPLACED";
    }
    return "ORDER_PLACED";
}

namespace {

void append_int(std::string& out, int64_t v) {
    char buf[24];
    out.append(buf, static_cast<size_t>(
                        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v))));
}

void append_uint(std::string& out, uint64_t v) {
    char buf[24];
    out.append(buf, static_cast<size_t>(std::snprintf(buf, sizeof(buf), "%llu",
                                                      static_cast<unsigned long long>(v))));
}

// Account ids, symbols and client ids are operator- or client-supplied, so they
// still need escaping even though the common case has nothing to escape.
void append_escaped(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void append_key(std::string& out, const char* key) {
    out += '"';
    out += key;
    out += "\":";
}

}  // namespace

void append_json(std::string& out, const Order& o) {
    out += '{';
    append_key(out, "id");
    append_uint(out, o.id);
    out += ',';
    append_key(out, "account_id");
    append_escaped(out, o.account_id);
    out += ',';
    append_key(out, "symbol");
    append_escaped(out, o.symbol);
    out += ',';
    append_key(out, "side");
    append_escaped(out, to_string(o.side));
    out += ',';
    append_key(out, "type");
    append_escaped(out, to_string(o.type));
    out += ',';
    append_key(out, "price");
    append_int(out, o.price);
    out += ',';
    append_key(out, "quantity");
    append_int(out, o.quantity);
    out += ',';
    append_key(out, "remaining_qty");
    append_int(out, o.remaining_qty);
    out += ',';
    append_key(out, "timestamp_ns");
    append_uint(out, o.timestamp_ns);
    out += ',';
    append_key(out, "status");
    append_escaped(out, to_string(o.status));
    if (o.time_in_force != TimeInForce::GTC) {
        out += ',';
        append_key(out, "time_in_force");
        append_escaped(out, to_string(o.time_in_force));
    }
    if (!o.idempotency_key.empty()) {
        out += ',';
        append_key(out, "idempotency_key");
        append_escaped(out, o.idempotency_key);
    }
    if (!o.client_order_id.empty()) {
        out += ',';
        append_key(out, "client_order_id");
        append_escaped(out, o.client_order_id);
    }
    out += '}';
}

void append_json(std::string& out, const Trade& t) {
    out += '{';
    append_key(out, "id");
    append_uint(out, t.id);
    out += ',';
    append_key(out, "buy_order_id");
    append_uint(out, t.buy_order_id);
    out += ',';
    append_key(out, "sell_order_id");
    append_uint(out, t.sell_order_id);
    out += ',';
    append_key(out, "symbol");
    append_escaped(out, t.symbol);
    out += ',';
    append_key(out, "price");
    append_int(out, t.price);
    out += ',';
    append_key(out, "quantity");
    append_int(out, t.quantity);
    out += ',';
    append_key(out, "timestamp_ns");
    append_uint(out, t.timestamp_ns);
    out += ',';
    append_key(out, "buyer_account_id");
    append_escaped(out, t.buyer_account_id);
    out += ',';
    append_key(out, "seller_account_id");
    append_escaped(out, t.seller_account_id);
    out += '}';
}

void to_json(nlohmann::json& j, const BookLevel& l) {
    j = nlohmann::json{
        {"price", l.price}, {"quantity", l.quantity}, {"order_count", l.order_count}};
}

void to_json(nlohmann::json& j, const Position& p) {
    j = nlohmann::json{{"base", p.base}, {"quote", p.quote}};
}

void from_json(const nlohmann::json& j, Position& p) {
    j.at("base").get_to(p.base);
    j.at("quote").get_to(p.quote);
}

int64_t notional(int64_t price, int64_t quantity) {
#if defined(__SIZEOF_INT128__)
    return static_cast<int64_t>(static_cast<__int128>(price) * quantity / PRICE_SCALE);
#else
    return static_cast<int64_t>(static_cast<long double>(price) * quantity / PRICE_SCALE);
#endif
}

std::string error_message(ErrorCode code) {
    switch (code) {
        case ErrorCode::NONE:
            return "Success";
        case ErrorCode::INVALID_QUANTITY:
            return "Quantity must be positive";
        case ErrorCode::INVALID_PRICE:
            return "Price must be positive for limit orders";
        case ErrorCode::INVALID_SYMBOL:
            return "Unknown or invalid symbol";
        case ErrorCode::INVALID_SIDE:
            return "Side must be BUY or SELL";
        case ErrorCode::INVALID_ORDER_TYPE:
            return "Order type must be LIMIT or MARKET";
        case ErrorCode::ORDER_NOT_FOUND:
            return "Order not found";
        case ErrorCode::INSUFFICIENT_BALANCE:
            return "Insufficient account balance";
        case ErrorCode::MAX_ORDER_SIZE_EXCEEDED:
            return "Order size exceeds maximum allowed";
        case ErrorCode::MAX_NOTIONAL_EXCEEDED:
            return "Order notional value exceeds maximum allowed";
        case ErrorCode::SELF_TRADE_PREVENTED:
            return "Order would result in self-trade";
        case ErrorCode::NO_LIQUIDITY:
            return "No liquidity available for market order";
        case ErrorCode::DUPLICATE_IDEMPOTENCY_KEY:
            return "Duplicate idempotency key";
        case ErrorCode::INTERNAL_ERROR:
            return "Internal engine error";
    }
    return "Unknown error";
}

uint64_t now_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(ns.count());
}

}  // namespace exchange