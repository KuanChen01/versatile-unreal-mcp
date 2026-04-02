<div align="center">

# versatile-unreal-mcp
<span style="color: #555555">Model Context Protocol for Unreal Engine</span>

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.5%2B-orange)](https://www.unrealengine.com)
[![Validated](https://img.shields.io/badge/Validated-UE%205.7-green)](https://www.unrealengine.com)
[![Python](https://img.shields.io/badge/Python-3.12%2B-yellow)](https://www.python.org)
[![Status](https://img.shields.io/badge/Status-Experimental-red)](https://github.com/KuanChen01/versatile-unreal-mcp)

</div>

`versatile-unreal-mcp` lets MCP-capable AI clients control Unreal Editor with natural language. It provides a native Unreal plugin plus a Python MCP server, and is designed to be copied into existing Unreal projects instead of being tied to a single sample game.

Validated in practice on Unreal Engine `5.7`, while still targeting `5.5+`.

## Highlights

- Works with existing Unreal projects by copying `MCPGameProject/Plugins/UnrealMCP` into your own `Plugins/` folder
- Supports MCP clients such as `Codex`, `OpenCode`, `Claude Desktop`, `Cursor`, and `Windsurf`
- Covers editor automation, actor spawning, Blueprint creation, Blueprint node editing, UMG widget editing, and project input mappings
- Adds material authoring tools for editor workflows, including material creation, expression graph editing, and a ready-made glass material preset
- Keeps the Python MCP server separate from the Unreal project so one server setup can be reused across multiple Unreal projects

## Capability Overview

| Area | What it can do |
| --- | --- |
| Actor Management | List actors, find actors by name, spawn actors, delete actors, move and rotate actors, inspect actor properties |
| Blueprint Authoring | Create Blueprints, add components, set mesh and physics properties, compile, set defaults, spawn Blueprint actors |
| Blueprint Graph Editing | Add event nodes, input nodes, function nodes, self/component references, variables, and connect graph pins |
| Material Authoring | Create materials, edit material properties, add expressions, connect expressions, connect root material properties, recompile materials, build a realistic glass preset |
| UMG Authoring | Create widget Blueprints, add text blocks and buttons, bind widget events, add widgets to viewport |
| Editor Utilities | Focus the viewport and take screenshots |

## Repository Layout

- [MCPGameProject](MCPGameProject)
  Sample Unreal project containing the `UnrealMCP` plugin.
- [MCPGameProject/Plugins/UnrealMCP](MCPGameProject/Plugins/UnrealMCP)
  The Unreal Editor plugin that opens the TCP bridge and executes commands.
- [Python](Python)
  The Python MCP server and tool registration layer.
- [Docs](Docs)
  Supplemental documentation.

## Quick Start

### Prerequisites

- Unreal Engine `5.5+`
- Python `3.12+`
- [`uv`](https://docs.astral.sh/uv/)
- An MCP client such as `Codex`, `OpenCode`, `Claude Desktop`, `Cursor`, or `Windsurf`

### Option A: Use the sample project

1. Open [MCPGameProject](MCPGameProject).
2. Generate project files for the `.uproject`.
3. Build the `Development Editor` target.
4. Start the editor, then run the Python MCP server from [Python](D:\MCP\Unreal\Python).

### Option B: Reuse in your own Unreal project

1. Copy [UnrealMCP](MCPGameProject/Plugins/UnrealMCP) into your Unreal project's `Plugins/` directory.
2. Enable the plugin in Unreal Editor if it is not already enabled.
3. Generate project files for your `.uproject`.
4. Build your Editor target so the plugin is compiled against that project.
5. Run the shared Python MCP server from [Python](D:\MCP\Unreal\Python).

This reuse model is the intended workflow. The plugin changes in this repository are not tied to any single project path or asset set.

## Python Server Setup

From [Python](Python):

```bash
uv sync
uv run unreal_mcp_server.py
```

The Unreal plugin listens on `127.0.0.1:55557`, and the Python server forwards MCP tool calls to the editor over that TCP bridge.

## MCP Client Configuration

All clients eventually run the same Python entrypoint:

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "uv",
      "args": [
        "--directory",
        "D:/path/to/versatile-unreal-mcp/Python",
        "run",
        "unreal_mcp_server.py"
      ]
    }
  }
}
```

### Codex

Add this to `~/.codex/config.toml`:

```toml
[mcp_servers.unrealMCP]
command = "uv"
args = ["--directory", "D:\\path\\to\\versatile-unreal-mcp\\Python", "run", "unreal_mcp_server.py"]
```

### OpenCode

You can configure OpenCode globally in `~/.config/opencode/opencode.json` or per-project with `opencode.json` in the workspace root:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "unrealMCP": {
      "type": "local",
      "command": [
        "uv",
        "--directory",
        "D:/path/to/versatile-unreal-mcp/Python",
        "run",
        "unreal_mcp_server.py"
      ],
      "enabled": true
    }
  }
}
```

### Other clients

- Claude Desktop: `~/.config/claude-desktop/mcp.json`
- Cursor: `.cursor/mcp.json` in the workspace
- Windsurf: `~/.config/windsurf/mcp.json`

Use the same `uv --directory ... run unreal_mcp_server.py` command pattern.

## Material Tools

This fork adds editor-side material creation and graph editing support.

Available material commands:

- `create_material`
- `set_material_properties`
- `add_material_expression`
- `set_material_expression_property`
- `connect_material_expressions`
- `connect_material_property`
- `recompile_material`
- `configure_glass_material`

### Example workflows

- Create a new material asset under `/Game/Project/Test/M_Chrome`
- Build a reusable glass material in `/Game/Project/Test/Glass`
- Add scalar/vector expressions and wire them to `BaseColor`, `Opacity`, `Roughness`, `Specular`, or `Refraction`
- Recompile and save the material, then assign it to a mesh or Blueprint component

### Glass preset

`configure_glass_material` creates a practical glass graph using:

- `Translucent` blend mode
- `Default Lit` shading model
- `Surface ForwardShading` translucency lighting
- `Index Of Refraction` refraction mode
- Fresnel-driven opacity shaping
- Adjustable tint, roughness, specular, opacity, and IOR

This is meant for Unreal Editor authoring workflows and rapid iteration.

## Compatibility Notes

- The current material tools are intended for `Editor` workflows, not packaged runtime gameplay systems.
- This repository has been validated on `UE 5.7`.
- The plugin still targets `UE 5.5+`, but if Epic changes editor-only APIs again, additional compatibility fixes may be required.
- Unsaved Unreal assets created in-editor can still disappear after editor restart if the asset package or level is not saved.

## Verification Performed

This fork has been exercised end-to-end with:

- plugin build success in Unreal Engine `5.7`
- Python MCP server registration and tool exposure
- Blueprint creation and spawning
- material creation through `create_material`
- glass graph generation through `configure_glass_material`
- assigning the generated material to a sphere in a test level

## Development Notes

- The Unreal plugin is the source of truth for editor-side behavior.
- The Python server is only the MCP-facing wrapper layer.
- When you add new plugin commands, remember to:
  - route them in `UnrealMCPBridge`
  - register a matching Python tool
  - document the capability in this README or `Docs`

## License

MIT
