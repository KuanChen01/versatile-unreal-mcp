"""
Blueprint Tools for Unreal MCP.

This module provides tools for creating and manipulating Blueprint assets in Unreal Engine.
"""

import logging
from typing import Dict, List, Any

from mcp.server.fastmcp import FastMCP, Context

from bridge_client import run_bridge_command

logger = logging.getLogger("UnrealMCP")


def register_blueprint_tools(mcp: FastMCP):
    """Register Blueprint tools with the MCP server."""

    @mcp.tool()
    def create_blueprint(
        ctx: Context,
        name: str,
        parent_class: str,
    ) -> Dict[str, Any]:
        """Create a new Blueprint class."""
        return run_bridge_command(
            "create_blueprint",
            {
                "name": name,
                "parent_class": parent_class,
            },
        )

    @mcp.tool()
    def add_component_to_blueprint(
        ctx: Context,
        blueprint_name: str,
        component_type: str,
        component_name: str,
        location: List[float] = [],
        rotation: List[float] = [],
        scale: List[float] = [],
        component_properties: Dict[str, Any] = {},
    ) -> Dict[str, Any]:
        """
        Add a component to a Blueprint.

        Args:
            blueprint_name: Name of the target Blueprint
            component_type: Type of component to add (use component class name without U prefix)
            component_name: Name for the new component
            location: [X, Y, Z] coordinates for component's position
            rotation: [Pitch, Yaw, Roll] values for component's rotation
            scale: [X, Y, Z] values for component's scale
            component_properties: Additional properties to set on the component
        """
        params = {
            "blueprint_name": blueprint_name,
            "component_type": component_type,
            "component_name": component_name,
            "location": location or [0.0, 0.0, 0.0],
            "rotation": rotation or [0.0, 0.0, 0.0],
            "scale": scale or [1.0, 1.0, 1.0],
        }
        if component_properties:
            params["component_properties"] = component_properties

        for param_name in ("location", "rotation", "scale"):
            param_value = params[param_name]
            if not isinstance(param_value, list) or len(param_value) != 3:
                return {
                    "success": False,
                    "message": f"Invalid {param_name} format. Must be a list of 3 float values.",
                    "error": f"Invalid {param_name} format. Must be a list of 3 float values.",
                }
            params[param_name] = [float(val) for val in param_value]

        return run_bridge_command("add_component_to_blueprint", params)

    @mcp.tool()
    def set_static_mesh_properties(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        static_mesh: str = "/Engine/BasicShapes/Cube.Cube",
    ) -> Dict[str, Any]:
        """Set static mesh properties on a StaticMeshComponent."""
        return run_bridge_command(
            "set_static_mesh_properties",
            {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "static_mesh": static_mesh,
            },
        )

    @mcp.tool()
    def set_component_property(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        property_name: str,
        property_value,
    ) -> Dict[str, Any]:
        """Set a property on a component in a Blueprint."""
        return run_bridge_command(
            "set_component_property",
            {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "property_name": property_name,
                "property_value": property_value,
            },
        )

    @mcp.tool()
    def set_physics_properties(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        simulate_physics: bool = True,
        gravity_enabled: bool = True,
        mass: float = 1.0,
        linear_damping: float = 0.01,
        angular_damping: float = 0.0,
    ) -> Dict[str, Any]:
        """Set physics properties on a component."""
        return run_bridge_command(
            "set_physics_properties",
            {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "simulate_physics": simulate_physics,
                "gravity_enabled": gravity_enabled,
                "mass": float(mass),
                "linear_damping": float(linear_damping),
                "angular_damping": float(angular_damping),
            },
        )

    @mcp.tool()
    def compile_blueprint(
        ctx: Context,
        blueprint_name: str,
    ) -> Dict[str, Any]:
        """Compile a Blueprint."""
        return run_bridge_command(
            "compile_blueprint",
            {"blueprint_name": blueprint_name},
        )

    @mcp.tool()
    def set_blueprint_property(
        ctx: Context,
        blueprint_name: str,
        property_name: str,
        property_value,
    ) -> Dict[str, Any]:
        """Set a property on a Blueprint class default object."""
        return run_bridge_command(
            "set_blueprint_property",
            {
                "blueprint_name": blueprint_name,
                "property_name": property_name,
                "property_value": property_value,
            },
        )

    # Not registered as MCP tool; kept as utility. Prefer set_component_property / set_blueprint_property.
    def set_pawn_properties(
        ctx: Context,
        blueprint_name: str,
        auto_possess_player: str = "",
        use_controller_rotation_yaw: bool = None,
        use_controller_rotation_pitch: bool = None,
        use_controller_rotation_roll: bool = None,
        can_be_damaged: bool = None,
    ) -> Dict[str, Any]:
        """Set common Pawn properties on a Blueprint."""
        properties = {}
        if auto_possess_player:
            properties["auto_possess_player"] = auto_possess_player
        if use_controller_rotation_yaw is not None:
            properties["bUseControllerRotationYaw"] = use_controller_rotation_yaw
        if use_controller_rotation_pitch is not None:
            properties["bUseControllerRotationPitch"] = use_controller_rotation_pitch
        if use_controller_rotation_roll is not None:
            properties["bUseControllerRotationRoll"] = use_controller_rotation_roll
        if can_be_damaged is not None:
            properties["bCanBeDamaged"] = can_be_damaged

        if not properties:
            return {"success": True, "message": "No properties specified to set", "results": {}}

        results = {}
        overall_success = True
        for prop_name, prop_value in properties.items():
            response = run_bridge_command(
                "set_blueprint_property",
                {
                    "blueprint_name": blueprint_name,
                    "property_name": prop_name,
                    "property_value": prop_value,
                },
            )
            results[prop_name] = response
            if not response.get("success", False):
                overall_success = False

        return {
            "success": overall_success,
            "message": "Pawn properties set" if overall_success else "Some pawn properties failed to set",
            "results": results,
        }

    logger.info("Blueprint tools registered successfully")
