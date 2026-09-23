"""
Unreal Engine MCP Server

A simple MCP server for interacting with Unreal Engine.
"""

import logging
import socket
import threading
import time
from contextlib import asynccontextmanager
from typing import AsyncIterator, Dict, Any, Optional

import anyio
from mcp.server.fastmcp import FastMCP

from stdio_compat import run_fastmcp_stdio

from bridge_protocol import (
    PROTOCOL_INCOMPATIBLE_HINT,
    PROTOCOL_VERSION,
    ProtocolError,
    build_command,
    extract_remote_protocol_version,
    format_protocol_mismatch_message,
    is_protocol_compatible,
    is_retryable_transport_error,
    recv_json_frame,
    send_json_frame,
    timeout_for_command,
)

# Configure logging with more detailed format
logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s - %(name)s - %(levelname)s - [%(filename)s:%(lineno)d] - %(message)s",
    handlers=[
        logging.FileHandler("unreal_mcp.log"),
        # StreamHandler removed: stdio transport must stay clean for MCP JSON-RPC
    ],
)
logger = logging.getLogger("UnrealMCP")

# Configuration
SERVER_NAME = "UnrealMCP Python Server"
SERVER_VERSION = "1.1.0"

# Multi-instance: set UNREAL_MCP_HOST / UNREAL_MCP_PORT to match the Editor process env.
import os as _os

UNREAL_HOST = _os.environ.get("UNREAL_MCP_HOST", "127.0.0.1").strip() or "127.0.0.1"
try:
    UNREAL_PORT = int(_os.environ.get("UNREAL_MCP_PORT", "55557"))
except ValueError:
    UNREAL_PORT = 55557
CONNECT_TIMEOUT = 5.0
# Reconnect+retry after WinError 10053 / reset / mid-frame close.
TRANSPORT_MAX_ATTEMPTS = 8
# Base backoff; multiplies by attempt. Must cover multi-agent Accept contention
# (another MCP host may hold the bridge idle for a short poll window).
TRANSPORT_RETRY_BACKOFF_SEC = 0.2
# Session reuse is opt-in. Default is one-shot TCP per command:
# connect → frame → response → close. This matches real agent tool gaps and
# avoids WinError 10053 on half-open sockets when an older plugin ends the
# session after each response. Set UNREAL_MCP_SESSION_REUSE=1 after loading
# handler_build >= 2026-09-23.1 to enable multi-request TCP sessions.
def _env_flag(name: str, default: bool = False) -> bool:
    raw = _os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() in ("1", "true", "yes", "on")


SESSION_REUSE_ENABLED = _env_flag("UNREAL_MCP_SESSION_REUSE", default=False)
# Only used when SESSION_REUSE_ENABLED: proactive reconnect after idle.
SESSION_MAX_IDLE_SEC = float(_os.environ.get("UNREAL_MCP_SESSION_MAX_IDLE_SEC", "30") or "30")


class UnrealConnection:
    """
    Framed bridge client for UnrealMCP.

    Default: one-shot TCP per command (reliable under agent think-gaps).
    Optional session reuse via UNREAL_MCP_SESSION_REUSE=1 (needs plugin that
    keeps the accepted socket open for multiple frames). Retries transient
    WinError 10053/10054 with exponential backoff.
    """

    def __init__(self):
        self.socket: Optional[socket.socket] = None
        self.connected = False
        self.remote_protocol_version: Optional[str] = None
        self._lock = threading.RLock()
        self._last_success_at: float = 0.0
        self._session_requests: int = 0

    def connect(self, timeout: float = CONNECT_TIMEOUT) -> bool:
        """Open a TCP connection to the UnrealMCP plugin bridge."""
        try:
            self.disconnect()

            logger.info(
                "Connecting to Unreal at %s:%s (protocol %s)...",
                UNREAL_HOST,
                UNREAL_PORT,
                PROTOCOL_VERSION,
            )
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)
            # Do NOT set SO_LINGER(on). On Windows linger is two u_shorts; packing
            # two ints is wrong and linger-on can RST mid-retry storms.
            sock.connect((UNREAL_HOST, UNREAL_PORT))

            self.socket = sock
            self.connected = True
            self._session_requests = 0
            logger.info("Connected to Unreal Engine")
            return True
        except OSError as exc:
            logger.error("Failed to connect to Unreal: %s", exc)
            self.disconnect()
            return False

    def disconnect(self) -> None:
        """Close the current socket if open (graceful half-close when possible)."""
        if self.socket is not None:
            try:
                # Prefer write-half shutdown so the plugin sees a clean peer close
                # while any residual FIN/ACK can still complete.
                self.socket.shutdown(socket.SHUT_WR)
            except OSError:
                pass
            try:
                self.socket.close()
            except OSError:
                pass
        self.socket = None
        self.connected = False
        self._session_requests = 0

    def _session_is_reusable(self) -> bool:
        if not SESSION_REUSE_ENABLED:
            return False
        if not (self.connected and self.socket is not None):
            return False
        if self._last_success_at <= 0:
            return True
        return (time.monotonic() - self._last_success_at) <= SESSION_MAX_IDLE_SEC

    def _ensure_connected(self, timeout: float) -> bool:
        if self._session_is_reusable():
            return True
        return self.connect(timeout=timeout)

    def _round_trip(
        self,
        command: str,
        params: Optional[Dict[str, Any]],
        *,
        command_timeout: float,
        request_id: Optional[str],
        session_mode: str,
    ) -> Dict[str, Any]:
        assert self.socket is not None
        self.socket.settimeout(command_timeout)

        request = build_command(command, params, request_id=request_id)
        logger.info(
            "Sending command type=%s request_id=%s timeout=%.1fs protocol=%s session=%s n=%s",
            command,
            request_id,
            command_timeout,
            PROTOCOL_VERSION,
            session_mode,
            self._session_requests + 1,
        )
        logger.debug("Command payload: %s", request)
        send_json_frame(self.socket, request)

        response = recv_json_frame(self.socket)
        self._session_requests += 1
        self._last_success_at = time.monotonic()
        logger.info(
            "Complete response from Unreal for %s request_id=%s",
            command,
            request_id or (response.get("request_id") if isinstance(response, dict) else None),
        )
        logger.debug("Response payload: %s", response)

        if response.get("status") == "error":
            error_message = response.get("error") or response.get("message", "Unknown Unreal error")
            logger.error("Unreal error (status=error): %s", error_message)
            if "error" not in response:
                response["error"] = error_message
        elif response.get("success") is False:
            error_message = response.get("error") or response.get("message", "Unknown Unreal error")
            logger.error("Unreal error (success=false): %s", error_message)
            response = {
                "status": "error",
                "error": error_message,
            }

        return response

    def send_command(
        self,
        command: str,
        params: Optional[Dict[str, Any]] = None,
        *,
        timeout: Optional[float] = None,
        request_id: Optional[str] = None,
    ) -> Optional[Dict[str, Any]]:
        """
        Send a command to Unreal Engine and return the parsed JSON response.

        Default one-shot TCP (connect per command). Retries transient transport
        errors (e.g. WinError 10053) with exponential backoff. Optional session
        reuse via UNREAL_MCP_SESSION_REUSE=1.
        """
        command_timeout = timeout if timeout is not None else timeout_for_command(command)
        connect_timeout = min(CONNECT_TIMEOUT, command_timeout)
        last_exc: Optional[BaseException] = None

        with self._lock:
            for attempt in range(1, TRANSPORT_MAX_ATTEMPTS + 1):
                try:
                    was_reusable = self._session_is_reusable()
                    if not self._ensure_connected(connect_timeout):
                        logger.error(
                            "Failed to connect to Unreal Engine for command %s "
                            "request_id=%s attempt=%s",
                            command,
                            request_id,
                            attempt,
                        )
                        if attempt < TRANSPORT_MAX_ATTEMPTS:
                            # Cap backoff so multi-agent waits stay bounded.
                            time.sleep(min(1.5, TRANSPORT_RETRY_BACKOFF_SEC * attempt))
                            continue
                        return None

                    session_mode = "reuse" if was_reusable else "new"
                    response = self._round_trip(
                        command,
                        params,
                        command_timeout=command_timeout,
                        request_id=request_id,
                        session_mode=session_mode,
                    )
                    # One-shot: release the socket so the plugin Accept loop is free
                    # for the next agent tool call (and other MCP clients).
                    if not SESSION_REUSE_ENABLED:
                        self.disconnect()
                    return response
                except ProtocolError as exc:
                    last_exc = exc
                    logger.error(
                        "Protocol error for %s attempt=%s: %s",
                        command,
                        attempt,
                        exc,
                    )
                    self.disconnect()
                    if exc.incompatible or not is_retryable_transport_error(exc):
                        message = str(exc)
                        if exc.incompatible:
                            message = f"{message} ({PROTOCOL_INCOMPATIBLE_HINT})"
                        return {
                            "status": "error",
                            "error": message,
                            "protocol_version": PROTOCOL_VERSION,
                            "protocol_incompatible": bool(exc.incompatible),
                        }
                    if attempt < TRANSPORT_MAX_ATTEMPTS:
                        backoff = min(1.5, TRANSPORT_RETRY_BACKOFF_SEC * attempt)
                        logger.warning(
                            "Retrying %s after transient transport error "
                            "(attempt %s/%s backoff=%.2fs): %s",
                            command,
                            attempt,
                            TRANSPORT_MAX_ATTEMPTS,
                            backoff,
                            exc,
                        )
                        time.sleep(backoff)
                        continue
                    return {
                        "status": "error",
                        "error": str(exc),
                        "protocol_version": PROTOCOL_VERSION,
                        "protocol_incompatible": False,
                        "transport_retries": attempt,
                    }
                except OSError as exc:
                    last_exc = exc
                    logger.error(
                        "Socket error sending command %s attempt=%s: %s",
                        command,
                        attempt,
                        exc,
                    )
                    self.disconnect()
                    if not is_retryable_transport_error(exc) or attempt >= TRANSPORT_MAX_ATTEMPTS:
                        return {
                            "status": "error",
                            "error": str(exc),
                            "protocol_version": PROTOCOL_VERSION,
                            "transport_retries": attempt,
                        }
                    backoff = min(1.5, TRANSPORT_RETRY_BACKOFF_SEC * attempt)
                    logger.warning(
                        "Retrying %s after OSError (attempt %s/%s backoff=%.2fs): %s",
                        command,
                        attempt,
                        TRANSPORT_MAX_ATTEMPTS,
                        backoff,
                        exc,
                    )
                    time.sleep(backoff)
                except Exception as exc:  # noqa: BLE001 — surface unexpected errors to tools
                    logger.error("Error sending command %s: %s", command, exc)
                    self.disconnect()
                    return {
                        "status": "error",
                        "error": str(exc),
                        "protocol_version": PROTOCOL_VERSION,
                    }

        return {
            "status": "error",
            "error": str(last_exc) if last_exc else "Transport failed after retries",
            "protocol_version": PROTOCOL_VERSION,
        }

    def ping(self) -> bool:
        """Return True if a framed ping round-trip succeeds (no version gate)."""
        ok, _ = self.handshake()
        return ok

    def handshake(self) -> tuple[bool, Optional[str]]:
        """
        Framed ping + hard protocol version check.

        Returns:
            (True, None) on success.
            (False, error_message) when unreachable, framing-incompatible, or
            protocol_version does not match ``PROTOCOL_VERSION``.
        """
        response = self.send_command("ping", {})
        if not response:
            return (
                False,
                "Failed to connect to Unreal Engine (no response to ping). "
                "Is the Editor running with UnrealMCP enabled on 127.0.0.1:55557?",
            )

        if response.get("protocol_incompatible"):
            return (
                False,
                response.get("error")
                or response.get("message")
                or PROTOCOL_INCOMPATIBLE_HINT,
            )

        if response.get("status") == "error":
            return (
                False,
                response.get("error")
                or response.get("message")
                or "UnrealMCP ping failed",
            )

        result = response.get("result") if isinstance(response.get("result"), dict) else response
        is_pong = False
        if isinstance(result, dict) and result.get("message") == "pong":
            is_pong = True
        elif response.get("message") == "pong":
            is_pong = True

        if not is_pong:
            return (
                False,
                "UnrealMCP ping did not return pong. "
                "Check that the live editor plugin is UnrealMCP and listening.",
            )

        remote_version = extract_remote_protocol_version(response)
        if not is_protocol_compatible(remote_version):
            message = format_protocol_mismatch_message(remote_version)
            logger.error(message)
            self.remote_protocol_version = remote_version
            return False, message

        self.remote_protocol_version = remote_version
        logger.info(
            "UnrealMCP protocol handshake ok (remote=%s local=%s)",
            remote_version,
            PROTOCOL_VERSION,
        )
        return True, None


# Global connection state
_unreal_connection: Optional[UnrealConnection] = None
_last_connect_error: Optional[str] = None
_remote_protocol_version: Optional[str] = None


def get_last_connect_error() -> Optional[str]:
    """Most recent handshake / connect failure message for tool envelopes."""
    return _last_connect_error


def get_remote_protocol_version() -> Optional[str]:
    """Last successfully negotiated remote protocol version, if any."""
    return _remote_protocol_version


def clear_unreal_connection_cache() -> None:
    """Drop cached connection so the next call re-runs the hard handshake."""
    global _unreal_connection, _remote_protocol_version
    if _unreal_connection is not None:
        _unreal_connection.disconnect()
    _unreal_connection = None
    _remote_protocol_version = None


def get_unreal_connection() -> Optional[UnrealConnection]:
    """
    Get a connection helper that can reach Unreal Engine.

    On first use (or after a previous failure cleared the cache), health is
    verified with a framed ``ping`` **and** a hard protocol version check.
    Subsequent tool calls reuse the helper and its TCP session (with automatic
    reconnect/retry on transient socket aborts).
    """
    global _unreal_connection, _last_connect_error, _remote_protocol_version
    try:
        if _unreal_connection is not None:
            return _unreal_connection

        candidate = UnrealConnection()
        ok, error = candidate.handshake()
        if not ok:
            _last_connect_error = error or "Failed to connect to Unreal Engine"
            logger.warning("UnrealMCP handshake failed: %s", _last_connect_error)
            _unreal_connection = None
            _remote_protocol_version = None
            return None

        _last_connect_error = None
        _remote_protocol_version = candidate.remote_protocol_version
        _unreal_connection = candidate
        return _unreal_connection
    except Exception as exc:  # noqa: BLE001
        logger.error("Error getting Unreal connection: %s", exc)
        _last_connect_error = str(exc)
        _unreal_connection = None
        _remote_protocol_version = None
        return None


@asynccontextmanager
async def server_lifespan(server: FastMCP) -> AsyncIterator[Dict[str, Any]]:
    """Handle server startup and shutdown.

    Do not handshake the Unreal bridge here. MCP clients such as Antigravity
    CLI send ``initialize`` as soon as the process starts and close stdin if
    the first JSON-RPC reply waits on a TCP ping. Bridge connect stays lazy
    via ``get_unreal_connection()`` on first tool use.
    """
    global _unreal_connection
    logger.info(
        "UnrealMCP server starting up (bridge protocol %s; lazy bridge connect)",
        PROTOCOL_VERSION,
    )
    try:
        yield {}
    finally:
        if _unreal_connection:
            _unreal_connection.disconnect()
            _unreal_connection = None
        logger.info("Unreal MCP server shut down")


# Initialize server
mcp = FastMCP(
    "UnrealMCP",
    description="Unreal Engine integration via Model Context Protocol",
    lifespan=server_lifespan,
)

# Import and register tools
from tools.editor_tools import register_editor_tools
from tools.blueprint_tools import register_blueprint_tools
from tools.node_tools import register_blueprint_node_tools
from tools.material_tools import register_material_tools
from tools.project_tools import register_project_tools
from tools.umg_tools import register_umg_tools
from tools.workflow_tools import register_workflow_tools
from tools.mcp_resources import register_mcp_resources

# Register tools
register_editor_tools(mcp)
register_blueprint_tools(mcp)
register_blueprint_node_tools(mcp)
register_material_tools(mcp)
register_project_tools(mcp)
register_umg_tools(mcp)
register_workflow_tools(mcp)
register_mcp_resources(mcp)


@mcp.prompt()
def info():
    """Information about available Unreal MCP tools and best practices."""
    return f"""
    # Unreal MCP Server Tools and Best Practices

    Bridge protocol version: {PROTOCOL_VERSION} (length-prefixed JSON frames).
    The UnrealMCP editor plugin and this Python server must be upgraded together.

    ## MCP Resources (read-only — prefer before tools when planning)
    - `unreal://protocol` — static protocol contract (no Editor required)
    - `unreal://bridge/status` — live bridge / plugin / command groups
    - `unreal://level/status` — current map, dirty flag, actor count
    - `unreal://viewport/status` — viewport readiness
    - `unreal://play/state` — PIE/SIE state
    - `unreal://actors/list` — actors in the current level
    - `unreal://preflight` — composite readiness snapshot
    - `unreal://metrics/recent` — recent bridge command metrics (request_id, durations)
    - `unreal://assets/find/{{query}}` — asset search under /Game (use `_` for broad sample)
    - `unreal://asset/info/{{package_path}}` — one asset; encode path with dashes (Game-Foo-Bar → /Game/Foo/Bar)

    ## Correlation
    - Every tool response includes ``meta.request_id`` and ``meta.client_duration_ms``.
    - When the plugin supports it, ``meta.plugin_duration_ms`` and the wire ``request_id`` echo match.

    ## Agent policy (read first)
    1. Read `unreal://preflight` or call `editor_preflight` / `get_bridge_status` before mutating.
    2. Prefer **composite workflow tools** when they match the job:
       - `editor_preflight` — bridge/level/viewport readiness
       - `spawn_actor_with_material` — spawn by class + assign material
       - `create_and_rebuild_material` — create material + atomic graph rebuild
       - `create_blueprint_with_graph` — create BP + atomic event graph rebuild
    3. Prefer atomic graph rebuilds (`rebuild_material_graph`, `rebuild_blueprint_graph`) over many add_*/connect_* calls.
    4. Use **unique actor names**. Never delete+respawn with the same name unless delete succeeded and the name is free.
    5. Tools marked DESTRUCTIVE change the project; confirm intent before chaining them.

    ## UMG (Widget Blueprint) Tools
    - `create_umg_widget_blueprint(widget_name, parent_class="UserWidget", path="/Game/UI")`
      Create a new UMG Widget Blueprint
    - `add_text_block_to_widget(widget_name, text_block_name, text="", position=[0,0], size=[200,50], font_size=12, color=[1,1,1,1])`
      Add a Text Block widget with customizable properties
    - `add_button_to_widget(widget_name, button_name, text="", position=[0,0], size=[200,50], font_size=12, color=[1,1,1,1], background_color=[0.1,0.1,0.1,1])`
      Add a Button widget with text and styling
    - `bind_widget_event(widget_name, widget_component_name, event_name, function_name="")`
      Bind events like OnClicked to functions
    - `add_widget_to_viewport(widget_name, z_order=0)`
      Add widget instance to game viewport
    - `set_text_block_binding(widget_name, text_block_name, binding_property, binding_type="Text")`
      Set up dynamic property binding for text blocks

    ## Editor Tools
    ### Level and Play Session Management
    - `get_level_status()` - Report the current editor world, level package path, dirty state, and actor count
    - `open_level(level_path, save_dirty_packages=False)` - Open a /Game package path or absolute .umap file, optionally saving dirty packages first
    - `save_current_level()` - Save the current editor level without prompting
    - `save_dirty_packages(save_map_packages=True, save_content_packages=True)` - Save dirty map and/or content packages without prompting
    - `get_play_state()` - Report whether PIE/SIE is active or queued and which world is running
    - `start_pie(simulate=False, location=None, rotation=None)` - Start Play In Editor or Simulate In Editor in the active viewport
    - `stop_pie()` - Stop an active or queued play session
    - `get_output_log(max_entries=200, contains="", category="", verbosity="")` - Return recent buffered Unreal output log entries
    - `get_message_log(log_name="PIE", max_entries=100, contains="", severity="")` - Return recent entries from a named Unreal Message Log listing

    ### Bridge and Viewport Self-Checks
    - `get_bridge_status()` - Report the live plugin, editor, protocol, and routed command groups
    - `get_viewport_status()` - Report active viewport availability, size, and focus/screenshot readiness
    - `focus_viewport(target=None, location=None, distance=1000.0, orientation=None)` - Focus the active level editor viewport on an actor or location
    - `take_screenshot(filepath="")` - Capture the active Editor viewport to a PNG file, using a timestamped system temp path when omitted

    ### Actor Management
    - `get_actors_in_level()` - List all actors in current level
    - `find_actors_by_name(pattern)` - Find actors by name pattern
    - `spawn_actor(name, type, location=[0,0,0], rotation=[0,0,0])` - Create actors
    - `delete_actor(name)` - Remove actors
    - `set_actor_transform(name, location=None, rotation=None, scale=None)` - Modify actor transform
    - `get_actor_properties(name)` - Get actor properties

    ## Blueprint Management
    - `create_blueprint(name, parent_class)` - Create new Blueprint classes
    - `add_component_to_blueprint(blueprint_name, component_type, component_name)` - Add components
    - `set_static_mesh_properties(blueprint_name, component_name, static_mesh)` - Configure meshes
    - `set_physics_properties(blueprint_name, component_name)` - Configure physics
    - `compile_blueprint(blueprint_name)` - Compile Blueprint changes
    - `set_blueprint_property(blueprint_name, property_name, property_value)` - Set properties
    - `set_pawn_properties(blueprint_name)` - Configure Pawn settings
    - `spawn_blueprint_actor(blueprint_name, actor_name, location=[0,0,0], rotation=[0,0,0])` - Spawn Blueprint actors

    ## Blueprint Node Management
    - `add_blueprint_event_node(blueprint_name, event_type)` - Add event nodes
    - `add_blueprint_input_action_node(blueprint_name, action_name)` - Add input nodes
    - `add_blueprint_function_node(blueprint_name, target, function_name)` - Add function nodes
    - `connect_blueprint_nodes(blueprint_name, source_node_id, source_pin, target_node_id, target_pin)` - Connect nodes
    - `add_blueprint_variable(blueprint_name, variable_name, variable_type)` - Add variables
    - `add_blueprint_get_self_component_reference(blueprint_name, component_name)` - Add component refs
    - `add_blueprint_self_reference(blueprint_name)` - Add self references
    - `find_blueprint_nodes(blueprint_name, node_type, event_type)` - Find nodes

    ## Material Management
    - `create_material(material_path)` - Create a material asset
    - `set_material_properties(material_path, properties)` - Set material asset properties
    - `add_material_expression(...)` / `rebuild_material_graph(...)` - Material graph editing
    - `configure_glass_material(material_path, ...)` - Build a realistic glass preset

    ## Project Tools
    - `create_input_mapping(action_name, key, input_type)` - Create input mappings

    ## Workflow tools (prefer these over long tool chains)
    - `editor_preflight()` - Bridge + level + viewport readiness
    - `spawn_actor_with_material(class_path, material_path, name="", ...)` - Spawn + assign material
    - `create_and_rebuild_material(material_path, graph_spec, validate=True)` - Create + atomic material graph
    - `create_blueprint_with_graph(name, graph_spec, parent_class="Actor", ...)` - Create + atomic BP graph

    ## Best Practices
    - Call `editor_preflight` (or `get_bridge_status`) before large mutation sequences
    - Prefer atomic rebuild APIs and workflow tools over many fine-grained steps
    - Compile Blueprints after graph changes
    - Use unique actor names; never force-reuse names that may still exist
    - Take screenshots to verify viewport state
    """


if __name__ == "__main__":
    logger.info("Starting MCP server with stdio transport (protocol %s)", PROTOCOL_VERSION)
    anyio.run(run_fastmcp_stdio, mcp)
