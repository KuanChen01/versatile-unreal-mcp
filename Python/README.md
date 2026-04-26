# Unreal MCP Python Server

Python bridge for interacting with Unreal Engine 5.5+ using the Model Context Protocol (MCP). This server is reusable across projects that have the `UnrealMCP` editor plugin installed.

## Setup

1. Make sure Python 3.10+ is installed
2. Install `uv` if you haven't already:
   ```bash
   curl -LsSf https://astral.sh/uv/install.sh | sh
   ```
3. Create and activate a virtual environment:
   ```bash
   uv venv
   source .venv/bin/activate  # On Unix/macOS
   # or
   .venv\Scripts\activate     # On Windows
   ```
4. Install dependencies:
   ```bash
   uv pip install -e .
   ```

At this point, you can configure your MCP Client to run:

```bash
uv --directory /path/to/versatile-unreal-mcp/Python run unreal_mcp_server.py
```

## Testing Scripts

There are several scripts in the [scripts](./scripts) folder. They are useful for testing the tools and the Unreal Bridge via a direct connection. This means that you do not need to have an MCP Server running.

You should make sure you have installed dependencies and/or are running in the `uv` virtual environment in order for the scripts to work.


## Troubleshooting

- Make sure Unreal Engine editor is loaded loaded and running before running the server.
- Check logs in `unreal_mcp.log` for detailed error information

## Development

To add new tools, route the command in the Unreal plugin bridge, add the C++ handler, then expose the matching MCP wrapper in `tools/*.py`.
