# Unreal MCP Material Tools

These tools create, inspect, validate, and rebuild Unreal material graphs from MCP.

## Core Material Tools

- `create_material(material_path)` - Create a material asset at a `/Game/...` path
- `set_material_properties(material_path, properties)` - Set blend mode, shading model, and related material properties
- `add_material_expression(...)` - Add a material node and return stable refs when available
- `set_material_expression_property(...)` - Edit one or more properties on a material node
- `connect_material_expressions(...)` - Connect two material nodes
- `connect_material_property(...)` - Connect a node output to a material root property
- `recompile_material(material_path)` - Compile and save a material asset

## Pin Resolution (handler_build >= 2026-08-11.1)

UE 5.7+ can use **dynamic display names** for some expression inputs (for example
`DistanceToNearestSurface` world-position pin). Bare property names like
`Position` are no longer always the pin label shown in the Material Editor.

`connect_material_expressions` now resolves targets in this order:

1. `to_input_index` / `from_output_index` (explicit)
2. empty name + first pin default
3. case-insensitive **display name** (`GetInputName`)
4. case-insensitive reflected **`FExpressionInput` property name** (e.g. `Position`)
5. unique soft contains match on display name

Parameters:

| Param | Role |
| --- | --- |
| `to_input_name` | Display name **or** stable property name |
| `to_input_index` | 0-based input index (preferred when known) |
| `from_output_name` | Output display/property name |
| `from_output_index` | 0-based output index |

On failure the error payload includes `available_inputs` / `available_outputs`
with `index`, `display_name`, and `property_name`. Expression JSON from create/
connect responses also includes `inputs[]` and `outputs[]`.

`rebuild_material_graph` connection endpoints accept the same resolution:
`input` / `input_name` / `input_index` and `output` / `output_name` / `output_index`.

## Graph-Level Tools

- `rebuild_material_graph(material_path, graph_spec)` - Rebuild a material graph atomically from a declarative spec
- `get_material_compile_status(material_path)` - Return compile errors, error nodes, and material statistics
- `validate_material_graph(material_path)` - Validate root outputs, required inputs, `ComponentMask` usage, and compile status

## Asset Safety Tools

- `reload_asset_from_disk(asset_path, close_editors=False, fail_if_dirty=True)` - Reload an asset package safely
- `close_asset_editor(asset_path)` - Close open asset editors for an asset
- `is_asset_loaded_dirty(asset_path)` - Report whether an asset is loaded, dirty, or open in editors

## Material Function / Preset Tools

- `create_material_function(function_path)` - Create a Material Function asset
- `rebuild_material_function_graph(function_path, graph_spec)` - Rebuild a Material Function from a declarative spec
- `configure_glass_material(material_path, ...)` - Build a practical glass preset in the target material asset

## Notes

- Prefer `rebuild_material_graph(...)` for non-trivial graphs so compile, validation, and save happen as one operation.
- Use the asset safety tools before overwriting assets that may already be open in the Unreal Editor.
