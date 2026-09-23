"""Stdio compatibility for MCP 2026-07-28 probes.

Antigravity CLI 1.1.12 (and other 2026-07-28 clients) send ``server/discover``
before ``initialize``. This server still speaks the 2024-11-05 initialize
handshake. The 2026-07-28 spec says any JSON-RPC error other than
UnsupportedProtocolVersionError means "legacy — fall back to initialize".

The stock mcp 1.5 FastMCP stack does not answer ``server/discover``; it
raises a pydantic ValidationError and never writes a JSON-RPC response, so
the client waits until deadline. Intercept the probe and reply ``-32601``.
"""

from __future__ import annotations

import json
from typing import Optional

METHOD_NOT_FOUND = -32601
_DISCOVER_METHOD = "server/discover"


def resolve_session_message_type():
    """Return SessionMessage when the installed mcp stdio transport uses it.

    mcp 1.5 carries JSONRPCMessage on the stdio streams. mcp 1.26 wraps the
    same JSON-RPC object in SessionMessage, which has no model_dump_json.
    """
    import inspect

    import mcp.server.stdio as stdio_mod

    try:
        source = inspect.getsource(stdio_mod.stdio_server)
    except (OSError, TypeError):
        return None
    if "SessionMessage" not in source:
        return None
    from mcp.shared.message import SessionMessage

    return SessionMessage


def encode_outbound_message(message) -> str:
    """Serialize a stdio outbound object to one JSON-RPC line body."""
    inner = getattr(message, "message", None)
    if (
        inner is not None
        and hasattr(inner, "model_dump_json")
        and not hasattr(message, "model_dump_json")
    ):
        message = inner
    dump = getattr(message, "model_dump_json", None)
    if dump is None:
        raise TypeError(
            f"Cannot encode stdio message of type {type(message).__name__}"
        )
    return dump(by_alias=True, exclude_none=True)


def decode_inbound_line(line: str, session_message_type):
    """Parse one JSON-RPC line, wrapping SessionMessage when that transport needs it."""
    import mcp.types as types

    message = types.JSONRPCMessage.model_validate_json(line)
    if session_message_type is None:
        return message
    return session_message_type(message)


def method_not_found_reply(raw_line: str) -> Optional[str]:
    """Return a JSON-RPC -32601 line if this is ``server/discover``, else None."""
    text = raw_line.strip()
    if not text:
        return None
    try:
        message = json.loads(text)
    except json.JSONDecodeError:
        return None
    if not isinstance(message, dict):
        return None
    if message.get("method") != _DISCOVER_METHOD:
        return None
    if "id" not in message:
        return None
    reply = {
        "jsonrpc": "2.0",
        "id": message["id"],
        "error": {
            "code": METHOD_NOT_FOUND,
            "message": "Method not found",
        },
    }
    return json.dumps(reply, separators=(",", ":"))


async def run_fastmcp_stdio(mcp_server) -> None:
    """Run FastMCP over stdio, answering ``server/discover`` with -32601."""
    import sys
    from contextlib import asynccontextmanager
    from io import TextIOWrapper

    import anyio
    import anyio.lowlevel

    session_message_type = resolve_session_message_type()

    @asynccontextmanager
    async def _stdio_server():
        stdin = anyio.wrap_file(TextIOWrapper(sys.stdin.buffer, encoding="utf-8"))
        stdout = anyio.wrap_file(TextIOWrapper(sys.stdout.buffer, encoding="utf-8"))

        read_stream_writer, read_stream = anyio.create_memory_object_stream(0)
        write_stream, write_stream_reader = anyio.create_memory_object_stream(0)

        async def stdin_reader():
            try:
                async with read_stream_writer:
                    async for line in stdin:
                        reply = method_not_found_reply(line)
                        if reply is not None:
                            await stdout.write(reply + "\n")
                            await stdout.flush()
                            continue
                        try:
                            message = decode_inbound_line(line, session_message_type)
                        except Exception as exc:
                            await read_stream_writer.send(exc)
                            continue
                        await read_stream_writer.send(message)
            except anyio.ClosedResourceError:
                await anyio.lowlevel.checkpoint()

        async def stdout_writer():
            try:
                async with write_stream_reader:
                    async for message in write_stream_reader:
                        payload = encode_outbound_message(message)
                        await stdout.write(payload + "\n")
                        await stdout.flush()
            except anyio.ClosedResourceError:
                await anyio.lowlevel.checkpoint()

        async with anyio.create_task_group() as tg:
            tg.start_soon(stdin_reader)
            tg.start_soon(stdout_writer)
            yield read_stream, write_stream

    async with _stdio_server() as (read_stream, write_stream):
        await mcp_server._mcp_server.run(
            read_stream,
            write_stream,
            mcp_server._mcp_server.create_initialization_options(),
        )
