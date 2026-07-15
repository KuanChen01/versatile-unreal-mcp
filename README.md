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
| Actor Management | List/find actors, spawn built-in or **by class/Blueprint**, delete, transform, properties, **assign material to mesh slots** |
| Asset Registry | **find_assets**, get_asset_info, delete_asset (Content Browser search) |
| Blueprint Authoring | Create Blueprints, add components, set mesh and physics properties, compile, set defaults, spawn Blueprint actors |
| Blueprint Graph Editing | Incremental node tools **plus** atomic `rebuild_blueprint_graph` / `batch_connect_blueprint_nodes` (local ids, variables, compile) |
| Material Authoring | Create materials, rebuild material graphs atomically, use stable node refs, validate/compile graphs, manage asset cache state, create/reuse Material Functions, build glass/chrome workflows |
| UMG Authoring | Create widget Blueprints, add text blocks and buttons, bind widget events, add widgets to viewport |
| Editor Utilities | Inspect live bridge/editor status, manage levels and play sessions, query viewport readiness, capture logs, focus the viewport, and take screenshots |

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
4. Start the editor, then run the Python MCP server from the repository's [Python](Python) directory.

### Option B: Reuse in your own Unreal project

**Preferred:** use the sync script (avoids stale manual copies):

```powershell
# From the repository root — Source mode (then rebuild Editor):
.\Scripts\sync_unreal_mcp_plugin.ps1 -TargetProject "C:\Path\To\YourGame.uproject" -Mode Source

# Or install a matching prebuilt package:
.\Scripts\sync_unreal_mcp_plugin.ps1 -TargetProject "C:\Path\To\YourGame.uproject" -Mode Release -EngineVersion 5.7

# Drift report only:
.\Scripts\sync_unreal_mcp_plugin.ps1 -TargetProject "C:\Path\To\YourGame.uproject" -Mode Status
```

Details: [Docs/Tools/plugin_sync.md](Docs/Tools/plugin_sync.md).

Manual equivalent:

1. Copy [UnrealMCP](MCPGameProject/Plugins/UnrealMCP) into your Unreal project's `Plugins/` directory (or extract a zip from [Releases/UnrealMCP](Releases/UnrealMCP)).
2. Enable the plugin in Unreal Editor if it is not already enabled.
3. Generate project files for your `.uproject` (Source installs).
4. Build your Editor target so the plugin is compiled against that project (Source installs).
5. Run the shared Python MCP server from the repository's [Python](Python) directory.

This reuse model is the intended workflow. The plugin changes in this repository are not tied to any single project path or asset set. After pulling plugin changes, re-run the sync script (or re-copy), rebuild if needed, and **fully restart** the Editor.

## Python Server Setup

From [Python](Python):

```bash
uv sync
uv run unreal_mcp_server.py
```

The Unreal plugin listens on `127.0.0.1:55557` by default, and the Python server forwards MCP tool calls to the editor over that TCP bridge.

Multi-instance (optional): set the same `UNREAL_MCP_PORT` (and optionally `UNREAL_MCP_HOST`) environment variable for both the Unreal Editor process and the Python MCP server before launch.

### Bridge protocol 2.0 (breaking)

Python and the editor plugin speak **protocol 2.0**: each message is a little-endian `uint32` byte length followed by a UTF-8 JSON body. They must be upgraded **together**. An old plugin with a new Python server (or the reverse) will fail framing with an explicit upgrade hint.

- Constants: `Python/bridge_protocol.py` (`PROTOCOL_VERSION = "2.0"`) and plugin `UnrealMCPProtocolVersion`
- Offline framing tests: `uv run python tests/test_bridge_protocol.py` (from `Python/`)

## MCP Client Configuration

All clients eventually run the same Python entrypoint:

Replace the path below with the absolute path to this repository's `Python` directory.

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "uv",
      "args": [
        "--directory",
        "/absolute/path/to/versatile-unreal-mcp/Python",
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
args = ["--directory", "/absolute/path/to/versatile-unreal-mcp/Python", "run", "unreal_mcp_server.py"]
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
        "/absolute/path/to/versatile-unreal-mcp/Python",
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

This fork adds editor-side material creation and graph editing support. For non-trivial graphs, prefer `rebuild_material_graph` over incremental add/connect calls so the graph is rebuilt, validated, compiled, and saved as one unit.

Available material commands:

- `create_material`
- `set_material_properties`
- `add_material_expression`
- `set_material_expression_property`
- `connect_material_expressions`
- `connect_material_property`
- `recompile_material`
- `rebuild_material_graph`
- `get_material_compile_status`
- `validate_material_graph`
- `reload_asset_from_disk`
- `close_asset_editor`
- `is_asset_loaded_dirty`
- `create_material_function`
- `rebuild_material_function_graph`
- `configure_glass_material`

### Batch graph workflow

`rebuild_material_graph(material_path, graph_spec)` is the preferred API for complex material authoring. It can clear the old graph, create nodes, create comment boxes and reroute nodes, connect by local ids, group/layout the graph, compile, validate, and save.

The operation is designed to avoid saving partial products. The hard guarantee is that failures do not write the half-built graph to disk; the plugin also attempts to restore the in-memory Editor graph from a transient backup.

Minimal chrome-like example:

```json
{
  "version": 1,
  "material_properties": {
    "blend_mode": "Opaque",
    "shading_model": "DefaultLit",
    "two_sided": false
  },
  "groups": ["Surface"],
  "nodes": [
    {
      "id": "base_color",
      "type": "Constant3Vector",
      "label": "Chrome tint",
      "group": "Surface",
      "properties": { "Constant": [0.8, 0.82, 0.84, 1.0] }
    },
    {
      "id": "metallic",
      "type": "Constant",
      "label": "Metallic",
      "group": "Surface",
      "properties": { "R": 1.0 }
    },
    {
      "id": "roughness",
      "type": "Constant",
      "label": "Polished roughness",
      "group": "Surface",
      "properties": { "R": 0.08 }
    }
  ],
  "comments": [
    { "id": "surface_comment", "text": "Surface response", "group": "Surface" }
  ],
  "connections": [
    { "from": { "node": "base_color" }, "to": { "material_property": "BaseColor" } },
    { "from": { "node": "metallic" }, "to": { "material_property": "Metallic" } },
    { "from": { "node": "roughness" }, "to": { "material_property": "Roughness" } }
  ],
  "options": {
    "compile": true,
    "save": true,
    "validate_before_save": true
  }
}
```

Created expression responses include `object_path`, `expression_guid`, `stable_id`, `name`, `label`, `type`, and `position`. Incremental connect tools accept `source_ref`, `target_ref`, and `expression_ref` objects so callers can connect by `object_path` or `expression_guid` instead of relying on duplicate-prone labels.

Incremental `add_material_expression`, `set_material_expression_property`, `connect_material_expressions`, and `connect_material_property` default to `defer_compile=true` and `defer_save=true`. Call `recompile_material` after a batch, or pass `defer_compile=false` / `defer_save=false` when you explicitly want old step-by-step behavior.

### Validation and cache tools

- `get_material_compile_status` reports shader compile errors, error node refs, shader platform, and material statistics.
- `validate_material_graph` checks required expression inputs, root material output connections, illegal empty `ComponentMask` nodes, and compile errors.
- `is_asset_loaded_dirty`, `close_asset_editor`, and `reload_asset_from_disk` help avoid stale Editor-loaded assets overwriting externally changed `.uasset` files.
- `reload_asset_from_disk` refuses dirty packages by default; pass `fail_if_dirty=false` only when losing unsaved in-memory edits is acceptable.

### Material Functions

Use `create_material_function` and `rebuild_material_function_graph` to package reusable subgraphs. A material graph can reuse an existing function with a node like:

```json
{
  "id": "shared_falloff",
  "type": "MaterialFunctionCall",
  "function_path": "/Game/Materials/MF_EdgeFalloff",
  "group": "Reuse"
}
```

For large graphs, split the spec into semantic groups, add comment boxes, use reroute nodes where wires cross, and move repeated logic into Material Functions instead of creating a single dense node cluster.

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
- After pulling protocol 2.0 changes, rebuild/reload the `UnrealMCP` plugin in every target project before using the shared Python server. Prebuilt packages under `Releases/` must be regenerated from the updated plugin source before redistribution.

## Verification Performed

This fork has been exercised end-to-end with:

- plugin build success in Unreal Engine `5.7`
- Python MCP server registration and tool exposure
- Blueprint creation and spawning
- material creation through `create_material`
- glass graph generation through `configure_glass_material`
- assigning the generated material to a sphere in a test level
- live bridge checks via `get_bridge_status` / `ping` (protocol **2.0**) after install

With the Editor open and the plugin listening:

```bash
uv --directory Python run python -c "from bridge_client import run_bridge_command; print(run_bridge_command('get_bridge_status'))"
```

Expect `protocol_version: "2.0"` and `success: true`.

## Development Notes

- The Unreal plugin is the source of truth for editor-side behavior.
- The Python server is only the MCP-facing wrapper layer.
- When you add new plugin commands, remember to:
  - route them in `UnrealMCPBridge`
  - register a matching Python tool
  - document the capability in this README or `Docs`

## License

MIT
