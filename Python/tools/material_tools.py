"""
Material Tools for Unreal MCP.

This module provides tools for creating and editing Unreal Engine materials.
"""

import logging
from typing import Dict, Any, List, Optional
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_material_tools(mcp: FastMCP):
    """Register material tools with the MCP server."""

    def _send_material_command(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
        from bridge_client import run_bridge_command

        response = run_bridge_command(command, params)
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
        position: Optional[List[float]] = None,
        properties: Optional[Dict[str, Any]] = None,
        selected_asset: str = "",
        function_path: str = "",
        defer_compile: bool = True,
        defer_save: bool = True
    ) -> Dict[str, Any]:
        """Add a material expression node to a material graph."""
        params: Dict[str, Any] = {
            "material_path": material_path,
            "expression_type": expression_type,
            "position": position or [0.0, 0.0],
            "defer_compile": defer_compile,
            "defer_save": defer_save,
        }

        if expression_name:
            params["expression_name"] = expression_name
        if properties:
            params["properties"] = properties
        if selected_asset:
            params["selected_asset"] = selected_asset
        if function_path:
            params["function_path"] = function_path

        return _send_material_command("add_material_expression", params)

    @mcp.tool()
    def set_material_expression_property(
        ctx: Context,
        material_path: str,
        expression: str,
        property_name: str = "",
        property_value=None,
        properties: Optional[Dict[str, Any]] = None,
        expression_ref: Optional[Dict[str, Any]] = None,
        defer_compile: bool = True,
        defer_save: bool = True
    ) -> Dict[str, Any]:
        """Set one or more properties on a material expression node."""
        params: Dict[str, Any] = {
            "material_path": material_path,
            "defer_compile": defer_compile,
            "defer_save": defer_save,
        }

        if expression_ref:
            params["expression_ref"] = expression_ref
        else:
            params["expression"] = expression

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
        to_input_name: str = "",
        source_ref: Optional[Dict[str, Any]] = None,
        target_ref: Optional[Dict[str, Any]] = None,
        defer_compile: bool = True,
        defer_save: bool = True
    ) -> Dict[str, Any]:
        """Connect two material expressions."""
        params: Dict[str, Any] = {
            "material_path": material_path,
            "defer_compile": defer_compile,
            "defer_save": defer_save,
        }

        if source_ref:
            params["source_ref"] = source_ref
        else:
            params["from_expression"] = from_expression
        if target_ref:
            params["target_ref"] = target_ref
        else:
            params["to_expression"] = to_expression

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
        from_output_name: str = "",
        expression_ref: Optional[Dict[str, Any]] = None,
        defer_compile: bool = True,
        defer_save: bool = True
    ) -> Dict[str, Any]:
        """Connect a material expression output to a material root property."""
        params: Dict[str, Any] = {
            "material_path": material_path,
            "property_name": property_name,
            "defer_compile": defer_compile,
            "defer_save": defer_save,
        }

        if expression_ref:
            params["expression_ref"] = expression_ref
        else:
            params["expression"] = expression

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
    def rebuild_material_graph(
        ctx: Context,
        material_path: str,
        graph_spec: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Atomically rebuild a material graph from a declarative graph spec."""
        return _send_material_command("rebuild_material_graph", {
            "material_path": material_path,
            "graph_spec": graph_spec,
        })

    @mcp.tool()
    def get_material_compile_status(
        ctx: Context,
        material_path: str
    ) -> Dict[str, Any]:
        """Return shader compile errors, error nodes, and material statistics."""
        return _send_material_command("get_material_compile_status", {
            "material_path": material_path,
        })

    @mcp.tool()
    def validate_material_graph(
        ctx: Context,
        material_path: str
    ) -> Dict[str, Any]:
        """Validate material graph connectivity, root outputs, ComponentMask nodes, and compile errors."""
        return _send_material_command("validate_material_graph", {
            "material_path": material_path,
        })

    @mcp.tool()
    def reload_asset_from_disk(
        ctx: Context,
        asset_path: str,
        close_editors: bool = False,
        fail_if_dirty: bool = True
    ) -> Dict[str, Any]:
        """Reload an asset package from disk, optionally closing open asset editors first."""
        return _send_material_command("reload_asset_from_disk", {
            "asset_path": asset_path,
            "close_editors": close_editors,
            "fail_if_dirty": fail_if_dirty,
        })

    @mcp.tool()
    def close_asset_editor(
        ctx: Context,
        asset_path: str
    ) -> Dict[str, Any]:
        """Close all open Unreal asset editors for the supplied asset."""
        return _send_material_command("close_asset_editor", {
            "asset_path": asset_path,
        })

    @mcp.tool()
    def is_asset_loaded_dirty(
        ctx: Context,
        asset_path: str
    ) -> Dict[str, Any]:
        """Report whether an asset is loaded, dirty, and open in asset editors."""
        return _send_material_command("is_asset_loaded_dirty", {
            "asset_path": asset_path,
        })

    @mcp.tool()
    def create_material_function(
        ctx: Context,
        function_path: str
    ) -> Dict[str, Any]:
        """Create a new Material Function asset at a /Game path."""
        return _send_material_command("create_material_function", {
            "function_path": function_path,
        })

    @mcp.tool()
    def rebuild_material_function_graph(
        ctx: Context,
        function_path: str,
        graph_spec: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Rebuild a Material Function expression graph from a declarative graph spec."""
        return _send_material_command("rebuild_material_function_graph", {
            "function_path": function_path,
            "graph_spec": graph_spec,
        })

    @mcp.tool()
    def configure_glass_material(
        ctx: Context,
        material_path: str,
        tint: Optional[List[float]] = None,
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
