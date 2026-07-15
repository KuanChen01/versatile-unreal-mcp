"""
Editor Tools for Unreal MCP.

This module provides tools for controlling the Unreal Editor viewport and other editor functionality.
"""

import logging
import tempfile
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Any, Optional

from mcp.server.fastmcp import FastMCP, Context

from bridge_client import extract_list_field, run_bridge_command

logger = logging.getLogger("UnrealMCP")


def register_editor_tools(mcp: FastMCP):
    """Register editor tools with the MCP server."""

    @mcp.tool()
    def get_bridge_status(ctx: Context) -> Dict[str, Any]:
        """Report the live Unreal bridge status and routed command groups."""
        from unreal_mcp_server import SERVER_NAME, SERVER_VERSION

        normalized = run_bridge_command("get_bridge_status")
        if not normalized.get("success", False):
            return normalized

        normalized["server"] = {
            "name": SERVER_NAME,
            "version": SERVER_VERSION,
        }
        return normalized

    @mcp.tool()
    def get_viewport_status(ctx: Context) -> Dict[str, Any]:
        """Report active viewport availability, size, and self-check readiness."""
        return run_bridge_command("get_viewport_status")

    @mcp.tool()
    def get_level_status(ctx: Context) -> Dict[str, Any]:
        """Report the current editor world, map package path, dirty state, and actor count."""
        return run_bridge_command("get_level_status")

    @mcp.tool()
    def open_level(ctx: Context, level_path: str, save_dirty_packages: bool = False) -> Dict[str, Any]:
        """Open a level by /Game package path or absolute .umap path."""
        return run_bridge_command(
            "open_level",
            {
                "level_path": level_path,
                "save_dirty_packages": save_dirty_packages,
            },
        )

    @mcp.tool()
    def save_current_level(ctx: Context) -> Dict[str, Any]:
        """Save the current editor level without prompting."""
        return run_bridge_command("save_current_level")

    @mcp.tool()
    def save_dirty_packages(
        ctx: Context,
        save_map_packages: bool = True,
        save_content_packages: bool = True,
    ) -> Dict[str, Any]:
        """Save dirty map and/or content packages without prompting."""
        return run_bridge_command(
            "save_dirty_packages",
            {
                "save_map_packages": save_map_packages,
                "save_content_packages": save_content_packages,
            },
        )

    @mcp.tool()
    def get_play_state(ctx: Context) -> Dict[str, Any]:
        """Report whether PIE or SIE is active, queued, and which world is running."""
        return run_bridge_command("get_play_state")

    @mcp.tool()
    def start_pie(
        ctx: Context,
        simulate: bool = False,
        location: List[float] = None,
        rotation: List[float] = None,
    ) -> Dict[str, Any]:
        """Start a Play In Editor or Simulate In Editor session in the active viewport."""
        params: Dict[str, Any] = {"simulate": simulate}
        if location is not None:
            if not isinstance(location, list) or len(location) != 3:
                return {
                    "success": False,
                    "message": "Invalid location format. Must be a list of 3 float values.",
                    "error": "Invalid location format. Must be a list of 3 float values.",
                }
            params["location"] = [float(value) for value in location]
        if rotation is not None:
            if not isinstance(rotation, list) or len(rotation) != 3:
                return {
                    "success": False,
                    "message": "Invalid rotation format. Must be a list of 3 float values.",
                    "error": "Invalid rotation format. Must be a list of 3 float values.",
                }
            params["rotation"] = [float(value) for value in rotation]
        return run_bridge_command("start_pie", params)

    @mcp.tool()
    def stop_pie(ctx: Context) -> Dict[str, Any]:
        """Stop an active or queued Play In Editor session."""
        return run_bridge_command("stop_pie")

    @mcp.tool()
    def get_output_log(
        ctx: Context,
        max_entries: int = 200,
        contains: str = "",
        category: str = "",
        verbosity: str = "",
    ) -> Dict[str, Any]:
        """Return recent buffered Unreal output log entries."""
        return run_bridge_command(
            "get_output_log",
            {
                "max_entries": int(max_entries),
                "contains": contains,
                "category": category,
                "verbosity": verbosity,
            },
        )

    @mcp.tool()
    def get_message_log(
        ctx: Context,
        log_name: str = "PIE",
        max_entries: int = 100,
        contains: str = "",
        severity: str = "",
    ) -> Dict[str, Any]:
        """Return recent entries from a named Unreal Message Log listing such as PIE or LoadErrors."""
        return run_bridge_command(
            "get_message_log",
            {
                "log_name": log_name,
                "max_entries": int(max_entries),
                "contains": contains,
                "severity": severity,
            },
        )

    @mcp.tool()
    def get_actors_in_level(ctx: Context) -> List[Dict[str, Any]]:
        """Get a list of all actors in the current level."""
        normalized = run_bridge_command("get_actors_in_level")
        actors = extract_list_field(normalized, "actors")
        if actors:
            logger.info("Found %d actors in level", len(actors))
        elif not normalized.get("success", False):
            logger.warning("get_actors_in_level failed: %s", normalized.get("message"))
        return actors

    @mcp.tool()
    def find_actors_by_name(ctx: Context, pattern: str) -> List[str]:
        """Find actors by name pattern."""
        normalized = run_bridge_command("find_actors_by_name", {"pattern": pattern})
        return extract_list_field(normalized, "actors")

    @mcp.tool()
    def spawn_actor(
        ctx: Context,
        name: str,
        type: str,
        location: List[float] = [0.0, 0.0, 0.0],
        rotation: List[float] = [0.0, 0.0, 0.0],
    ) -> Dict[str, Any]:
        """Create a new actor in the current level (built-in types only)."""
        params = {
            "name": name,
            "type": type.upper(),
            "location": location,
            "rotation": rotation,
        }
        for param_name in ("location", "rotation"):
            param_value = params[param_name]
            if not isinstance(param_value, list) or len(param_value) != 3:
                return {
                    "success": False,
                    "message": f"Invalid {param_name} format. Must be a list of 3 float values.",
                    "error": f"Invalid {param_name} format. Must be a list of 3 float values.",
                }
            params[param_name] = [float(val) for val in param_value]
        return run_bridge_command("spawn_actor", params)

    @mcp.tool()
    def spawn_actor_by_class(
        ctx: Context,
        class_path: str,
        name: str = "",
        location: List[float] = [0.0, 0.0, 0.0],
        rotation: List[float] = [0.0, 0.0, 0.0],
        scale: List[float] = [1.0, 1.0, 1.0],
    ) -> Dict[str, Any]:
        """
        Spawn an actor from any Actor class or Blueprint.

        Args:
            class_path: Full path (/Script/Engine.PointLight, /Game/BP.BP_C)
                or short alias (PointLight, StaticMeshActor, CameraActor)
            name: Optional unique actor name
            location / rotation / scale: Transform
        """
        params: Dict[str, Any] = {
            "class_path": class_path,
            "location": location or [0.0, 0.0, 0.0],
            "rotation": rotation or [0.0, 0.0, 0.0],
            "scale": scale or [1.0, 1.0, 1.0],
        }
        if name:
            params["name"] = name
        for param_name in ("location", "rotation", "scale"):
            param_value = params[param_name]
            if not isinstance(param_value, list) or len(param_value) != 3:
                return {
                    "success": False,
                    "message": f"Invalid {param_name} format. Must be a list of 3 float values.",
                    "error": f"Invalid {param_name} format. Must be a list of 3 float values.",
                }
            params[param_name] = [float(val) for val in param_value]
        return run_bridge_command("spawn_actor_by_class", params)

    @mcp.tool()
    def assign_material_to_actor(
        ctx: Context,
        actor_name: str,
        material_path: str,
        slot_index: int = 0,
        component_name: str = "",
        slot_name: str = "",
    ) -> Dict[str, Any]:
        """
        Assign a material to a mesh component on an actor in the level.

        Args:
            actor_name: Level actor name
            material_path: Material asset path (/Game/.../M_Name or object path)
            slot_index: Material element index (ignored if slot_name set)
            component_name: Optional mesh component name (default: first mesh)
            slot_name: Optional material slot name
        """
        params: Dict[str, Any] = {
            "actor_name": actor_name,
            "material_path": material_path,
            "slot_index": int(slot_index),
        }
        if component_name:
            params["component_name"] = component_name
        if slot_name:
            params["slot_name"] = slot_name
        return run_bridge_command("assign_material_to_actor", params)

    @mcp.tool()
    def delete_actor(ctx: Context, name: str) -> Dict[str, Any]:
        """DESTRUCTIVE: permanently delete a level actor by name."""
        return run_bridge_command("delete_actor", {"name": name})

    @mcp.tool()
    def set_actor_transform(
        ctx: Context,
        name: str,
        location: List[float] = None,
        rotation: List[float] = None,
        scale: List[float] = None,
    ) -> Dict[str, Any]:
        """Set the transform of an actor."""
        params: Dict[str, Any] = {"name": name}
        if location is not None:
            params["location"] = location
        if rotation is not None:
            params["rotation"] = rotation
        if scale is not None:
            params["scale"] = scale
        return run_bridge_command("set_actor_transform", params)

    @mcp.tool()
    def get_actor_properties(ctx: Context, name: str) -> Dict[str, Any]:
        """Get all properties of an actor."""
        return run_bridge_command("get_actor_properties", {"name": name})

    @mcp.tool()
    def set_actor_property(
        ctx: Context,
        name: str,
        property_name: str,
        property_value,
    ) -> Dict[str, Any]:
        """Set a property on an actor."""
        return run_bridge_command(
            "set_actor_property",
            {
                "name": name,
                "property_name": property_name,
                "property_value": property_value,
            },
        )

    @mcp.tool()
    def take_screenshot(ctx: Context, filepath: str = "") -> Dict[str, Any]:
        """Capture the active Unreal Editor viewport to a PNG file."""
        if filepath:
            output_path = Path(filepath).expanduser()
        else:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_path = (
                Path(tempfile.gettempdir())
                / "unreal_mcp_screenshots"
                / f"unreal_screenshot_{timestamp}.png"
            )

        if not output_path.is_absolute():
            output_path = Path.cwd() / output_path
        if output_path.suffix.lower() != ".png":
            output_path = output_path.with_suffix(".png")
        output_path.parent.mkdir(parents=True, exist_ok=True)

        normalized = run_bridge_command(
            "take_screenshot",
            {"filepath": str(output_path)},
        )
        normalized.setdefault("filepath", str(output_path))
        return normalized

    @mcp.tool()
    def focus_viewport(
        ctx: Context,
        target: str = None,
        location: List[float] = None,
        distance: float = 1000.0,
        orientation: List[float] = None,
    ) -> Dict[str, Any]:
        """Focus the viewport on a specific actor or location."""
        if not target and location is None:
            return {
                "success": False,
                "message": "Either 'target' or 'location' must be provided",
                "error": "Either 'target' or 'location' must be provided",
            }

        if location is not None:
            if not isinstance(location, list) or len(location) != 3:
                return {
                    "success": False,
                    "message": "Invalid location format. Must be a list of 3 float values.",
                    "error": "Invalid location format. Must be a list of 3 float values.",
                }
            location = [float(value) for value in location]

        if orientation is not None:
            if not isinstance(orientation, list) or len(orientation) != 3:
                return {
                    "success": False,
                    "message": "Invalid orientation format. Must be a list of 3 float values.",
                    "error": "Invalid orientation format. Must be a list of 3 float values.",
                }
            orientation = [float(value) for value in orientation]

        params: Dict[str, Any] = {}
        if target:
            params["target"] = target
        elif location is not None:
            params["location"] = location
        if distance:
            params["distance"] = distance
        if orientation:
            params["orientation"] = orientation
        return run_bridge_command("focus_viewport", params)

    @mcp.tool()
    def spawn_blueprint_actor(
        ctx: Context,
        blueprint_name: str,
        actor_name: str,
        location: List[float] = [0.0, 0.0, 0.0],
        rotation: List[float] = [0.0, 0.0, 0.0],
    ) -> Dict[str, Any]:
        """Spawn an actor from a Blueprint."""
        params = {
            "blueprint_name": blueprint_name,
            "actor_name": actor_name,
            "location": location or [0.0, 0.0, 0.0],
            "rotation": rotation or [0.0, 0.0, 0.0],
        }
        for param_name in ("location", "rotation"):
            param_value = params[param_name]
            if not isinstance(param_value, list) or len(param_value) != 3:
                return {
                    "success": False,
                    "message": f"Invalid {param_name} format. Must be a list of 3 float values.",
                    "error": f"Invalid {param_name} format. Must be a list of 3 float values.",
                }
            params[param_name] = [float(val) for val in param_value]
        return run_bridge_command("spawn_blueprint_actor", params)

    logger.info("Editor tools registered successfully")
