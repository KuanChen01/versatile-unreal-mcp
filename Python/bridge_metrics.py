"""
In-process bridge metrics and request correlation.

Records recent command outcomes for debugging multi-step agent sessions.
Thread-safe ring buffer; no external dependencies.
"""

from __future__ import annotations

import threading
import time
from collections import deque
from typing import Any, Deque, Dict, List, Optional

_LOCK = threading.Lock()
_RECENT: Deque[Dict[str, Any]] = deque(maxlen=100)
_TOTALS: Dict[str, Any] = {
    "commands": 0,
    "successes": 0,
    "failures": 0,
    "total_client_duration_ms": 0.0,
}


def new_request_id() -> str:
    """Return a short unique request id (12 hex chars)."""
    import uuid

    return uuid.uuid4().hex[:12]


def record_command_metric(
    *,
    request_id: str,
    command: str,
    success: bool,
    client_duration_ms: Optional[float] = None,
    plugin_duration_ms: Optional[float] = None,
    error_code: Optional[str] = None,
    message: Optional[str] = None,
) -> None:
    """Append one metric sample and update aggregate counters."""
    entry = {
        "request_id": request_id,
        "command": command,
        "success": bool(success),
        "ts_unix": time.time(),
    }
    if client_duration_ms is not None:
        entry["client_duration_ms"] = round(float(client_duration_ms), 2)
    if plugin_duration_ms is not None:
        entry["plugin_duration_ms"] = round(float(plugin_duration_ms), 2)
    if error_code:
        entry["error_code"] = error_code
    if message:
        entry["message"] = message[:200]

    with _LOCK:
        _RECENT.append(entry)
        _TOTALS["commands"] += 1
        if success:
            _TOTALS["successes"] += 1
        else:
            _TOTALS["failures"] += 1
        if client_duration_ms is not None:
            _TOTALS["total_client_duration_ms"] += float(client_duration_ms)


def get_recent_metrics(limit: int = 20) -> List[Dict[str, Any]]:
    """Return the most recent metrics (newest last), capped by limit."""
    limit = max(1, min(int(limit), 100))
    with _LOCK:
        items = list(_RECENT)
    if len(items) > limit:
        items = items[-limit:]
    return items


def get_metrics_summary() -> Dict[str, Any]:
    """Aggregate counters + average client duration."""
    with _LOCK:
        totals = dict(_TOTALS)
        n = int(totals.get("commands") or 0)
        total_ms = float(totals.get("total_client_duration_ms") or 0.0)
        recent = list(_RECENT)[-10:]
    avg = (total_ms / n) if n else None
    return {
        "commands": n,
        "successes": totals.get("successes", 0),
        "failures": totals.get("failures", 0),
        "avg_client_duration_ms": round(avg, 2) if avg is not None else None,
        "recent_tail": recent,
    }


def clear_metrics() -> None:
    """Reset the ring buffer and counters (tests / debug)."""
    with _LOCK:
        _RECENT.clear()
        _TOTALS["commands"] = 0
        _TOTALS["successes"] = 0
        _TOTALS["failures"] = 0
        _TOTALS["total_client_duration_ms"] = 0.0
