"""
Blueprint Node Tools for Unreal MCP.

This module provides tools for manipulating Blueprint graph nodes and connections.
"""

import logging
from typing import Dict, Any

from mcp.server.fastmcp import FastMCP, Context

from bridge_client import run_bridge_command

logger = logging.getLogger("UnrealMCP")


def register_blueprint_node_tools(mcp: FastMCP):
    """Register Blueprint node manipulation tools with the MCP server."""

    @mcp.tool()
    def add_blueprint_event_node(
        ctx: Context,
        blueprint_name: str,
        event_name: str,
        node_position=None,
    ) -> Dict[str, Any]:
        """
        Add an event node to a Blueprint's event graph.

        Args:
            blueprint_name: Name of the target Blueprint
            event_name: Name of the event (e.g. ReceiveBeginPlay, ReceiveTick)
            node_position: Optional [X, Y] position in the graph
        """
        if node_position is None:
            node_position = [0, 0]
        return run_bridge_command(
            "add_blueprint_event_node",
            {
                "blueprint_name": blueprint_name,
                "event_name": event_name,
                "node_position": node_position,
            },
        )

    @mcp.tool()
    def add_blueprint_input_action_node(
        ctx: Context,
        blueprint_name: str,
        action_name: str,
        node_position=None,
    ) -> Dict[str, Any]:
        """Add an input action event node to a Blueprint's event graph."""
        if node_position is None:
            node_position = [0, 0]
        return run_bridge_command(
            "add_blueprint_input_action_node",
            {
                "blueprint_name": blueprint_name,
                "action_name": action_name,
                "node_position": node_position,
            },
        )

    @mcp.tool()
    def add_blueprint_function_node(
        ctx: Context,
        blueprint_name: str,
        target: str,
        function_name: str,
        params=None,
        node_position=None,
    ) -> Dict[str, Any]:
        """Add a function call node to a Blueprint's event graph."""
        if params is None:
            params = {}
        if node_position is None:
            node_position = [0, 0]
        return run_bridge_command(
            "add_blueprint_function_node",
            {
                "blueprint_name": blueprint_name,
                "target": target,
                "function_name": function_name,
                "params": params,
                "node_position": node_position,
            },
        )

    @mcp.tool()
    def connect_blueprint_nodes(
        ctx: Context,
        blueprint_name: str,
        source_node_id: str,
        source_pin: str,
        target_node_id: str,
        target_pin: str,
    ) -> Dict[str, Any]:
        """Connect two nodes in a Blueprint's event graph."""
        return run_bridge_command(
            "connect_blueprint_nodes",
            {
                "blueprint_name": blueprint_name,
                "source_node_id": source_node_id,
                "source_pin": source_pin,
                "target_node_id": target_node_id,
                "target_pin": target_pin,
            },
        )

    @mcp.tool()
    def add_blueprint_variable(
        ctx: Context,
        blueprint_name: str,
        variable_name: str,
        variable_type: str,
        is_exposed: bool = False,
    ) -> Dict[str, Any]:
        """Add a variable to a Blueprint."""
        return run_bridge_command(
            "add_blueprint_variable",
            {
                "blueprint_name": blueprint_name,
                "variable_name": variable_name,
                "variable_type": variable_type,
                "is_exposed": is_exposed,
            },
        )

    @mcp.tool()
    def add_blueprint_get_self_component_reference(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        node_position=None,
    ) -> Dict[str, Any]:
        """Add a node that gets a reference to a component owned by the current Blueprint."""
        if node_position is None:
            node_position = [0, 0]
        return run_bridge_command(
            "add_blueprint_get_self_component_reference",
            {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "node_position": node_position,
            },
        )

    @mcp.tool()
    def add_blueprint_self_reference(
        ctx: Context,
        blueprint_name: str,
        node_position=None,
    ) -> Dict[str, Any]:
        """Add a 'Get Self' node to a Blueprint's event graph."""
        if node_position is None:
            node_position = [0, 0]
        return run_bridge_command(
            "add_blueprint_self_reference",
            {
                "blueprint_name": blueprint_name,
                "node_position": node_position,
            },
        )

    @mcp.tool()
    def find_blueprint_nodes(
        ctx: Context,
        blueprint_name: str,
        node_type=None,
        event_type=None,
    ) -> Dict[str, Any]:
        """Find nodes in a Blueprint's event graph."""
        return run_bridge_command(
            "find_blueprint_nodes",
            {
                "blueprint_name": blueprint_name,
                "node_type": node_type,
                "event_type": event_type,
            },
        )

    logger.info("Blueprint node tools registered successfully")
