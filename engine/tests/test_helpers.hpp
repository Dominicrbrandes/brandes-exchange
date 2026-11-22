#pragma once

#include <string>

#include "exchange/matching_engine.hpp"

namespace exchange::test {

inline Order limit(const std::string& account, Side side, int64_t price, int64_t qty,
                   TimeInForce tif = TimeInForce::GTC) {
    Order o;
    o.account_id = account;
    o.symbol = "BTC-USD";
    o.side = side;
    o.type = OrderType::LIMIT;
    o.time_in_force = tif;
    o.price = price * PRICE_SCALE;
    o.quantity = qty;
    return o;
}

inline Order market(const std::string& account, Side side, int64_t qty,
                    TimeInForce tif = TimeInForce::GTC) {
    Order o;
    o.account_id = account;
    o.symbol = "BTC-USD";
    o.side = side;
    o.type = OrderType::MARKET;
    o.time_in_force = tif;
    o.quantity = qty;
    return o;
}

inline int64_t px(int64_t whole) {
    return whole * PRICE_SCALE;
}

}  // namespace exchange::test
