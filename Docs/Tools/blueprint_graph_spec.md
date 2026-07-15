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

## Node types

| type | Key fields |
| --- | --- |
| `event` | `event_name` (e.g. `ReceiveBeginPlay`) |
| `function` | `function_name`, optional `target`, optional `params` |
| `self` | — |
| `input_action` | `action_name` |
| `get_component` | `component_name` |
| `variable_get` / `variable_set` | `variable_name` |

Optional top-level `variables`: `{ "name", "type", "is_exposed" }` with types Boolean/Int/Float/String/Vector.

## Response

- `nodes[]` with `id` + `node_id` (GUID)
- `id_to_node_id` map for later `batch_connect_blueprint_nodes`
- `connection_failures`, `compile_ok`

## Notes

- Prefer `clear_event_graph=true` only when replacing the whole Ubergraph intentionally.
- Exec pin names are usually `then` / `execute` (aliases tried on failure).
- Not a full Blueprint Visual Script coverage (no branch/timeline/cast yet).
