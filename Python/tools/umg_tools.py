"""
UMG Tools for Unreal MCP.

This module provides tools for creating and manipulating UMG Widget Blueprints in Unreal Engine.
"""

import logging
from typing import Dict, List, Any

from mcp.server.fastmcp import FastMCP, Context

from bridge_client import run_bridge_command

logger = logging.getLogger("UnrealMCP")


def register_umg_tools(mcp: FastMCP):
    """Register UMG tools with the MCP server."""

    @mcp.tool()
    def create_umg_widget_blueprint(
        ctx: Context,
        widget_name: str,
        parent_class: str = "UserWidget",
        path: str = "/Game/UI",
    ) -> Dict[str, Any]:
        """Create a new UMG Widget Blueprint."""
        return run_bridge_command(
            "create_umg_widget_blueprint",
            {
                "widget_name": widget_name,
                "parent_class": parent_class,
                "path": path,
            },
        )

    @mcp.tool()
    def add_text_block_to_widget(
        ctx: Context,
        widget_name: str,
        text_block_name: str,
        text: str = "",
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 50.0],
        font_size: int = 12,
        color: List[float] = [1.0, 1.0, 1.0, 1.0],
    ) -> Dict[str, Any]:
        """Add a Text Block widget to a UMG Widget Blueprint."""
        return run_bridge_command(
            "add_text_block_to_widget",
            {
                "widget_name": widget_name,
                "text_block_name": text_block_name,
                "text": text,
                "position": position,
                "size": size,
                "font_size": font_size,
                "color": color,
            },
        )

    @mcp.tool()
    def add_button_to_widget(
        ctx: Context,
        widget_name: str,
        button_name: str,
        text: str = "",
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 50.0],
        font_size: int = 12,
        color: List[float] = [1.0, 1.0, 1.0, 1.0],
        background_color: List[float] = [0.1, 0.1, 0.1, 1.0],
    ) -> Dict[str, Any]:
        """Add a Button widget to a UMG Widget Blueprint."""
        return run_bridge_command(
            "add_button_to_widget",
            {
                "widget_name": widget_name,
                "button_name": button_name,
                "text": text,
                "position": position,
                "size": size,
                "font_size": font_size,
                "color": color,
                "background_color": background_color,
            },
        )

    @mcp.tool()
    def bind_widget_event(
        ctx: Context,
        widget_name: str,
        widget_component_name: str,
        event_name: str,
        function_name: str = "",
    ) -> Dict[str, Any]:
        """Bind an event on a widget component to a function."""
        if not function_name:
            function_name = f"{widget_component_name}_{event_name}"
        return run_bridge_command(
            "bind_widget_event",
            {
                "widget_name": widget_name,
                "widget_component_name": widget_component_name,
                "event_name": event_name,
                "function_name": function_name,
            },
        )

    @mcp.tool()
    def add_widget_to_viewport(
        ctx: Context,
        widget_name: str,
        z_order: int = 0,
    ) -> Dict[str, Any]:
        """Add a Widget Blueprint instance to the viewport."""
        return run_bridge_command(
            "add_widget_to_viewport",
            {
                "widget_name": widget_name,
                "z_order": z_order,
            },
        )

    @mcp.tool()
    def set_text_block_binding(
        ctx: Context,
        widget_name: str,
        text_block_name: str,
        binding_property: str,
        binding_type: str = "Text",
    ) -> Dict[str, Any]:
        """Set up a property binding for a Text Block widget."""
        return run_bridge_command(
            "set_text_block_binding",
            {
                "widget_name": widget_name,
                "text_block_name": text_block_name,
                "binding_property": binding_property,
                "binding_type": binding_type,
            },
        )

    logger.info("UMG tools registered successfully")
