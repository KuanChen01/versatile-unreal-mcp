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

## Manual install

1. Pick the zip that matches your Unreal Engine version.
2. Extract the `UnrealMCP` folder into your project's `Plugins` directory (replace any older copy).
3. Open the project in Unreal Editor. If `UnrealMCP` is not already enabled, enable it and restart once.
4. Start the shared Python MCP server from this repository's `Python` directory:

```powershell
uv sync
uv run unreal_mcp_server.py
```

The Unreal plugin listens on `127.0.0.1:55557`. The Python server speaks protocol **2.0** and hard-handshakes on connect.

## Verify after install

With the Editor open:

```powershell
uv --directory D:\MCP\Unreal\Python run python scripts/smoke/editor_smoke.py
```

Or at least:

```powershell
uv --directory D:\MCP\Unreal\Python run python -c "from bridge_client import run_bridge_command; print(run_bridge_command('get_bridge_status'))"
```

Expect `protocol_version: "2.0"` and `success: true`.

## Rebuild notes

Packages were produced with Epic `RunUAT BuildPlugin` from `MCPGameProject/Plugins/UnrealMCP` (protocol 2.0 source), Win64 Editor Development, engines installed under `D:\Software\Unreal\UE_5.5` / `UE_5.6` / `UE_5.7`.

Last refreshed: 2026-07-14.
