# Unreal MCP UMG Tools

UMG tools create and edit Widget Blueprints from MCP.

## Widget Blueprint Creation

- `create_umg_widget_blueprint(widget_name, parent_class="UserWidget", path="/Game/UI")` - Create a Widget Blueprint asset

## Widget Structure and Styling

- `add_text_block_to_widget(...)` - Add a text block with position, size, font size, and color
- `add_button_to_widget(...)` - Add a button with text, size, foreground color, and background color

## Widget Behavior

- `bind_widget_event(widget_name, widget_component_name, event_name, function_name="")` - Bind widget events such as `OnClicked`
- `set_text_block_binding(widget_name, text_block_name, binding_property, binding_type="Text")` - Create a dynamic text binding
- `add_widget_to_viewport(widget_name, z_order=0)` - Add a widget instance to the game viewport

## Notes

- Use descriptive widget and component names so later Blueprint automation can find and reuse them.
- These tools operate on widget assets and editor-side setup; they do not replace runtime Blueprint logic where gameplay-specific behavior is required.
