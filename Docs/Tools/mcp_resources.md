# MCP Resources

Read-only surfaces for agent **planning**. Prefer reading a resource before calling mutating tools when you only need state.

Registered from `Python/tools/mcp_resources.py` via FastMCP `@mcp.resource`.

MIME type for all current resources: `application/json`.

## Fixed resources

| URI | Editor required | Contents |
| --- | --- | --- |
| `unreal://protocol` | No | Protocol 2.0 contract, default host/port, env overrides |
| `unreal://bridge/status` | Yes | Live plugin, protocol, listen, command groups |
| `unreal://level/status` | Yes | Map package, dirty, actor count |
| `unreal://viewport/status` | Yes | Active viewport readiness |
| `unreal://play/state` | Yes | PIE/SIE state |
| `unreal://actors/list` | Yes | Actors in the current level |
| `unreal://preflight` | Yes | Composite readiness (bridge + level + viewport) |

## Template resources

| URI template | Example | Notes |
| --- | --- | --- |
| `unreal://assets/find/{query}` | `unreal://assets/find/Material` | Search under `/Game`, max 50. Use `_` for a broad sample. |
| `unreal://asset/info/{package_path}` | `unreal://asset/info/Game-MCP_Smoke-M_Wf` | Dashes encode slashes: `Game-Foo-Bar` → `/Game/Foo/Bar` |

## Client usage (conceptual)

```text
resources/list   → discover URIs
resources/read   → unreal://bridge/status
```

When the Editor is closed, live resources return JSON with `success: false` and a short hint (they do not crash the MCP server).

## Relation to tools

| Need | Prefer resource | Or tool |
| --- | --- | --- |
| Ready to edit? | `unreal://preflight` | `editor_preflight` |
| Command surface | `unreal://bridge/status` | `get_bridge_status` |
| Search assets | `unreal://assets/find/{q}` | `find_assets` |
| Mutate level/assets | — | tools (spawn, delete, rebuild, …) |

## Related

- Workflow tools: [workflow_tools.md](workflow_tools.md)
- Protocol: root README (Bridge protocol 2.0)
