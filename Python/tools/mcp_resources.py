"""
MCP Resources — read-only Unreal editor surfaces for agent planning.

Resources are not tools: clients read URIs instead of guessing which get_* tool to call.
All live resources use bridge_client (protocol 2.0). Offline resources work without Editor.
"""

from __future__ import annotations

import json
import logging
from typing import Any, Dict

from mcp.server.fastmcp import FastMCP

from bridge_client import extract_list_field, run_bridge_command
from bridge_protocol import PROTOCOL_VERSION

logger = logging.getLogger("UnrealMCP")


def _json(data: Any) -> str:
    return json.dumps(data, indent=2, default=str, ensure_ascii=False)


def _bridge_error_payload(label: str, response: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "success": False,
        "resource": label,
        "message": response.get("message") or response.get("error") or "Unreal bridge unavailable",
        "error": response.get("error") or response.get("message"),
        "hint": "Open UnrealMCP_ZipSmoke_57 (or your test project) with UnrealMCP enabled; protocol 2.0 required.",
        "raw": response,
    }


def register_mcp_resources(mcp: FastMCP) -> None:
    """Register Unreal-related MCP resources on the server."""

    @mcp.resource(
        "unreal://protocol",
        name="UnrealMCP Protocol",
        description="Static protocol contract for the Python server (no Editor required).",
        mime_type="application/json",
    )
    def resource_protocol() -> str:
        from unreal_mcp_server import SERVER_NAME, SERVER_VERSION, UNREAL_HOST, UNREAL_PORT

        return _json(
            {
                "success": True,
                "protocol_version": PROTOCOL_VERSION,
                "framing": "uint32_le_length_prefix + utf8_json",
                "max_payload_bytes": 16 * 1024 * 1024,
                "default_listen": {"host": "127.0.0.1", "port": 55557},
                "client_target": {"host": UNREAL_HOST, "port": UNREAL_PORT},
                "server": {"name": SERVER_NAME, "version": SERVER_VERSION},
                "env_overrides": ["UNREAL_MCP_HOST", "UNREAL_MCP_PORT"],
                "co_upgrade": "Python server and UnrealMCP plugin must both speak protocol 2.0",
            }
        )

    @mcp.resource(
        "unreal://bridge/status",
        name="Bridge Status",
        description="Live UnrealMCP bridge status: protocol, plugin, listen address, routed command groups.",
        mime_type="application/json",
    )
    def resource_bridge_status() -> str:
        from unreal_mcp_server import SERVER_NAME, SERVER_VERSION

        response = run_bridge_command("get_bridge_status")
        if not response.get("success", False):
            return _json(_bridge_error_payload("unreal://bridge/status", response))
        response["server"] = {"name": SERVER_NAME, "version": SERVER_VERSION}
        response["resource"] = "unreal://bridge/status"
        return _json(response)

    @mcp.resource(
        "unreal://level/status",
        name="Level Status",
        description="Current editor world/level package path, dirty state, and actor count.",
        mime_type="application/json",
    )
    def resource_level_status() -> str:
        response = run_bridge_command("get_level_status")
        if not response.get("success", False):
            return _json(_bridge_error_payload("unreal://level/status", response))
        response["resource"] = "unreal://level/status"
        return _json(response)

    @mcp.resource(
        "unreal://viewport/status",
        name="Viewport Status",
        description="Active level-editor viewport availability and self-check readiness.",
        mime_type="application/json",
    )
    def resource_viewport_status() -> str:
        response = run_bridge_command("get_viewport_status")
        if not response.get("success", False):
            return _json(_bridge_error_payload("unreal://viewport/status", response))
        response["resource"] = "unreal://viewport/status"
        return _json(response)

    @mcp.resource(
        "unreal://play/state",
        name="Play State",
        description="Whether PIE/SIE is active or queued and which world is running.",
        mime_type="application/json",
    )
    def resource_play_state() -> str:
        response = run_bridge_command("get_play_state")
        if not response.get("success", False):
            return _json(_bridge_error_payload("unreal://play/state", response))
        response["resource"] = "unreal://play/state"
        return _json(response)

    @mcp.resource(
        "unreal://actors/list",
        name="Actors In Level",
        description="JSON list of actors currently in the editor level (name, class, transform when available).",
        mime_type="application/json",
    )
    def resource_actors_list() -> str:
        response = run_bridge_command("get_actors_in_level")
        if not response.get("success", False) and not extract_list_field(response, "actors"):
            # Some normalizations may flatten success differently
            if "actors" not in response and response.get("success") is False:
                return _json(_bridge_error_payload("unreal://actors/list", response))
        actors = extract_list_field(response, "actors")
        if not actors and isinstance(response.get("actors"), list):
            actors = response["actors"]
        return _json(
            {
                "success": True,
                "resource": "unreal://actors/list",
                "count": len(actors),
                "actors": actors,
            }
        )

    @mcp.resource(
        "unreal://preflight",
        name="Editor Preflight",
        description="Composite readiness snapshot (bridge + level + viewport) before mutations.",
        mime_type="application/json",
    )
    def resource_preflight() -> str:
        bridge = run_bridge_command("get_bridge_status")
        level = run_bridge_command("get_level_status")
        viewport = run_bridge_command("get_viewport_status")
        editor = (bridge.get("editor") or {}) if isinstance(bridge, dict) else {}
        protocol = bridge.get("protocol_version") if isinstance(bridge, dict) else None
        ready = bool(
            bridge.get("success")
            and protocol == PROTOCOL_VERSION
            and editor.get("connected")
            and level.get("success")
        )
        issues = []
        if not bridge.get("success"):
            issues.append("bridge unavailable")
        elif protocol != PROTOCOL_VERSION:
            issues.append(f"protocol {protocol!r} != {PROTOCOL_VERSION}")
        if not editor.get("connected"):
            issues.append("editor not connected")
        if not level.get("success"):
            issues.append("level status failed")
        if not viewport.get("success"):
            issues.append("viewport status failed (non-fatal)")

        return _json(
            {
                "success": ready,
                "ready": ready,
                "resource": "unreal://preflight",
                "protocol_version": protocol,
                "listen": bridge.get("listen") if isinstance(bridge, dict) else None,
                "plugin": bridge.get("plugin") if isinstance(bridge, dict) else None,
                "level": {
                    "success": level.get("success"),
                    "level_name": level.get("level_name") or level.get("world_name"),
                    "package_name": level.get("package_name"),
                    "is_dirty": level.get("is_dirty"),
                    "actor_count": level.get("actor_count"),
                },
                "viewport": {
                    "success": viewport.get("success"),
                    "has_active_viewport": viewport.get("has_active_viewport")
                    or editor.get("has_active_viewport"),
                },
                "issues": issues,
                "hints": [
                    "Read unreal://preflight or call editor_preflight before mutations.",
                    "Prefer workflow tools for multi-step recipes.",
                    "Use unique actor names.",
                ],
            }
        )

    @mcp.resource(
        "unreal://assets/find/{query}",
        name="Find Assets",
        description=(
            "Search Content Browser assets by substring query under /Game "
            "(max 50). Use query '_' for a broad sample under /Game."
        ),
        mime_type="application/json",
    )
    def resource_find_assets(query: str) -> str:
        q = (query or "").strip()
        if q in ("_", "*", "all", "-"):
            q = ""
        response = run_bridge_command(
            "find_assets",
            {
                "path": "/Game",
                "query": q,
                "class_name": "",
                "max_results": 50,
                "recursive": True,
            },
        )
        if not response.get("success", False):
            return _json(_bridge_error_payload(f"unreal://assets/find/{query}", response))
        assets = response.get("assets") or response.get("results") or []
        return _json(
            {
                "success": True,
                "resource": f"unreal://assets/find/{query}",
                "path": "/Game",
                "query": q,
                "count": response.get("count", len(assets) if isinstance(assets, list) else 0),
                "assets": assets,
            }
        )

    @mcp.resource(
        "unreal://asset/info/{package_path}",
        name="Asset Info",
        description=(
            "Registry info for one asset. package_path uses dashes instead of slashes, "
            "e.g. Game-MCP_Smoke-M_Name for /Game/MCP_Smoke/M_Name."
        ),
        mime_type="application/json",
    )
    def resource_asset_info(package_path: str) -> str:
        # Template params cannot include '/'; encode /Game/Foo as Game-Foo
        raw = (package_path or "").strip()
        if not raw:
            return _json(
                {
                    "success": False,
                    "message": "package_path is empty",
                    "hint": "Use Game-Folder-AssetName for /Game/Folder/AssetName",
                }
            )
        path = raw.replace("-", "/")
        if not path.startswith("/"):
            path = "/" + path
        response = run_bridge_command("get_asset_info", {"asset_path": path})
        if not response.get("success", False):
            return _json(_bridge_error_payload(f"unreal://asset/info/{package_path}", response))
        response["resource"] = f"unreal://asset/info/{package_path}"
        response["resolved_asset_path"] = path
        return _json(response)

    logger.info("MCP resources registered successfully")
