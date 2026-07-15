"""
UnrealMCP bridge wire protocol (Python side).

Protocol 2.0 frame format:
  [uint32 little-endian payload length][UTF-8 JSON body]

JSON request body (unchanged):
  {"type": "<command>", "params": {...}}

This module is intentionally free of MCP / socket connection policy so it can
be unit-tested offline.
"""

from __future__ import annotations

import json
import struct
from typing import Any, Dict, Mapping, Optional, Set

# Must match UnrealMCPBridge.cpp UnrealMCPProtocolVersion
PROTOCOL_VERSION = "2.0"

# 4-byte little-endian length prefix
HEADER_STRUCT = struct.Struct("<I")
HEADER_SIZE = HEADER_STRUCT.size

# Hard cap to avoid runaway allocations (16 MiB)
MAX_PAYLOAD_BYTES = 16 * 1024 * 1024

PROTOCOL_INCOMPATIBLE_HINT = (
    "UnrealMCP protocol incompatible or framing failed. "
    "Upgrade the UnrealMCP editor plugin and this Python server together "
    f"(protocol {PROTOCOL_VERSION}, length-prefixed frames)."
)


def extract_remote_protocol_version(response: Optional[Mapping[str, Any]]) -> Optional[str]:
    """
    Pull protocol_version from a bridge ping / get_bridge_status style response.

    Accepts either raw wire shape ``{"status","result":{...}}`` or a flattened
    result dict that already contains ``protocol_version``.
    """
    if not response or not isinstance(response, Mapping):
        return None

    direct = response.get("protocol_version")
    if isinstance(direct, str) and direct.strip():
        return direct.strip()

    result = response.get("result")
    if isinstance(result, Mapping):
        nested = result.get("protocol_version")
        if isinstance(nested, str) and nested.strip():
            return nested.strip()

    return None


def format_protocol_mismatch_message(remote_version: Optional[str]) -> str:
    """Human-readable hard-handshake failure for agents and logs."""
    if remote_version:
        return (
            f"UnrealMCP protocol mismatch: plugin reports {remote_version!r}, "
            f"Python server requires {PROTOCOL_VERSION!r}. "
            "Sync Plugins/UnrealMCP from the versatile-unreal-mcp repo, rebuild the "
            "Editor target, and fully restart Unreal Editor so both sides speak the "
            "same protocol."
        )
    return (
        f"UnrealMCP plugin did not report protocol_version (Python requires "
        f"{PROTOCOL_VERSION!r}). The live plugin is likely older than this Python "
        "server. Sync Plugins/UnrealMCP from the versatile-unreal-mcp repo, rebuild, "
        "and restart Unreal Editor."
    )


def is_protocol_compatible(remote_version: Optional[str]) -> bool:
    """Return True only when the remote version exactly matches this server."""
    return isinstance(remote_version, str) and remote_version.strip() == PROTOCOL_VERSION

# Category timeouts (seconds)
TIMEOUT_BRIDGE = 5.0
TIMEOUT_DEFAULT = 10.0
TIMEOUT_EDITOR_IO = 60.0
TIMEOUT_HEAVY = 120.0

_BRIDGE_COMMANDS: Set[str] = {
    "ping",
    "get_bridge_status",
}

_HEAVY_COMMANDS: Set[str] = {
    "rebuild_material_graph",
    "rebuild_material_function_graph",
    "recompile_material",
    "validate_material_graph",
    "configure_glass_material",
    "get_material_compile_status",
    "rebuild_blueprint_graph",
    "compile_blueprint",
}

_EDITOR_IO_COMMANDS: Set[str] = {
    "open_level",
    "save_current_level",
    "save_dirty_packages",
    "take_screenshot",
}


class ProtocolError(Exception):
    """Raised when framing or payload constraints are violated."""

    def __init__(self, message: str, *, incompatible: bool = False):
        super().__init__(message)
        self.incompatible = incompatible
        self.hint = PROTOCOL_INCOMPATIBLE_HINT if incompatible else message


def timeout_for_command(command: str) -> float:
    """Return the socket timeout in seconds for a bridge command."""
    if command in _BRIDGE_COMMANDS:
        return TIMEOUT_BRIDGE
    if command in _HEAVY_COMMANDS:
        return TIMEOUT_HEAVY
    if command in _EDITOR_IO_COMMANDS or command.startswith("save_"):
        return TIMEOUT_EDITOR_IO
    return TIMEOUT_DEFAULT


def encode_frame(payload: bytes) -> bytes:
    """Encode a raw payload with a little-endian uint32 length prefix."""
    if not isinstance(payload, (bytes, bytearray)):
        raise TypeError("payload must be bytes")
    length = len(payload)
    if length == 0:
        raise ProtocolError("payload must not be empty", incompatible=False)
    if length > MAX_PAYLOAD_BYTES:
        raise ProtocolError(
            f"payload length {length} exceeds MAX_PAYLOAD_BYTES {MAX_PAYLOAD_BYTES}",
            incompatible=False,
        )
    return HEADER_STRUCT.pack(length) + bytes(payload)


def encode_json_frame(obj: Mapping[str, Any]) -> bytes:
    """Serialize a JSON object and wrap it in a length-prefixed frame."""
    body = json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return encode_frame(body)


def decode_header(header: bytes) -> int:
    """Decode a 4-byte header into a payload length."""
    if len(header) != HEADER_SIZE:
        raise ProtocolError(
            f"header must be {HEADER_SIZE} bytes, got {len(header)}",
            incompatible=True,
        )
    (length,) = HEADER_STRUCT.unpack(header)
    if length == 0:
        raise ProtocolError("payload length is zero", incompatible=True)
    if length > MAX_PAYLOAD_BYTES:
        raise ProtocolError(
            f"payload length {length} exceeds MAX_PAYLOAD_BYTES {MAX_PAYLOAD_BYTES}",
            incompatible=True,
        )
    return int(length)


def decode_json_payload(payload: bytes) -> Dict[str, Any]:
    """Parse a UTF-8 JSON object from a full payload body."""
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ProtocolError(f"payload is not valid UTF-8: {exc}", incompatible=True) from exc
    try:
        value = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ProtocolError(f"payload is not valid JSON: {exc}", incompatible=True) from exc
    if not isinstance(value, dict):
        raise ProtocolError("JSON payload must be an object", incompatible=True)
    return value


def split_frame(data: bytes) -> tuple[Dict[str, Any], bytes]:
    """
    Parse one complete frame from the front of ``data``.

    Returns (json_object, remainder). Raises ProtocolError if the buffer does
    not yet contain a full frame or the frame is invalid.
    """
    if len(data) < HEADER_SIZE:
        raise ProtocolError("incomplete header", incompatible=False)
    length = decode_header(data[:HEADER_SIZE])
    total = HEADER_SIZE + length
    if len(data) < total:
        raise ProtocolError("incomplete payload", incompatible=False)
    obj = decode_json_payload(data[HEADER_SIZE:total])
    return obj, data[total:]


def recv_exact(sock: Any, num_bytes: int) -> bytes:
    """
    Read exactly ``num_bytes`` from a socket-like object with ``recv``.

    The socket should already have an appropriate timeout set.
    """
    if num_bytes < 0:
        raise ValueError("num_bytes must be non-negative")
    if num_bytes == 0:
        return b""

    chunks = bytearray()
    while len(chunks) < num_bytes:
        try:
            chunk = sock.recv(num_bytes - len(chunks))
        except TimeoutError as exc:
            raise ProtocolError(
                f"timeout while reading {num_bytes} bytes ({len(chunks)} received)",
                incompatible=False,
            ) from exc
        except OSError as exc:
            # socket.timeout is a subclass of OSError on some Python versions
            if isinstance(exc, socket_timeout_types()):
                raise ProtocolError(
                    f"timeout while reading {num_bytes} bytes ({len(chunks)} received)",
                    incompatible=False,
                ) from exc
            raise ProtocolError(f"socket error while reading: {exc}", incompatible=True) from exc

        if not chunk:
            raise ProtocolError(
                f"connection closed while reading {num_bytes} bytes ({len(chunks)} received). "
                + PROTOCOL_INCOMPATIBLE_HINT,
                incompatible=True,
            )
        chunks.extend(chunk)
    return bytes(chunks)


def socket_timeout_types() -> tuple:
    """Return exception types that represent socket timeouts."""
    import socket as _socket

    types = [TimeoutError, _socket.timeout]
    # Avoid duplicates if socket.timeout is TimeoutError (3.10+ aliases vary)
    unique = []
    for t in types:
        if t not in unique:
            unique.append(t)
    return tuple(unique)


def recv_json_frame(sock: Any) -> Dict[str, Any]:
    """Read one length-prefixed JSON frame from ``sock``."""
    header = recv_exact(sock, HEADER_SIZE)
    try:
        length = decode_header(header)
    except ProtocolError:
        # Likely talking to a legacy (unframed) plugin — surface the hint.
        raise ProtocolError(PROTOCOL_INCOMPATIBLE_HINT, incompatible=True) from None
    payload = recv_exact(sock, length)
    return decode_json_payload(payload)


def send_json_frame(sock: Any, obj: Mapping[str, Any]) -> None:
    """Write one length-prefixed JSON frame to ``sock``."""
    frame = encode_json_frame(obj)
    sock.sendall(frame)


def build_command(command: str, params: Optional[Mapping[str, Any]] = None) -> Dict[str, Any]:
    """Build a bridge request object."""
    return {"type": command, "params": dict(params or {})}
