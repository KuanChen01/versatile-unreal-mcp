# UnrealMCP Precompiled Packages

This directory contains precompiled `BuildPlugin` packages for Windows `Win64` Unreal Editor targets.

**Bridge protocol:** `2.0` (little-endian `uint32` length-prefix + UTF-8 JSON).  
These packages **must** be used with the matching Python server from this repo (`Python/`, `PROTOCOL_VERSION = "2.0"`).

Available engine versions:

- `UE_5.5`
- `UE_5.6`
- `UE_5.7`

Artifacts:

- `UnrealMCP-UE_5.5-Win64.zip`
- `UnrealMCP-UE_5.6-Win64.zip`
- `UnrealMCP-UE_5.7-Win64.zip`

Checksums: see `SHA256SUMS.txt`.

## Package contents (this refresh)

Includes plugin features as of **2026-07-15** (HEAD at rebuild, includes `bf4cba6` safety fixes):

- Protocol 2.0 framing + hard handshake surface
- P1 tools: `find_assets`, `get_asset_info`, `delete_asset`, `spawn_actor_by_class`, `assign_material_to_actor`
- Multi-instance env: `UNREAL_MCP_HOST` / `UNREAL_MCP_PORT`
- Blueprint atomic graph: `rebuild_blueprint_graph`, `batch_connect_blueprint_nodes`
- graph_spec expand: `branch`, `cast`, `custom_event`, `timeline` + pin aliases
- **Spawn safety:** duplicate actor names return JSON errors (do not Fatal the Editor)
- **Delete:** uses `EditorDestroyActor` so names can be reused more reliably
- **Properties:** `FVector` / `FRotator` component property writes (e.g. `RotationRate`)

## Manual install

1. Pick the zip that matches your Unreal Engine version.
2. Extract the `UnrealMCP` folder into your project's `Plugins` directory (replace any older copy).
3. Open the project in Unreal Editor. If `UnrealMCP` is not already enabled, enable it and restart once.
4. Start the shared Python MCP server from this repository's `Python` directory:

```powershell
uv sync
uv run unreal_mcp_server.py
```

The Unreal plugin listens on `127.0.0.1:55557` by default. The Python server speaks protocol **2.0** and hard-handshakes on connect.

## Verify after install

With the Editor open, from this repo's `Python/` directory:

```powershell
uv run python -c "from bridge_client import run_bridge_command; print(run_bridge_command('get_bridge_status'))"
```

Expect `protocol_version: "2.0"` and `success: true`.

## Rebuild notes

Packages were produced with Epic `RunUAT BuildPlugin` from `MCPGameProject/Plugins/UnrealMCP`, Win64 Editor Development, against installed Unreal Engine `5.5` / `5.6` / `5.7` builds.

Each zip root is `UnrealMCP/` (`Binaries/Win64` + `Source` + `.uplugin`).

Last refreshed: **2026-07-15** (P1 + graph expand + spawn/property safety).
