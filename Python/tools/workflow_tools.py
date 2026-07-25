"""
Composite workflow tools for Unreal MCP.

High-frequency multi-step recipes so agents do not re-chain many fine-grained tools.
All calls go through bridge_client (protocol 2.0 envelope).
"""

from __future__ import annotations

import logging
from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import Context, FastMCP

from bridge_client import run_bridge_command

logger = logging.getLogger("UnrealMCP")


def _fail(message: str, **extra: Any) -> Dict[str, Any]:
    out: Dict[str, Any] = {
        "success": False,
        "message": message,
        "error": message,
    }
    out.update(extra)
    return out


def _ok(message: str, **extra: Any) -> Dict[str, Any]:
    out: Dict[str, Any] = {
        "success": True,
        "message": message,
    }
    out.update(extra)
    return out


def register_workflow_tools(mcp: FastMCP) -> None:
    """Register composite workflow tools."""

    @mcp.tool()
    def editor_preflight(ctx: Context) -> Dict[str, Any]:
        """
        Read-only preflight before mutating the editor.

        Checks bridge/protocol, editor connection, level status, and viewport.
        Prefer this (or get_bridge_status) before spawn/delete/rebuild tools.
        """
        EXPECTED_HANDLER_BUILD = "2026-07-25.4"
        bridge = run_bridge_command("get_bridge_status")
        if not bridge.get("success", False):
            return _fail(
                "Bridge preflight failed — is Unreal open with UnrealMCP and protocol 2.0 on 127.0.0.1:55557?",
                bridge=bridge,
                ready=False,
                editor_online=False,
                recovery_hint=(
                    "Open UnrealMCP_ZipSmoke_57 (test project) with UnrealMCP enabled; "
                    "do not use product projects for agent MCP tests unless asked."
                ),
            )

        level = run_bridge_command("get_level_status")
        viewport = run_bridge_command("get_viewport_status")

        protocol = bridge.get("protocol_version")
        editor = bridge.get("editor") or {}
        plugin = bridge.get("plugin") or {}
        handler_build = plugin.get("handler_build")
        build_mismatch = (not handler_build) or (handler_build != EXPECTED_HANDLER_BUILD)

        issues: List[str] = []
        if protocol != "2.0":
            issues.append(f"protocol_version is {protocol!r}, expected '2.0'")
        if not editor.get("connected"):
            issues.append("editor.connected is not true")
        if not level.get("success", False):
            issues.append("get_level_status failed")
        if not viewport.get("success", False):
            issues.append("get_viewport_status failed (viewport may be unavailable)")
        if build_mismatch:
            issues.append(
                f"handler_build={handler_build!r}, expected={EXPECTED_HANDLER_BUILD!r} "
                "(sync/rebuild plugin + full Editor restart)"
            )

        # Connection can still work with an older build; flag mismatch but allow
        # soft-ready when only the stamp is missing (agents decide whether to upgrade).
        core_ready = bool(
            protocol == "2.0"
            and editor.get("connected")
            and level.get("success", False)
        )
        ready = core_ready and not build_mismatch

        return {
            "success": ready,
            "ready": ready,
            "core_ready": core_ready,
            "message": "Editor ready for mutations" if ready else "; ".join(issues) or "Not ready",
            "error": None if ready else ("; ".join(issues) or "Not ready"),
            "protocol_version": protocol,
            "listen": bridge.get("listen"),
            "plugin": plugin,
            "handler_build": handler_build,
            "expected_handler_build": EXPECTED_HANDLER_BUILD,
            "handler_build_mismatch": build_mismatch,
            "editor_online": True,
            "level": {
                "success": level.get("success"),
                "level_name": level.get("level_name") or level.get("world_name"),
                "package_name": level.get("package_name"),
                "is_dirty": level.get("is_dirty"),
                "actor_count": level.get("actor_count"),
            },
            "viewport": {
                "success": viewport.get("success"),
                "has_active_viewport": (viewport.get("has_active_viewport")
                                        or (viewport.get("editor") or {}).get("has_active_viewport")
                                        or editor.get("has_active_viewport")),
            },
            "hints": [
                "Call editor_preflight (or get_bridge_status) before destructive tools.",
                "Use unique actor names, or spawn with replace_existing=true for respawns.",
                "Prefer rebuild_material_graph / rebuild_blueprint_graph over many incremental node tools.",
                "After plugin upgrades, fully restart the Editor — hot-reload often keeps old handlers.",
            ],
        }

    @mcp.tool()
    def spawn_actor_with_material(
        ctx: Context,
        class_path: str,
        material_path: str,
        name: str = "",
        location: Optional[List[float]] = None,
        rotation: Optional[List[float]] = None,
        scale: Optional[List[float]] = None,
        slot_index: int = 0,
    ) -> Dict[str, Any]:
        """
        Composite: spawn_actor_by_class then assign_material_to_actor.

        DESTRUCTIVE: mutates the level (spawns an actor). Use a unique name.
        class_path: e.g. StaticMeshActor, PointLight, or /Game/... Blueprint path.
        material_path: e.g. /Game/.../M_Name
        """
        loc = location or [0.0, 0.0, 0.0]
        rot = rotation or [0.0, 0.0, 0.0]
        scl = scale or [1.0, 1.0, 1.0]

        for label, vec in (("location", loc), ("rotation", rot), ("scale", scl)):
            if not isinstance(vec, list) or len(vec) != 3:
                return _fail(f"Invalid {label}: must be a list of 3 floats")

        spawn_params: Dict[str, Any] = {
            "class_path": class_path,
            "location": [float(x) for x in loc],
            "rotation": [float(x) for x in rot],
            "scale": [float(x) for x in scl],
            "replace_existing": bool(name),  # named spawns in recipes: safe respawn
        }
        if name:
            spawn_params["name"] = name

        spawn = run_bridge_command("spawn_actor_by_class", spawn_params)
        if not spawn.get("success", False):
            return _fail(
                f"spawn_actor_by_class failed: {spawn.get('message') or spawn.get('error')}",
                step="spawn",
                spawn=spawn,
            )

        actor_name = (
            spawn.get("name")
            or spawn.get("actor_name")
            or name
            or (spawn.get("actor") or {}).get("name")
        )
        if not actor_name:
            # Fallback: some responses flatten actor fields
            actor_name = spawn.get("Name") or ""
        if not actor_name:
            return _fail(
                "Spawn reported success but no actor name was returned",
                step="spawn",
                spawn=spawn,
            )

        assign = run_bridge_command(
            "assign_material_to_actor",
            {
                "actor_name": actor_name,
                "material_path": material_path,
                "slot_index": int(slot_index),
            },
        )
        if not assign.get("success", False):
            return {
                "success": False,
                "message": (
                    f"Spawned '{actor_name}' but assign_material failed: "
                    f"{assign.get('message') or assign.get('error')}"
                ),
                "error": assign.get("error") or assign.get("message"),
                "step": "assign_material",
                "actor_name": actor_name,
                "spawn": spawn,
                "assign": assign,
            }

        return _ok(
            f"Spawned '{actor_name}' and assigned material '{material_path}'",
            actor_name=actor_name,
            class_path=class_path,
            material_path=material_path,
            slot_index=int(slot_index),
            spawn=spawn,
            assign=assign,
        )

    @mcp.tool()
    def create_and_rebuild_material(
        ctx: Context,
        material_path: str,
        graph_spec: Dict[str, Any],
        run_validate: bool = True,
    ) -> Dict[str, Any]:
        """
        Composite: create_material then rebuild_material_graph (optional validate).

        DESTRUCTIVE: creates/overwrites material graph content at material_path.
        Prefer a full graph_spec (version 1) rather than many incremental expression tools.
        """
        if not material_path or not material_path.startswith("/Game"):
            return _fail("material_path must be a /Game/... package path")
        if not isinstance(graph_spec, dict) or not graph_spec:
            return _fail("graph_spec must be a non-empty object")

        create = run_bridge_command("create_material", {"material_path": material_path})
        # create may fail if asset already exists — still try rebuild
        create_ok = create.get("success", False)
        create_msg = create.get("message") or create.get("error") or ""

        rebuild = run_bridge_command(
            "rebuild_material_graph",
            {"material_path": material_path, "graph_spec": graph_spec},
        )
        if not rebuild.get("success", False):
            return _fail(
                f"rebuild_material_graph failed: {rebuild.get('message') or rebuild.get('error')}",
                step="rebuild",
                material_path=material_path,
                create_success=create_ok,
                create=create,
                rebuild=rebuild,
            )

        validate_result = None
        if run_validate:
            validate_result = run_bridge_command(
                "validate_material_graph",
                {"material_path": material_path},
            )

        return _ok(
            f"Material ready at {material_path}",
            material_path=material_path,
            created=create_ok,
            create_note=create_msg if not create_ok else None,
            rebuild=rebuild,
            validate=validate_result,
        )

    @mcp.tool()
    def create_blueprint_with_graph(
        ctx: Context,
        name: str,
        graph_spec: Dict[str, Any],
        parent_class: str = "Actor",
        clear_event_graph: bool = True,
        compile: bool = True,
    ) -> Dict[str, Any]:
        """
        Composite: create_blueprint then rebuild_blueprint_graph then compile_blueprint.

        DESTRUCTIVE: creates a Blueprint asset and rewrites its event graph when clear_event_graph=true.
        Prefer graph_spec local ids over many add_*/connect_* calls.
        """
        if not name:
            return _fail("Blueprint name is required")
        if not isinstance(graph_spec, dict) or not graph_spec:
            return _fail("graph_spec must be a non-empty object")

        create = run_bridge_command(
            "create_blueprint",
            {"name": name, "parent_class": parent_class},
        )
        create_ok = create.get("success", False)
        # If BP already exists, continue with rebuild
        if not create_ok:
            logger.info("create_blueprint returned: %s — continuing with rebuild", create.get("message"))

        rebuild = run_bridge_command(
            "rebuild_blueprint_graph",
            {
                "blueprint_name": name,
                "graph_spec": graph_spec,
                "clear_event_graph": clear_event_graph,
                "compile": compile,
            },
        )
        if not rebuild.get("success", False):
            return _fail(
                f"rebuild_blueprint_graph failed: {rebuild.get('message') or rebuild.get('error')}",
                step="rebuild",
                blueprint_name=name,
                create=create,
                rebuild=rebuild,
            )

        compile_result = None
        if compile:
            compile_result = run_bridge_command("compile_blueprint", {"blueprint_name": name})

        return _ok(
            f"Blueprint '{name}' graph applied",
            blueprint_name=name,
            parent_class=parent_class,
            created=create_ok,
            connection_failures=rebuild.get("connection_failures"),
            compile_ok=rebuild.get("compile_ok"),
            rebuild=rebuild,
            compile=compile_result,
        )

    @mcp.tool()
    def respawn_actor_by_class(
        ctx: Context,
        class_path: str,
        name: str,
        location: Optional[List[float]] = None,
        rotation: Optional[List[float]] = None,
        scale: Optional[List[float]] = None,
    ) -> Dict[str, Any]:
        """
        DESTRUCTIVE: ensure an actor name is free, then spawn (replace_existing).

        Preferred over manual delete_actor + spawn when reusing a fixed actor_name.
        Requires plugin handler_build >= 2026-07-25.4 for native replace_existing
        (EditorDestroyActor + CollectGarbage). Falls back to delete+retry spawn.
        """
        import time

        if not name:
            return _fail("name is required for respawn")
        if not class_path:
            return _fail("class_path is required")

        loc = location or [0.0, 0.0, 0.0]
        rot = rotation or [0.0, 0.0, 0.0]
        scl = scale or [1.0, 1.0, 1.0]
        for label, vec in (("location", loc), ("rotation", rot), ("scale", scl)):
            if not isinstance(vec, list) or len(vec) != 3:
                return _fail(f"Invalid {label}: must be a list of 3 floats")

        spawn_params: Dict[str, Any] = {
            "class_path": class_path,
            "name": name,
            "location": [float(x) for x in loc],
            "rotation": [float(x) for x in rot],
            "scale": [float(x) for x in scl],
            "replace_existing": True,
        }
        spawn = run_bridge_command("spawn_actor_by_class", spawn_params)
        if spawn.get("success", False):
            return _ok(
                f"Respawned '{name}' as {class_path}",
                actor_name=name,
                class_path=class_path,
                replaced_existing=spawn.get("replaced_existing"),
                spawn=spawn,
            )

        # Fallback path for older plugins without replace_existing / GC free
        delete = run_bridge_command("delete_actor", {"name": name})
        spawn2 = None
        for attempt in range(3):
            if attempt > 0:
                time.sleep(0.15 * attempt)
            spawn2 = run_bridge_command(
                "spawn_actor_by_class",
                {
                    "class_path": class_path,
                    "name": name,
                    "location": [float(x) for x in loc],
                    "rotation": [float(x) for x in rot],
                    "scale": [float(x) for x in scl],
                    "replace_existing": True,
                },
            )
            if spawn2.get("success", False):
                return _ok(
                    f"Respawned '{name}' via delete+spawn fallback (attempt {attempt + 1})",
                    actor_name=name,
                    class_path=class_path,
                    fallback=True,
                    attempts=attempt + 1,
                    delete=delete,
                    spawn=spawn2,
                )

        return _fail(
            f"respawn failed after retries: {(spawn2 or {}).get('message') or (spawn2 or {}).get('error')}",
            step="spawn_after_delete",
            first_spawn=spawn,
            delete=delete,
            spawn=spawn2,
            recovery_hint="Prefer a unique actor name, or upgrade plugin to handler_build 2026-07-25.4+",
        )

    @mcp.tool()
    def verify_after_mutate(
        ctx: Context,
        actor_name: str = "",
        take_screenshot: bool = True,
        screenshot_path: str = "",
        check_play_state: bool = True,
    ) -> Dict[str, Any]:
        """
        Read-only verification after a mutation (spawn/material/BP).

        Optionally captures a viewport screenshot and reports play/level state.
        Prefer this instead of chaining many get_* tools manually.
        """
        import tempfile
        from datetime import datetime
        from pathlib import Path

        level = run_bridge_command("get_level_status")
        viewport = run_bridge_command("get_viewport_status")
        play = None
        if check_play_state:
            play = run_bridge_command("get_play_state")

        actor = None
        actor_found = None
        if actor_name:
            actor = run_bridge_command("get_actor_properties", {"name": actor_name})
            actor_found = bool(actor.get("success", False))

        shot = None
        if take_screenshot:
            if screenshot_path:
                out = Path(screenshot_path).expanduser()
            else:
                stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                out = (
                    Path(tempfile.gettempdir())
                    / "unreal_mcp_screenshots"
                    / f"verify_{stamp}.png"
                )
            if not out.is_absolute():
                out = Path.cwd() / out
            if out.suffix.lower() != ".png":
                out = out.with_suffix(".png")
            out.parent.mkdir(parents=True, exist_ok=True)
            shot = run_bridge_command("take_screenshot", {"filepath": str(out)})
            if isinstance(shot, dict):
                shot.setdefault("filepath", str(out))

        issues: List[str] = []
        if not level.get("success", False):
            issues.append("get_level_status failed")
        if not viewport.get("success", False):
            issues.append("get_viewport_status failed")
        if actor_name and not actor_found:
            issues.append(f"actor '{actor_name}' not found")
        if take_screenshot and shot is not None and not shot.get("success", False):
            issues.append(f"screenshot failed: {shot.get('message') or shot.get('error')}")

        ok = len(issues) == 0
        return {
            "success": ok,
            "message": "Verification OK" if ok else "; ".join(issues),
            "error": None if ok else "; ".join(issues),
            "actor_name": actor_name or None,
            "actor_found": actor_found,
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
                or (viewport.get("editor") or {}).get("has_active_viewport"),
            },
            "play": play,
            "actor": actor,
            "screenshot": shot,
            "hints": [
                "Use verify_after_mutate after spawn/assign/rebuild to close the agent loop.",
                "Screenshot path is under TEMP/unreal_mcp_screenshots when not specified.",
            ],
        }

    logger.info("Workflow tools registered successfully")
