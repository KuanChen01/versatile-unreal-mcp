"""
Shared Unreal bridge client helpers for MCP tools.

Provides one response envelope for all tool modules so agents do not have to
handle status/success/result shape differences per domain.
"""

from __future__ import annotations

import logging
import time
from typing import Any, Dict, List, Optional

from bridge_protocol import PROTOCOL_VERSION

logger = logging.getLogger("UnrealMCP")


def normalize_response(
    response: Optional[Dict[str, Any]],
    *,
    command_name: str,
    duration_ms: Optional[float] = None,
) -> Dict[str, Any]:
    """
    Normalize a raw bridge response into a stable tool envelope.

    Success shape (result fields flattened for agent convenience)::

        {
          "success": True,
          "message": optional,
          ...result fields...,
          "meta": {"command", "protocol_version", "duration_ms"}
        }

    Failure shape::

        {
          "success": False,
          "message": "...",
          "error": "...",
          "meta": {...}
        }
    """
    meta: Dict[str, Any] = {
        "command": command_name,
        "protocol_version": PROTOCOL_VERSION,
    }
    if duration_ms is not None:
        meta["duration_ms"] = round(duration_ms, 2)

    if not response:
        logger.error("No response from Unreal Engine for %s", command_name)
        return {
            "success": False,
            "message": "No response from Unreal Engine",
            "error": "No response from Unreal Engine",
            "meta": meta,
        }

    # Preserve protocol mismatch flags from the connection layer when present.
    if response.get("protocol_incompatible"):
        error_message = response.get("error") or response.get("message") or "Protocol incompatible"
        return {
            "success": False,
            "message": error_message,
            "error": error_message,
            "protocol_incompatible": True,
            "meta": meta,
        }

    if response.get("status") == "error":
        error_message = response.get("error") or response.get("message") or "Unknown error"
        out = {
            "success": False,
            "message": error_message,
            "error": error_message,
            "meta": meta,
        }
        return rewrite_unknown_command_message(out, command_name)

    if response.get("success") is False:
        error_message = response.get("error") or response.get("message") or "Unknown error"
        out = {
            "success": False,
            "message": error_message,
            "error": error_message,
            "meta": meta,
        }
        return rewrite_unknown_command_message(out, command_name)

    result = response.get("result")
    if isinstance(result, dict):
        normalized: Dict[str, Any] = dict(result)
    else:
        # Already a flat payload or unexpected shape — keep usable fields.
        normalized = {
            k: v
            for k, v in response.items()
            if k not in ("status", "result")
        }

    normalized.setdefault("success", True)
    if "error" in normalized and normalized.get("success") is True:
        # Some handlers put error:null-ish; leave as-is only if success false path.
        pass
    normalized["meta"] = meta
    return normalized


def rewrite_unknown_command_message(
    normalized: Dict[str, Any],
    command_name: str,
) -> Dict[str, Any]:
    """Rewrite bare Unknown command errors into actionable upgrade guidance."""
    if normalized.get("success", False):
        return normalized

    error_message = str(normalized.get("message") or normalized.get("error") or "")
    if (
        f"Unknown command: {command_name}" in error_message
        or f"Unknown editor command: {command_name}" in error_message
        or f"Unknown blueprint command: {command_name}" in error_message
        or f"Unknown material command: {command_name}" in error_message
        or f"Unknown project command: {command_name}" in error_message
        or f"Unknown blueprint node command: {command_name}" in error_message
    ):
        normalized["message"] = (
            f"The live Unreal session does not expose {command_name} yet. "
            "Upgrade/sync the UnrealMCP editor plugin from this repo, rebuild it, "
            "and restart the Unreal Editor so the live command surface matches the Python tools."
        )
        normalized["error"] = normalized["message"]
        normalized.setdefault("error_code", "UNKNOWN_COMMAND")

    return normalized


def connection_failure(command_name: str = "") -> Dict[str, Any]:
    """Standard failure when the editor bridge is unreachable or handshake fails."""
    from unreal_mcp_server import get_last_connect_error

    detail = get_last_connect_error() or "Failed to connect to Unreal Engine"
    meta: Dict[str, Any] = {
        "protocol_version": PROTOCOL_VERSION,
        "error_code": "CONNECT_OR_HANDSHAKE_FAILED",
    }
    if command_name:
        meta["command"] = command_name

    # Version / framing mismatches get a dedicated code for agents.
    lower = detail.lower()
    if "protocol mismatch" in lower or "protocol_version" in lower:
        meta["error_code"] = "PROTOCOL_MISMATCH"
    elif "framing" in lower or "protocol incompatible" in lower:
        meta["error_code"] = "PROTOCOL_INCOMPATIBLE"

    return {
        "success": False,
        "message": detail,
        "error": detail,
        "meta": meta,
    }


def run_bridge_command(
    command_name: str,
    params: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """
    Connect (via shared helper), send one command, return a normalized envelope.
    """
    from unreal_mcp_server import clear_unreal_connection_cache, get_unreal_connection

    unreal = get_unreal_connection()
    if not unreal:
        logger.error("Failed to connect to Unreal Engine for %s", command_name)
        return connection_failure(command_name)

    started = time.perf_counter()
    try:
        response = unreal.send_command(command_name, params or {})
    except Exception as exc:  # noqa: BLE001
        logger.error("Exception running bridge command %s: %s", command_name, exc)
        return {
            "success": False,
            "message": str(exc),
            "error": str(exc),
            "meta": {
                "command": command_name,
                "protocol_version": PROTOCOL_VERSION,
            },
        }

    # Framing / hard protocol failures on a later call invalidate the cache so
    # the next tool invocation re-runs handshake instead of reusing a stale OK.
    if isinstance(response, dict) and response.get("protocol_incompatible"):
        clear_unreal_connection_cache()

    duration_ms = (time.perf_counter() - started) * 1000.0
    return normalize_response(
        response,
        command_name=command_name,
        duration_ms=duration_ms,
    )


def extract_list_field(
    normalized: Dict[str, Any],
    *field_names: str,
) -> List[Any]:
    """
    Pull a list field from a normalized success response.

    Tries each field name in order. Returns [] on failure / missing field.
    """
    if not normalized or not normalized.get("success", False):
        return []
    for name in field_names:
        value = normalized.get(name)
        if isinstance(value, list):
            return value
    return []
