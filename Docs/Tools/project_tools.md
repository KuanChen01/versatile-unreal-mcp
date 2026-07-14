# Unreal MCP Project Tools

Project tools manage editor-wide settings that are not tied to a single actor or asset.

## create_input_mapping

Create an input mapping in the Unreal project input settings.

**Parameters:**
- `action_name` (string) - Name of the input action
- `key` (string) - Input key to bind, for example `SpaceBar`
- `input_type` (string, optional) - `Action` or `Axis`, default `Action`

**Returns:**
- A structured success or failure response from the Unreal bridge

**Example:**

```json
{
  "command": "create_input_mapping",
  "params": {
    "action_name": "Jump",
    "key": "SpaceBar",
    "input_type": "Action"
  }
}
```
