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
        bridge = run_bridge_command("get_bridge_status")
        if not bridge.get("success", False):
            return _fail(
                "Bridge preflight failed — is Unreal open with UnrealMCP and protocol 2.0?",
                bridge=bridge,
                ready=False,
            )

        level = run_bridge_command("get_level_status")
        viewport = run_bridge_command("get_viewport_status")

        protocol = bridge.get("protocol_version")
        editor = bridge.get("editor") or {}
        ready = bool(
            protocol == "2.0"
            and editor.get("connected")
            and level.get("success", False)
        )

        issues: List[str] = []
        if protocol != "2.0":
            issues.append(f"protocol_version is {protocol!r}, expected '2.0'")
        if not editor.get("connected"):
            issues.append("editor.connected is not true")
        if not level.get("success", False):
            issues.append("get_level_status failed")
        if not viewport.get("success", False):
            issues.append("get_viewport_status failed (viewport may be unavailable)")

        return {
            "success": ready,
            "ready": ready,
            "message": "Editor ready for mutations" if ready else "; ".join(issues) or "Not ready",
            "error": None if ready else ("; ".join(issues) or "Not ready"),
            "protocol_version": protocol,
            "listen": bridge.get("listen"),
            "plugin": bridge.get("plugin"),
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
                "Use unique actor names; never respawn with a name still in the level.",
                "Prefer rebuild_material_graph / rebuild_blueprint_graph over many incremental node tools.",
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

    logger.info("Workflow tools registered successfully")
