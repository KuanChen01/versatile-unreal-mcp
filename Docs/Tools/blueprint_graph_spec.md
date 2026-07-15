# Blueprint graph_spec (v1)

Atomic Event Graph authoring via `rebuild_blueprint_graph`.

## Tool

```text
rebuild_blueprint_graph(blueprint_name, graph_spec, clear_event_graph=False, compile=True)
batch_connect_blueprint_nodes(blueprint_name, connections, id_to_node_id=None)
```

## Minimal example

```json
{
  "version": 1,
  "options": { "clear_event_graph": true, "compile": true },
  "nodes": [
    {
      "id": "begin",
      "type": "event",
      "event_name": "ReceiveBeginPlay",
      "position": [0, 0]
    },
    {
      "id": "print",
      "type": "function",
      "target": "KismetSystemLibrary",
      "function_name": "PrintString",
      "position": [350, 0],
      "params": { "InString": "Hello from graph_spec" }
    }
  ],
  "connections": [
    {
      "from": { "node": "begin", "pin": "then" },
      "to": { "node": "print", "pin": "execute" }
    }
  ]
}
```

## Branch example

```json
{
  "version": 1,
  "options": { "clear_event_graph": true, "compile": true },
  "nodes": [
    { "id": "begin", "type": "event", "event_name": "ReceiveBeginPlay", "position": [0, 0] },
    { "id": "br", "type": "branch", "condition": true, "position": [250, 0] },
    {
      "id": "yes",
      "type": "function",
      "target": "KismetSystemLibrary",
      "function_name": "PrintString",
      "position": [500, -80],
      "params": { "InString": "true path" }
    },
    {
      "id": "no",
      "type": "function",
      "target": "KismetSystemLibrary",
      "function_name": "PrintString",
      "position": [500, 80],
      "params": { "InString": "false path" }
    }
  ],
  "connections": [
    { "from": { "node": "begin", "pin": "then" }, "to": { "node": "br", "pin": "execute" } },
    { "from": { "node": "br", "pin": "true" }, "to": { "node": "yes", "pin": "execute" } },
    { "from": { "node": "br", "pin": "false" }, "to": { "node": "no", "pin": "execute" } }
  ]
}
```

## Node types

| type | Key fields | Notes |
| --- | --- | --- |
| `event` | `event_name` (e.g. `ReceiveBeginPlay`) | Standard override events |
| `function` | `function_name`, optional `target`, optional `params` | Aliases: `call_function`, `function_call` |
| `self` | — | Self reference |
| `input_action` | `action_name` | Alias: `input` |
| `get_component` | `component_name` | |
| `variable_get` / `variable_set` | `variable_name` | |
| `branch` | optional `condition` (bool default) | Aliases: `if`, `if_then_else`. Out pins: `then`/`true`, `else`/`false` |
| `cast` | `class` / `target_class` / `cast_class`, optional `pure` | Aliases: `dynamic_cast`, `cast_to`. Object in: `Object`; fail exec: `else`/`CastFailed`; success object: `as` |
| `custom_event` | `event_name` (or `name`) | User-defined custom event |
| `timeline` | `timeline_name` (or `name`) | Creates/reuses `UTimelineTemplate`; pins include `Play`, `Update`, `Finished` |

Optional top-level `variables`: `{ "name", "type", "is_exposed" }` with types Boolean/Int/Float/String/Vector.

## Pin naming (agent-friendly)

Connections accept friendly aliases; the plugin expands them:

| You may write | Typical engine pin |
| --- | --- |
| `then` / `true` / `then0` | Branch true exec out |
| `else` / `false` / `then1` | Branch false / cast fail |
| `execute` / `exec` | Exec in |
| `Condition` / `cond` | Branch condition |
| `Object` / `target` | Cast object in |
| `as` / `result` | Cast success object out (first data out) |
| `Play` / `Update` / `Finished` | Timeline |

## Response

- `nodes[]` with `id` + `node_id` (GUID)
- `id_to_node_id` map for later `batch_connect_blueprint_nodes`
- `connection_failures`, `compile_ok`

## Notes

- Prefer `clear_event_graph=true` only when replacing the whole Ubergraph intentionally.
- Exec pin names are usually `then` / `execute` (aliases tried on failure).
- Timeline curves/keys are not authored by graph_spec v1 (node + template shell only).
