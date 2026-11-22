"""End-to-end tests: HTTP API -> engine subprocess -> HTTP API.

These start the real C++ engine. They are skipped when the engine binary has
not been built (set ENGINE_PATH to point at it).
"""

from pathlib import Path

import pytest

from app.config import settings

SCALE = 100_000_000

pytestmark = pytest.mark.skipif(
    not Path(settings.ENGINE_PATH).exists(), reason="engine binary not built"
)


@pytest.fixture
async def api(client):
    yield client


def order(account, side, price, qty, tif="GTC"):
    return {
        "account_id": account,
        "symbol": "BTC-USD",
        "side": side,
        "type": "LIMIT",
        "price": price * SCALE,
        "quantity": qty * SCALE,
        "time_in_force": tif,
    }


async def test_trade_updates_book_trades_and_balances(api):
    r = await api.post("/api/v1/orders", json=order("alice", "SELL", 50_000, 2))
    assert r.status_code == 200
    r = await api.post("/api/v1/orders", json=order("bob", "BUY", 50_000, 1))
    body = r.json()
    assert body["order"]["status"] == "FILLED"
    assert len(body["trades"]) == 1

    book = (await api.get("/api/v1/book/BTC-USD")).json()
    assert book["asks"] == [{"price": 50_000 * SCALE, "quantity": 1 * SCALE, "order_count": 1}]

    trades = (await api.get("/api/v1/trades/BTC-USD")).json()["trades"]
    assert trades[0]["buyer_account_id"] == "bob"

    bob = (await api.get("/api/v1/accounts/bob/balances")).json()
    assert bob["btc"] == 11 * SCALE  # 10 granted + 1 bought
    assert bob["usd"] == (100_000 - 50_000) * SCALE

    alice = (await api.get("/api/v1/accounts/alice/balances")).json()
    assert alice["btc_reserved"] == 1 * SCALE  # still offering 1
    assert alice["btc"] == (10 - 1 - 1) * SCALE
    assert alice["usd"] == 150_000 * SCALE


async def test_ioc_and_fok_through_the_api(api):
    await api.post("/api/v1/orders", json=order("maker", "SELL", 100, 1))

    fok = (await api.post("/api/v1/orders", json=order("taker", "BUY", 100, 2, "FOK"))).json()
    assert fok["order"]["status"] == "CANCELLED"
    assert fok["trades"] == []

    ioc = (await api.post("/api/v1/orders", json=order("taker", "BUY", 100, 2, "IOC"))).json()
    assert ioc["order"]["status"] == "CANCELLED"
    assert ioc["order"]["time_in_force"] == "IOC"
    assert len(ioc["trades"]) == 1

    book = (await api.get("/api/v1/book/BTC-USD")).json()
    assert book["bids"] == [] and book["asks"] == []


async def test_replace_and_cancel(api):
    placed = (await api.post("/api/v1/orders", json=order("alice", "BUY", 100, 3))).json()
    oid = placed["order"]["id"]

    r = await api.patch(f"/api/v1/orders/{oid}", json={"price": 101 * SCALE, "quantity": 2 * SCALE})
    assert r.status_code == 200
    assert r.json()["order"]["price"] == 101 * SCALE

    book = (await api.get("/api/v1/book/BTC-USD")).json()
    assert book["bids"][0]["quantity"] == 2 * SCALE

    assert (await api.delete(f"/api/v1/orders/{oid}")).status_code == 200
    again = await api.patch(f"/api/v1/orders/{oid}", json={"price": SCALE, "quantity": SCALE})
    assert again.status_code == 400
    assert "ORDER_NOT_FOUND" in again.json()["detail"]


async def test_reset_requires_admin_key_and_clears_state(api):
    await api.post("/api/v1/orders", json=order("alice", "BUY", 100, 1))

    denied = await api.post("/api/v1/admin/reset")
    assert denied.status_code == 401

    ok = await api.post("/api/v1/admin/reset", headers={"X-API-Key": "admin-key-1"})
    assert ok.json() == {"ok": True}
    book = (await api.get("/api/v1/book/BTC-USD")).json()
    assert book["bids"] == []
    stats = (await api.get("/api/v1/stats")).json()
    assert stats["total_orders"] == 0
