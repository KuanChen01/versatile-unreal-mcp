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

Includes plugin features as of **2026-08-11** (`handler_build` **2026-08-11.2** on UE 5.7 package):

- Protocol 2.0 framing + hard handshake surface
- P1 tools: `find_assets`, `get_asset_info`, `delete_asset`, `spawn_actor_by_class`, `assign_material_to_actor`
- Multi-instance env: `UNREAL_MCP_HOST` / `UNREAL_MCP_PORT`
- Blueprint atomic graph + graph_spec expand (branch/cast/custom_event/timeline)
- **Spawn safety:** duplicate actor names return JSON errors (do not Fatal the Editor)
- **Delete:** `EditorDestroyActor`
- **Properties:** `FVector` / `FRotator` component property writes
- **Correlation:** wire `request_id` + plugin `duration_ms` echo for multi-step debugging
- **Half-transaction / undo:** `begin_transaction` / `end_transaction` / `cancel_transaction` / undo-redo
- **Material pin resolve (5.7):** `connect_material_expressions` accepts `to_input_index` / `from_output_index` and reflected `FExpressionInput` property names (e.g. `Position` on DistanceToNearestSurface when the display name is dynamic World Position); expression JSON includes `inputs[]` / `outputs[]`
- **Transport:** clean client disconnect no longer logged as protocol-incompatible framing; session-friendly accept/idle; pair with Python session reuse + transient retry (10053/10054)

## Manual install

1. Pick the zip that matches your Unreal Engine version.
2. Extract the `UnrealMCP` folder into your project's `Plugins` directory (replace any older copy).
3. Prefer the repo sync script:

```powershell
.\Scripts\sync_unreal_mcp_plugin.ps1 -TargetProject "C:\Path\To\YourGame.uproject" -Mode Release -EngineVersion 5.7
```

4. Open the project in Unreal Editor. Enable UnrealMCP if needed and restart once.
5. Start the shared Python MCP server from this repository's `Python` directory:

```powershell
uv sync
uv run unreal_mcp_server.py
```

Default listen: `127.0.0.1:55557`.

## Verify after install

```powershell
uv --directory <repo>\Python run python -c "from bridge_client import run_bridge_command; print(run_bridge_command('get_bridge_status'))"
```

Expect `protocol_version: "2.0"` and `success: true`. On a matching Python server, check:

```text
plugin.handler_build == "2026-08-11.2"   # after installing the refreshed UE_5.7 zip
handler_build_mismatch == false
```

Tool responses include `meta.request_id` and timing fields when using a matching Python server.

## Rebuild notes

Packages produced with Epic `RunUAT BuildPlugin` from `MCPGameProject/Plugins/UnrealMCP`, Win64 Editor Development, engines `5.5` / `5.6` / `5.7`.

Zip root: `UnrealMCP/` (`Binaries/Win64` + `Source` + `.uplugin`).

- **UE 5.5 / 5.6 zips:** last full matrix rebuild **2026-07-16** (still usable for protocol 2.0; do not claim 2026-08-11 pin/transport stamps unless rebuilt).
- **UE 5.7 zip:** refreshed **2026-08-11** with material pin resolution + transport logging/session fixes (`handler_build` 2026-08-11.2).

Last refreshed: **2026-08-11** (UE 5.7 package + Python transport reuse/retry; 5.5/5.6 zips unchanged from 2026-07-16).
