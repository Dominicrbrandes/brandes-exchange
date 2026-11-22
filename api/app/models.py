"""Pydantic models for API requests and responses."""

from enum import StrEnum
from typing import Any

from pydantic import BaseModel, Field


class Side(StrEnum):
    BUY = "BUY"
    SELL = "SELL"


class OrderType(StrEnum):
    LIMIT = "LIMIT"
    MARKET = "MARKET"


class TimeInForce(StrEnum):
    GTC = "GTC"  # rest any unfilled remainder on the book
    IOC = "IOC"  # fill what is available now, cancel the rest
    FOK = "FOK"  # fill the whole quantity now or nothing


class OrderStatus(StrEnum):
    NEW = "NEW"
    PARTIAL = "PARTIAL"
    FILLED = "FILLED"
    CANCELLED = "CANCELLED"
    REJECTED = "REJECTED"


class PlaceOrderRequest(BaseModel):
    """Request to place a new order."""

    account_id: str = Field(..., min_length=1, max_length=64)
    symbol: str = Field(..., pattern=r"^[A-Z]+-[A-Z]+$")
    side: Side
    type: OrderType
    price: int = Field(0, ge=0, description="Price in fixed-point (1e8 = 1.0)")
    quantity: int = Field(..., gt=0, description="Quantity in fixed-point")
    time_in_force: TimeInForce = TimeInForce.GTC
    idempotency_key: str | None = Field(None, max_length=64)
    client_order_id: str | None = Field(None, max_length=64)

    model_config = {"json_schema_extra": {"examples": [
        {
            "account_id": "user123",
            "symbol": "BTC-USD",
            "side": "BUY",
            "type": "LIMIT",
            "price": 5000000000000,
            "quantity": 100000000,
            "idempotency_key": "order-abc-123",
        }
    ]}}


class Order(BaseModel):
    """Order representation."""

    id: int
    account_id: str
    symbol: str
    side: Side
    type: OrderType
    price: int
    quantity: int
    remaining_qty: int
    timestamp_ns: int
    status: OrderStatus
    time_in_force: TimeInForce = TimeInForce.GTC
    idempotency_key: str | None = None
    client_order_id: str | None = None


class Trade(BaseModel):
    """Trade representation."""

    id: int
    buy_order_id: int
    sell_order_id: int
    symbol: str
    price: int
    quantity: int
    timestamp_ns: int
    buyer_account_id: str
    seller_account_id: str


class BookLevel(BaseModel):
    """Aggregated price level in order book."""

    price: int
    quantity: int
    order_count: int


class OrderBookResponse(BaseModel):
    """Order book snapshot."""

    symbol: str
    bids: list[BookLevel]
    asks: list[BookLevel]


class PlaceOrderResponse(BaseModel):
    """Response from placing an order."""

    order: Order
    trades: list[Trade]


class ReplaceOrderRequest(BaseModel):
    """Cancel/replace: new price and new total quantity (filled + remaining)."""

    price: int = Field(..., gt=0, description="New price in fixed-point")
    quantity: int = Field(..., gt=0, description="New total quantity in fixed-point")


class BalanceResponse(BaseModel):
    """Demo account balances: a starting grant plus the account's fills.

    The engine does not enforce balances; these figures are for display.
    """

    usd: int
    btc: int
    usd_reserved: int
    btc_reserved: int


class TradesResponse(BaseModel):
    """Recent trades response."""

    symbol: str
    trades: list[Trade]


class StatsResponse(BaseModel):
    """Engine statistics."""

    total_orders: int
    total_trades: int
    total_cancels: int
    total_rejects: int
    total_replaces: int = 0
    total_expired: int = 0
    event_sequence: int


class HealthResponse(BaseModel):
    """Health check response."""

    status: str
    timestamp_ns: int
    engine_connected: bool


class ErrorResponse(BaseModel):
    """Error response."""

    code: str
    message: str
    request_id: str | None = None


class APIResponse(BaseModel):
    """Generic API response wrapper."""

    success: bool
    data: Any | None = None
    error: ErrorResponse | None = None