# Workflow tools (composite)

High-frequency multi-step recipes for agents. Prefer these over long chains of fine-grained tools when they match the job.

Registered from `Python/tools/workflow_tools.py`.

## Tools

### `editor_preflight()`

Read-only readiness check before mutations:

- `get_bridge_status` (protocol 2.0, editor connected)
- `get_level_status`
- `get_viewport_status`

Returns `ready: true/false` plus structured bridge/level/viewport fields and agent hints.

### `spawn_actor_with_material(class_path, material_path, name="", location, rotation, scale, slot_index=0)`

**DESTRUCTIVE** (spawns a level actor).

1. `spawn_actor_by_class`
2. `assign_material_to_actor`

Use a **unique** `name`.

### `create_and_rebuild_material(material_path, graph_spec, validate=True)`

**DESTRUCTIVE** (creates/rebuilds material graph).

1. `create_material` (continues if asset already exists)
2. `rebuild_material_graph`
3. optional `validate_material_graph`

### `create_blueprint_with_graph(name, graph_spec, parent_class="Actor", clear_event_graph=True, compile=True)`

**DESTRUCTIVE** when clearing the event graph.

1. `create_blueprint` (continues if already exists)
2. `rebuild_blueprint_graph`
3. `compile_blueprint` when `compile=True`

## Agent policy

1. Call `editor_preflight` before mutating.
2. Prefer workflow + atomic rebuild tools over many `add_*` / `connect_*` steps.
3. Never reuse actor names that may still exist in the level.
4. Treat descriptions marked **DESTRUCTIVE** as project-changing.

## Related

- Material graph_spec: [material_tools.md](material_tools.md)
- Blueprint graph_spec: [blueprint_graph_spec.md](blueprint_graph_spec.md)
- Plugin sync: [plugin_sync.md](plugin_sync.md)
