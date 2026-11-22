"""Test fixtures."""

from pathlib import Path

import pytest
from httpx import ASGITransport, AsyncClient

from app.config import settings
from app.engine_client import engine_client
from app.main import app


@pytest.fixture
async def client(tmp_path, monkeypatch):
    """HTTP client for the app, backed by a real engine when one is built.

    ASGITransport does not run the app's lifespan, so the engine is started
    here, with its event log in a throwaway directory.
    """
    monkeypatch.setenv("DATA_DIR", str(tmp_path))
    monkeypatch.setenv("RATE_LIMIT_REQUESTS", "10000")
    engine_built = Path(settings.ENGINE_PATH).exists()
    if engine_built:
        await engine_client.start()
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app),
            base_url="http://test",
            headers={"X-API-Key": "test-key-1"},
        ) as ac:
            yield ac
    finally:
        if engine_built:
            await engine_client.stop()
