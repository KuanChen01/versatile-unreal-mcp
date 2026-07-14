"""
Project Tools for Unreal MCP.

This module provides tools for managing project-wide settings, assets, and configuration.
"""

import logging
from typing import Dict, Any

from mcp.server.fastmcp import FastMCP, Context

from bridge_client import run_bridge_command

logger = logging.getLogger("UnrealMCP")


def register_project_tools(mcp: FastMCP):
    """Register project tools with the MCP server."""

    @mcp.tool()
    def create_input_mapping(
        ctx: Context,
        action_name: str,
        key: str,
        input_type: str = "Action",
    ) -> Dict[str, Any]:
        """
        Create an input mapping for the project.

        Args:
            action_name: Name of the input action
            key: Key to bind (SpaceBar, LeftMouseButton, etc.)
            input_type: Type of input mapping (Action or Axis)

        Returns:
            Response indicating success or failure
        """
        logger.info("Creating input mapping '%s' with key '%s'", action_name, key)
        return run_bridge_command(
            "create_input_mapping",
            {
                "action_name": action_name,
                "key": key,
                "input_type": input_type,
            },
        )

    @mcp.tool()
    def find_assets(
        ctx: Context,
        path: str = "/Game",
        query: str = "",
        class_name: str = "",
        max_results: int = 100,
        recursive: bool = True,
    ) -> Dict[str, Any]:
        """
        Search Content Browser assets via the Asset Registry.

        Args:
            path: Content path root (default /Game)
            query: Optional substring match on asset name or path
            class_name: Optional class filter — short alias (Material, StaticMesh,
                Blueprint, Texture2D, ...) or full path (/Script/Engine.Material)
            max_results: Cap results (1–2000, default 100)
            recursive: Search subfolders under path
        """
        return run_bridge_command(
            "find_assets",
            {
                "path": path,
                "query": query,
                "class_name": class_name,
                "max_results": int(max_results),
                "recursive": recursive,
            },
        )

    @mcp.tool()
    def get_asset_info(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """
        Get registry info for one asset.

        Args:
            asset_path: Package path (/Game/Foo/Bar) or object path (/Game/Foo/Bar.Bar)
        """
        return run_bridge_command("get_asset_info", {"asset_path": asset_path})

    @mcp.tool()
    def delete_asset(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """
        Delete a content asset by package path.

        Args:
            asset_path: Package path such as /Game/MCP_Smoke/M_Smoke_Test
        """
        return run_bridge_command("delete_asset", {"asset_path": asset_path})

    logger.info("Project tools registered successfully")
