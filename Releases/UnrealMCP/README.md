# UnrealMCP Precompiled Packages

This directory contains precompiled `BuildPlugin` packages for Windows `Win64` Unreal Editor targets.

Available engine versions:

- `UE_5.5`
- `UE_5.6`
- `UE_5.7`

Artifacts:

- `UnrealMCP-UE_5.5-Win64.zip`
- `UnrealMCP-UE_5.6-Win64.zip`
- `UnrealMCP-UE_5.7-Win64.zip`

Manual install:

1. Pick the zip that matches your Unreal Engine version.
2. Extract the `UnrealMCP` folder into your project's `Plugins` directory.
3. Open the project in Unreal Editor. If `UnrealMCP` is not already enabled, enable it in the project and restart once.
4. Start the shared Python MCP server from `D:\MCP\Unreal\Python`:

```powershell
uv sync
uv run unreal_mcp_server.py
```

The Unreal plugin exposes the editor bridge on `127.0.0.1:55557`, and the Python server forwards MCP tool calls to that bridge.
