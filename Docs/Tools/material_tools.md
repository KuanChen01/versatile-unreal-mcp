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
