"""API key authentication stub."""

from fastapi import Header, HTTPException, status

from app.config import settings


async def verify_api_key(x_api_key: str = Header(..., alias=settings.API_KEY_HEADER)) -> str:
    """Verify the API key from request header."""
    if x_api_key not in settings.VALID_API_KEYS:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
        )
    return x_api_key


async def verify_admin_key(x_api_key: str = Header(..., alias=settings.API_KEY_HEADER)) -> str:
    """Verify an admin API key."""
    if x_api_key not in settings.ADMIN_API_KEYS:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
        )
    return x_api_key