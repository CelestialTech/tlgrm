"""
MCP Test Configuration and Fixtures

This module automatically launches Tlgrm if not running and ensures MCP socket is available.
Tests should NOT skip - if Tlgrm can't be launched, tests FAIL.
"""
import json
import os
import signal
import socket
import subprocess
import time
import pytest
from typing import Optional, Dict, Any

# Configuration
APP_PATH = os.path.join(
    os.path.dirname(os.path.dirname(__file__)),
    "out/Release/Tlgrm.app/Contents/MacOS/Tlgrm"
)
IPC_SOCKET_PATH = "/tmp/tdesktop_mcp.sock"
STARTUP_TIMEOUT = 30  # seconds - increased for app startup
SOCKET_WAIT_TIMEOUT = 20  # seconds to wait for socket


class MCPClient:
    """Client for communicating with MCP server via IPC bridge"""

    def __init__(self, socket_path: str = IPC_SOCKET_PATH, max_retries: int = 5):
        self.socket_path = socket_path
        self._request_id = 0
        self._max_retries = max_retries

    def is_socket_available(self) -> bool:
        """Check if socket exists and is connectable"""
        if not os.path.exists(self.socket_path):
            return False
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(2.0)
            sock.connect(self.socket_path)
            sock.close()
            return True
        except (socket.error, OSError):
            return False

    def send_request(self, method: str, params: Optional[Dict] = None, timeout: float = 10.0) -> Dict[str, Any]:
        """Send JSON-RPC request and return response with retry logic"""
        self._request_id += 1
        request = {
            "jsonrpc": "2.0",
            "id": self._request_id,
            "method": method,
            "params": params or {}
        }

        last_error = None
        for attempt in range(self._max_retries):
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(timeout)

            try:
                sock.connect(self.socket_path)
                sock.sendall(json.dumps(request).encode() + b'\n')

                # Read response - use larger buffer for responses with Cyrillic/Unicode
                response_data = b''
                max_size = 2 * 1024 * 1024  # 2MB max response
                while len(response_data) < max_size:
                    chunk = sock.recv(16384)  # Larger buffer (16KB)
                    if not chunk:
                        break
                    response_data += chunk
                    # Check if we have complete JSON - use errors='replace' for partial UTF-8
                    try:
                        text = response_data.decode('utf-8', errors='replace')
                        return json.loads(text)
                    except json.JSONDecodeError:
                        continue

                if response_data:
                    # Final attempt - replace any invalid UTF-8 sequences
                    text = response_data.decode('utf-8', errors='replace')
                    return json.loads(text)
                else:
                    raise ConnectionError("Empty response from server")
            except (socket.error, ConnectionError, json.JSONDecodeError, UnicodeDecodeError) as e:
                last_error = e
                if attempt < self._max_retries - 1:
                    time.sleep(0.5 * (attempt + 1))  # Exponential backoff
            finally:
                sock.close()

        raise last_error or ConnectionError("Failed after retries")

    def ping(self) -> Dict[str, Any]:
        """Ping the server"""
        return self.send_request("ping")

    def list_chats(self, limit: int = 50) -> Dict[str, Any]:
        """Get list of chats via tools/call"""
        return self.send_request("tools/call", {
            "name": "list_chats",
            "arguments": {"limit": limit}
        }, timeout=15.0)


class TlgrmAppManager:
    """Manages Tlgrm application lifecycle for testing"""

    def __init__(self, app_path: str = APP_PATH):
        self.app_path = app_path
        self.process: Optional[subprocess.Popen] = None
        self._we_started_it = False

    def is_app_running(self) -> bool:
        """Check if Tlgrm is already running"""
        try:
            result = subprocess.run(
                ["pgrep", "-f", "Tlgrm"],
                capture_output=True,
                text=True,
                timeout=5
            )
            return result.returncode == 0
        except Exception:
            return False

    def wait_for_socket(self, timeout: int = SOCKET_WAIT_TIMEOUT) -> bool:
        """Wait for MCP socket to become available"""
        client = MCPClient()
        start_time = time.time()

        while time.time() - start_time < timeout:
            if client.is_socket_available():
                # Socket exists, try to connect and verify it responds
                try:
                    # Try a simple list_chats to verify session is ready
                    response = client.list_chats(limit=1)
                    if "result" in response:
                        return True
                except Exception:
                    pass
            time.sleep(1)

        return False

    def launch_app(self) -> bool:
        """Launch Tlgrm application"""
        if not os.path.exists(self.app_path):
            raise FileNotFoundError(f"Tlgrm app not found at {self.app_path}")

        # Kill any existing Tlgrm processes first
        subprocess.run(["pkill", "-9", "-f", "Tlgrm"], capture_output=True)
        time.sleep(2)

        # Remove old socket
        if os.path.exists(IPC_SOCKET_PATH):
            os.remove(IPC_SOCKET_PATH)

        # Launch the app
        self.process = subprocess.Popen(
            [self.app_path],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True
        )
        self._we_started_it = True

        # Wait for socket to be ready
        return self.wait_for_socket()

    def ensure_running(self) -> bool:
        """Ensure Tlgrm is running and MCP socket is available"""
        client = MCPClient()

        # First check if app is already running with socket available
        if self.is_app_running() and client.is_socket_available():
            # Verify it's responding
            try:
                response = client.list_chats(limit=1)
                if "result" in response:
                    print("Tlgrm already running with active session")
                    return True
            except Exception:
                pass

        # Need to launch the app
        print(f"Launching Tlgrm from {self.app_path}...")
        return self.launch_app()

    def stop(self):
        """Stop Tlgrm if we started it"""
        if self._we_started_it and self.process:
            try:
                os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
                self.process.wait(timeout=5)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                try:
                    os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
                except ProcessLookupError:
                    pass
            self.process = None


# Global app manager instance
_app_manager: Optional[TlgrmAppManager] = None


def get_app_manager() -> TlgrmAppManager:
    """Get or create the app manager singleton"""
    global _app_manager
    if _app_manager is None:
        _app_manager = TlgrmAppManager()
    return _app_manager


@pytest.fixture(scope="session", autouse=True)
def ensure_tlgrm_running():
    """
    Session-scoped fixture that ensures Tlgrm is running.
    This runs automatically before any tests.

    IMPORTANT: Tests should NOT skip. If Tlgrm can't start, tests FAIL.
    """
    manager = get_app_manager()

    if not manager.ensure_running():
        pytest.fail(
            f"FAILED to start Tlgrm and get MCP socket ready.\n"
            f"App path: {APP_PATH}\n"
            f"Socket path: {IPC_SOCKET_PATH}\n"
            f"Make sure:\n"
            f"  1. Tlgrm.app is built at {APP_PATH}\n"
            f"  2. You have a valid session (logged in at least once)\n"
            f"  3. MCP bridge creates socket at {IPC_SOCKET_PATH}"
        )

    yield manager

    # Cleanup - stop app if we started it
    manager.stop()


@pytest.fixture
def mcp_client(ensure_tlgrm_running) -> MCPClient:
    """Fixture providing MCP client - guaranteed to have working connection"""
    client = MCPClient()

    # Verify connection works
    try:
        response = client.list_chats(limit=1)
        if "error" in response and "No active session" in str(response.get("error", {})):
            pytest.fail("Tlgrm is running but has no active session. Please log in first.")
    except Exception as e:
        pytest.fail(f"Failed to connect to MCP socket: {e}")

    return client


@pytest.fixture
def app_manager(ensure_tlgrm_running) -> TlgrmAppManager:
    """Fixture providing app manager"""
    return get_app_manager()
