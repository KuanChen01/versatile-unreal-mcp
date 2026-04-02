"""
Material Tools for Unreal MCP.

This module provides tools for creating and editing Unreal Engine materials.
"""

import logging
from typing import Dict, Any, List
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_material_tools(mcp: FastMCP):
    """Register material tools with the MCP server."""

    def _send_material_command(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            logger.error("Failed to connect to Unreal Engine")
            return {"success": False, "message": "Failed to connect to Unreal Engine"}

        response = unreal.send_command(command, params)
        if not response:
            logger.error("No response from Unreal Engine")
            return {"success": False, "message": "No response from Unreal Engine"}

        logger.info("Material command %s response: %s", command, response)
        return response

    @mcp.tool()
    def create_material(
        ctx: Context,
        material_path: str
    ) -> Dict[str, Any]:
        """Create a new material asset at a /Game path."""
        return _send_material_command("create_material", {"material_path": material_path})

    @mcp.tool()
    def set_material_properties(
        ctx: Context,
        material_path: str,
        properties: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Set high-level properties on a material asset."""
        return _send_material_command("set_material_properties", {
            "material_path": material_path,
            "properties": properties,
        })

    @mcp.tool()
    def add_material_expression(
        ctx: Context,
        material_path: str,
        expression_type: str,
        expression_name: str = "",
        position: List[float] = [],
        properties: Dict[str, Any] = {},
        selected_asset: str = ""
    ) -> Dict[str, Any]:
        """Add a material expression node to a material graph."""
        params: Dict[str, Any] = {
            "material_path": material_path,
            "expression_type": expression_type,
            "position": position or [0.0, 0.0],
        }

        if expression_name:
            params["expression_name"] = expression_name
        if properties:
            params["properties"] = properties
        if selected_asset:
            params["selected_asset"] = selected_asset

        return _send_material_command("add_material_expression", params)

    @mcp.tool()
    def set_material_expression_property(
        ctx: Context,
        material_path: str,
        expression: str,
        property_name: str = "",
        property_value=None,
        properties: Dict[str, Any] = {}
    ) -> Dict[str, Any]:
        """Set one or more properties on a material expression node."""
        params: Dict[str, Any] = {
            "material_path": material_path,
            "expression": expression,
        }

        if properties:
            params["properties"] = properties
        else:
            params["property_name"] = property_name
            params["property_value"] = property_value

        return _send_material_command("set_material_expression_property", params)

    @mcp.tool()
    def connect_material_expressions(
        ctx: Context,
        material_path: str,
        from_expression: str,
        to_expression: str,
        from_output_name: str = "",
        to_input_name: str = ""
    ) -> Dict[str, Any]:
        """Connect two material expressions."""
        params: Dict[str, Any] = {
            "material_path": material_path,
            "from_expression": from_expression,
            "to_expression": to_expression,
        }

        if from_output_name:
            params["from_output_name"] = from_output_name
        if to_input_name:
            params["to_input_name"] = to_input_name

        return _send_material_command("connect_material_expressions", params)

    @mcp.tool()
    def connect_material_property(
        ctx: Context,
        material_path: str,
        expression: str,
        property_name: str,
        from_output_name: str = ""
    ) -> Dict[str, Any]:
        """Connect a material expression output to a material root property."""
        params: Dict[str, Any] = {
            "material_path": material_path,
            "expression": expression,
            "property_name": property_name,
        }

        if from_output_name:
            params["from_output_name"] = from_output_name

        return _send_material_command("connect_material_property", params)

    @mcp.tool()
    def recompile_material(
        ctx: Context,
        material_path: str
    ) -> Dict[str, Any]:
        """Recompile and save a material asset."""
        return _send_material_command("recompile_material", {"material_path": material_path})

    @mcp.tool()
    def configure_glass_material(
        ctx: Context,
        material_path: str,
        tint: List[float] = [],
        roughness: float = 0.02,
        specular: float = 0.5,
        base_opacity: float = 0.08,
        edge_opacity: float = 0.24,
        ior: float = 1.52,
        fresnel_exponent: float = 5.0,
        fresnel_base_reflect_fraction: float = 0.04,
        two_sided: bool = True
    ) -> Dict[str, Any]:
        """Build a physically plausible glass material preset in the target material asset."""
        params: Dict[str, Any] = {
            "material_path": material_path,
            "roughness": float(roughness),
            "specular": float(specular),
            "base_opacity": float(base_opacity),
            "edge_opacity": float(edge_opacity),
            "ior": float(ior),
            "fresnel_exponent": float(fresnel_exponent),
            "fresnel_base_reflect_fraction": float(fresnel_base_reflect_fraction),
            "two_sided": two_sided,
        }

        if tint:
            params["tint"] = [float(value) for value in tint]

        return _send_material_command("configure_glass_material", params)

    logger.info("Material tools registered successfully")
