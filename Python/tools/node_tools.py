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

    @mcp.tool()
    def rebuild_blueprint_graph(
        ctx: Context,
        blueprint_name: str,
        graph_spec: Dict[str, Any],
        clear_event_graph: bool = False,
        compile: bool = True,
    ) -> Dict[str, Any]:
        """
        Atomically author a Blueprint Event Graph from a declarative graph_spec (v1).

        Prefer this over many add_*/connect_* calls. Local node ids in the spec are
        mapped to real NodeGuid values in the response (id_to_node_id).

        graph_spec shape::

            {
              "version": 1,
              "options": {"clear_event_graph": false, "compile": true},
              "variables": [{"name": "Speed", "type": "Float", "is_exposed": false}],
              "nodes": [
                {"id": "begin", "type": "event", "event_name": "ReceiveBeginPlay", "position": [0, 0]},
                {"id": "print", "type": "function", "target": "KismetSystemLibrary",
                 "function_name": "PrintString", "position": [300, 0],
                 "params": {"InString": "Hello"}}
              ],
              "connections": [
                {"from": {"node": "begin", "pin": "then"}, "to": {"node": "print", "pin": "execute"}}
              ]
            }

        Supported node types: event, function, self, input_action, get_component,
        variable_get, variable_set.
        """
        params: Dict[str, Any] = {
            "blueprint_name": blueprint_name,
            "graph_spec": graph_spec,
            "clear_event_graph": clear_event_graph,
            "compile": compile,
        }
        return run_bridge_command("rebuild_blueprint_graph", params)

    @mcp.tool()
    def batch_connect_blueprint_nodes(
        ctx: Context,
        blueprint_name: str,
        connections: list,
        id_to_node_id: Dict[str, str] = None,
    ) -> Dict[str, Any]:
        """
        Batch-connect nodes by local id (with id_to_node_id map) or by NodeGuid.

        Each connection may use::

            {"from": {"node": "...", "pin": "then"}, "to": {"node": "...", "pin": "execute"}}

        or the flat shape from connect_blueprint_nodes.
        """
        params: Dict[str, Any] = {
            "blueprint_name": blueprint_name,
            "connections": connections or [],
        }
        if id_to_node_id:
            params["id_to_node_id"] = id_to_node_id
        return run_bridge_command("batch_connect_blueprint_nodes", params)

    logger.info("Blueprint node tools registered successfully")
