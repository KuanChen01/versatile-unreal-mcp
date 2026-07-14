# Unreal MCP Editor Tools

This document covers the editor-facing tools exposed through the Unreal MCP Python server.

## Level and Play Session Management

### get_level_status

Report the current editor world, map package path, dirty state, actor count, and whether a play world exists.

**Returns:**
- `success` (boolean)
- `world_name` (string)
- `level_name` (string)
- `package_name` (string)
- `file_path` (string)
- `is_dirty` (boolean)
- `actor_count` (number)
- `has_play_world` (boolean)

### open_level

Open a level by `/Game/...` package path or absolute `.umap` file path.

**Parameters:**
- `level_path` (string) - `/Game/...` map package path or absolute `.umap` filename
- `save_dirty_packages` (boolean, optional) - Save dirty packages before loading, default `false`

**Notes:**
- Fails if PIE or SIE is active.
- Fails instead of silently discarding unsaved changes.

### save_current_level

Save the current editor level without prompting.

### save_dirty_packages

Save dirty map and/or content packages without prompting.

**Parameters:**
- `save_map_packages` (boolean, optional) - Default `true`
- `save_content_packages` (boolean, optional) - Default `true`

### get_play_state

Report whether PIE or SIE is active or queued and which play world is currently running.

**Returns:**
- `success` (boolean)
- `is_play_session_in_progress` (boolean)
- `is_playing_session_in_editor` (boolean)
- `is_play_session_request_queued` (boolean)
- `is_simulating_in_editor` (boolean)
- `session_type` (`play_in_editor`, `simulate_in_editor`, or `inactive`)
- `has_play_world` (boolean)
- `play_world_name` (string)
- `pie_instance_count` (number)

### start_pie

Start a PIE or SIE session in the active level editor viewport.

**Parameters:**
- `simulate` (boolean, optional) - Start Simulate In Editor instead of Play In Editor, default `false`
- `location` (array, optional) - `[X, Y, Z]` start location override
- `rotation` (array, optional) - `[Pitch, Yaw, Roll]` start rotation override

### stop_pie

Stop an active or queued PIE/SIE session.

### get_output_log

Return recent buffered Unreal output log entries captured by the plugin.

**Parameters:**
- `max_entries` (integer, optional) - Default `200`
- `contains` (string, optional) - Case-insensitive message substring filter
- `category` (string, optional) - Exact category filter
- `verbosity` (string, optional) - Exact verbosity filter such as `Error`, `Warning`, or `Display`

### get_message_log

Return recent entries from a named Unreal Message Log listing.

**Parameters:**
- `log_name` (string, optional) - Default `PIE`
- `max_entries` (integer, optional) - Default `100`
- `contains` (string, optional) - Case-insensitive message substring filter
- `severity` (string, optional) - Exact severity filter such as `Error`, `Warning`, or `Info`

## Bridge and Viewport Self-Checks

### get_bridge_status

Report the live Unreal plugin status and the command groups currently routed by the bridge.

**Returns:**
- `success` (boolean)
- `plugin` (`name`, `version`, `version_name`)
- `server` (`name`, `version`)
- `protocol_version` (string)
- `editor` (`connected`, `has_active_viewport`)
- `commands` (`editor`, `blueprint`, `blueprint_nodes`, `material`, `project`, `umg`)

### get_viewport_status

Report whether Unreal currently has an active editor viewport and whether the viewport is ready for focusing and screenshots.

**Returns:**
- `success` (boolean)
- `has_active_viewport` (boolean)
- `size` (`[width, height]` or `null`)
- `can_focus` (boolean)
- `can_screenshot` (boolean)

### focus_viewport

Move the active level editor viewport to inspect a target actor or world location.

**Parameters:**
- `target` (string, optional) - Actor name to inspect
- `location` (array, optional) - `[X, Y, Z]` world location
- `distance` (float, optional) - Camera offset distance from the focus point, default `1000.0`
- `orientation` (array, optional) - `[Pitch, Yaw, Roll]` camera orientation

**Notes:**
- Provide either `target` or `location`.
- Fails with a structured error when no level editor viewport is available.

### take_screenshot

Capture the active Unreal Editor viewport to a PNG file.

**Parameters:**
- `filepath` (string, optional) - Absolute or relative PNG output path. If omitted, the Python wrapper writes to a timestamped file under the system temp screenshots directory.

**Returns:**
- `success` (boolean)
- `filepath` (string)
- `size` (`[width, height]`) when the capture succeeds

## Actor Management

- `get_actors_in_level()` - List actors in the current level
- `find_actors_by_name(pattern)` - Find actors by name pattern
- `spawn_actor(name, type, location=[0,0,0], rotation=[0,0,0])` - Spawn a new actor
- `delete_actor(name)` - Delete an actor by name
- `set_actor_transform(name, location=None, rotation=None, scale=None)` - Update actor transform
- `get_actor_properties(name)` - Inspect actor properties
- `set_actor_property(name, property_name, property_value)` - Set a single actor property
- `spawn_blueprint_actor(blueprint_name, actor_name, location=[0,0,0], rotation=[0,0,0])` - Spawn an actor from a Blueprint class

## Recommended Self-Check Flow

1. Call `get_bridge_status()` to verify the live editor session and routed commands.
2. Call `get_level_status()` to verify which map is loaded and whether it is dirty.
3. Call `get_viewport_status()` to verify the viewport is available.
4. Call `focus_viewport(...)` to position the camera.
5. Call `take_screenshot(...)` to capture evidence of the editor state.
6. Call `get_play_state()` / `start_pie()` / `get_output_log()` / `get_message_log()` when runtime validation is needed.
