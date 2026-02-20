"""
MCP Protocol Compliance Tests

Tests JSON-RPC 2.0 and MCP protocol conformance for the IPC bridge.
These tests connect to the real MCP socket and validate response schemas.
"""
import json
import os
import socket
import pytest
from typing import Any, Dict, Optional


SOCKET_PATH = "/tmp/tdesktop_mcp.sock"
TIMEOUT = 10.0


def send_jsonrpc(method: str, params: Optional[Dict] = None, id: int = 1) -> Dict[str, Any]:
    """Send a JSON-RPC 2.0 request via Unix socket and return parsed response."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(TIMEOUT)
    try:
        sock.connect(SOCKET_PATH)
        request = {
            "jsonrpc": "2.0",
            "id": id,
            "method": method,
            "params": params or {},
        }
        sock.sendall(json.dumps(request).encode() + b"\n")

        data = b""
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
            try:
                return json.loads(data.decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue

        return json.loads(data.decode("utf-8"))
    finally:
        sock.close()


def socket_available() -> bool:
    if not os.path.exists(SOCKET_PATH):
        return False
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(2.0)
        s.connect(SOCKET_PATH)
        s.close()
        return True
    except (socket.error, OSError):
        return False


needs_socket = pytest.mark.skipif(
    not socket_available(),
    reason=f"MCP socket not available at {SOCKET_PATH}"
)


# ===== JSON-RPC 2.0 Compliance =====

class TestJSONRPCCompliance:
    """Test JSON-RPC 2.0 protocol compliance."""

    @needs_socket
    def test_response_has_jsonrpc_field(self):
        resp = send_jsonrpc("initialize", {
            "protocolVersion": "2024-11-05",
            "clientInfo": {"name": "test", "version": "1.0"},
            "capabilities": {},
        })
        assert resp.get("jsonrpc") == "2.0", "Response must include jsonrpc: '2.0'"

    @needs_socket
    def test_response_preserves_request_id(self):
        resp = send_jsonrpc("initialize", {
            "protocolVersion": "2024-11-05",
            "clientInfo": {"name": "test", "version": "1.0"},
            "capabilities": {},
        }, id=42)
        assert resp.get("id") == 42, "Response id must match request id"

    @needs_socket
    def test_response_has_result_xor_error(self):
        resp = send_jsonrpc("initialize", {
            "protocolVersion": "2024-11-05",
            "clientInfo": {"name": "test", "version": "1.0"},
            "capabilities": {},
        })
        has_result = "result" in resp
        has_error = "error" in resp
        assert has_result != has_error, "Response must have exactly one of 'result' or 'error'"

    @needs_socket
    def test_unknown_method_returns_error(self):
        resp = send_jsonrpc("nonexistent_method_xyz")
        assert "error" in resp, "Unknown method must return error"
        err = resp["error"]
        assert "code" in err, "Error must have 'code'"
        assert "message" in err, "Error must have 'message'"
        assert err["code"] == -32601, "Should be method-not-found (-32601)"


# ===== MCP Protocol Compliance =====

class TestMCPInitialize:
    """Test MCP initialize handshake."""

    @needs_socket
    def test_initialize_returns_server_info(self):
        resp = send_jsonrpc("initialize", {
            "protocolVersion": "2024-11-05",
            "clientInfo": {"name": "test", "version": "1.0"},
            "capabilities": {},
        })
        assert "result" in resp
        result = resp["result"]
        assert "serverInfo" in result, "Must include serverInfo"
        assert "name" in result["serverInfo"], "serverInfo must have name"

    @needs_socket
    def test_initialize_returns_protocol_version(self):
        resp = send_jsonrpc("initialize", {
            "protocolVersion": "2024-11-05",
            "clientInfo": {"name": "test", "version": "1.0"},
            "capabilities": {},
        })
        result = resp["result"]
        assert "protocolVersion" in result, "Must include protocolVersion"

    @needs_socket
    def test_initialize_returns_capabilities(self):
        resp = send_jsonrpc("initialize", {
            "protocolVersion": "2024-11-05",
            "clientInfo": {"name": "test", "version": "1.0"},
            "capabilities": {},
        })
        result = resp["result"]
        assert "capabilities" in result, "Must include capabilities"
        caps = result["capabilities"]
        assert isinstance(caps, dict), "Capabilities must be an object"


class TestMCPToolsList:
    """Test MCP tools/list endpoint."""

    @needs_socket
    def test_tools_list_returns_array(self):
        resp = send_jsonrpc("tools/list")
        assert "result" in resp
        result = resp["result"]
        assert "tools" in result, "Must include 'tools' array"
        assert isinstance(result["tools"], list), "Tools must be an array"

    @needs_socket
    def test_tools_have_required_fields(self):
        resp = send_jsonrpc("tools/list")
        tools = resp["result"]["tools"]
        assert len(tools) > 0, "Should have at least one tool"
        for tool in tools[:10]:  # Check first 10
            assert "name" in tool, f"Tool must have 'name': {tool}"
            assert "description" in tool, f"Tool must have 'description': {tool}"
            assert "inputSchema" in tool, f"Tool must have 'inputSchema': {tool}"

    @needs_socket
    def test_tool_input_schema_is_valid_json_schema(self):
        resp = send_jsonrpc("tools/list")
        tools = resp["result"]["tools"]
        for tool in tools[:10]:
            schema = tool["inputSchema"]
            assert isinstance(schema, dict), f"inputSchema must be object for {tool['name']}"
            assert schema.get("type") == "object", (
                f"inputSchema.type must be 'object' for {tool['name']}"
            )

    @needs_socket
    def test_tools_count_reasonable(self):
        resp = send_jsonrpc("tools/list")
        tools = resp["result"]["tools"]
        assert len(tools) >= 20, f"Expected 20+ tools, got {len(tools)}"

    @needs_socket
    def test_core_tools_present(self):
        resp = send_jsonrpc("tools/list")
        tool_names = {t["name"] for t in resp["result"]["tools"]}
        core = {"list_chats", "get_chat_info", "read_messages", "send_message", "search_messages"}
        missing = core - tool_names
        assert not missing, f"Core tools missing from tools/list: {missing}"


class TestMCPToolsCall:
    """Test MCP tools/call endpoint."""

    @needs_socket
    def test_call_list_chats(self):
        resp = send_jsonrpc("tools/call", {
            "name": "list_chats",
            "arguments": {},
        })
        assert "result" in resp, f"tools/call should succeed: {resp}"
        result = resp["result"]
        # MCP tools/call returns content array
        if "content" in result:
            assert isinstance(result["content"], list)
            for item in result["content"]:
                assert "type" in item
                assert "text" in item

    @needs_socket
    def test_call_unknown_tool(self):
        resp = send_jsonrpc("tools/call", {
            "name": "this_tool_does_not_exist",
            "arguments": {},
        })
        # Should return an error in content or as a JSON-RPC error
        has_error = "error" in resp
        has_result_error = (
            "result" in resp
            and "content" in resp.get("result", {})
            and any("error" in str(c.get("text", "")).lower()
                    for c in resp["result"]["content"])
        )
        assert has_error or has_result_error, "Unknown tool should indicate error"

    @needs_socket
    def test_call_health_check(self):
        resp = send_jsonrpc("tools/call", {
            "name": "health_check",
            "arguments": {},
        })
        assert "result" in resp
        # Parse the content text as JSON
        result = resp["result"]
        if "content" in result and len(result["content"]) > 0:
            text = result["content"][0].get("text", "{}")
            data = json.loads(text)
            assert "status" in data or "success" in data


class TestMCPResourcesList:
    """Test MCP resources/list endpoint."""

    @needs_socket
    def test_resources_list_returns_array(self):
        resp = send_jsonrpc("resources/list")
        assert "result" in resp
        result = resp["result"]
        assert "resources" in result, "Must include 'resources'"
        assert isinstance(result["resources"], list)

    @needs_socket
    def test_resources_have_required_fields(self):
        resp = send_jsonrpc("resources/list")
        resources = resp["result"]["resources"]
        for res in resources:
            assert "uri" in res, f"Resource must have 'uri': {res}"
            assert "name" in res, f"Resource must have 'name': {res}"
