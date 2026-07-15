# UnrealMCP plugin sync / upgrade

Per-project install copies the editor plugin into each Unreal project. That copy **goes stale** when this repo advances (protocol, tools, bugfixes).

Use the sync script instead of ad-hoc file copies.

## Script

```text
Scripts/sync_unreal_mcp_plugin.ps1
```

Requires: Windows PowerShell 5+ or PowerShell 7+, `robocopy`, and for Release mode a package under `Releases/UnrealMCP/`.

## Modes

| Mode | What it installs | When to use |
| --- | --- | --- |
| **Source** | Canonical `MCPGameProject/Plugins/UnrealMCP` **Source** + `.uplugin` (+ Config). Clears Intermediate/Binaries under the target plugin. | You will **rebuild** the plugin against the target project/engine. |
| **Release** | Prebuilt `UnrealMCP-UE_x.y-Win64.zip` (or unpacked `Releases/UnrealMCP/UE_x.y/UnrealMCP`) | Matching engine version; prefer zip install without compiling plugin C++. |
| **Status** | Nothing (read-only drift report) | Check whether target matches repo source on key files. |

After every sync the script writes:

```text
<Project>/Plugins/UnrealMCP/UnrealMCP.sync.json
```

with UTC time, mode, source path, and optional git commit of this repo.

## Examples

### Drift check

```powershell
cd D:\MCP\Unreal
.\Scripts\sync_unreal_mcp_plugin.ps1 `
  -TargetProject "E:\Kuan\Projects\Unreal\OW_CarScene_57\OW_CarScene_57.uproject" `
  -Mode Status
```

### Upgrade from source (then rebuild Editor)

```powershell
# Close Unreal Editor first if the project is open.
.\Scripts\sync_unreal_mcp_plugin.ps1 `
  -TargetProject "E:\Kuan\Projects\Unreal\OW_CarScene_57\OW_CarScene_57.uproject" `
  -Mode Source

# Then: Generate project files if needed, build *Editor Development, full restart Editor.
```

### Install / upgrade from release zip (UE 5.7)

```powershell
.\Scripts\sync_unreal_mcp_plugin.ps1 `
  -TargetProject "E:\Kuan\Projects\Unreal\UnrealMCP_ZipSmoke_57" `
  -Mode Release `
  -EngineVersion 5.7
```

Dry-run:

```powershell
.\Scripts\sync_unreal_mcp_plugin.ps1 -TargetProject "..." -Mode Source -WhatIf
```

## After sync checklist

1. **Editor closed** (or project unloaded) during Binary/Source replacement when possible.  
2. **Source mode:** rebuild the project’s Editor target.  
3. **Full Editor restart** (hot-reload often does not refresh command registration).  
4. Run shared Python from **this** repo (`protocol 2.0` must match).  
5. Verify:

```powershell
uv --directory D:\MCP\Unreal\Python run python -c "from bridge_client import run_bridge_command; print(run_bridge_command('get_bridge_status'))"
```

Expect `protocol_version: "2.0"` and `success: true`.

## Multi-instance ports

If several Editors run at once, set the same env for Editor process and Python:

```text
UNREAL_MCP_HOST=127.0.0.1
UNREAL_MCP_PORT=55558
```

## What this does *not* do

- Does not modify project Content/assets  
- Does not auto-build the Editor for you  
- Does not upgrade the Python server (keep `Python/` from the same repo commit as the plugin)  
- Does not push to git remotes  

## Related

- Prebuilt packages: [Releases/UnrealMCP/README.md](../../Releases/UnrealMCP/README.md)  
- Protocol notes: root [README.md](../../README.md) (Bridge protocol 2.0)  
- Live checklist (local): `PROJECT_GAPS_AND_ROADMAP.md` §4.2 / P2  
