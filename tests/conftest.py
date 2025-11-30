"""
MCP Test Configuration and Fixtures
"""
import json
import os
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
STARTUP_TIMEOUT = 15  # seconds


class MCPClient:
    """Client for communicating with MCP server via IPC bridge"""

    def __init__(self, socket_path: str = IPC_SOCKET_PATH, max_retries: int = 3):
        self.socket_path = socket_path
        self._request_id = 0
        self._max_retries = max_retries

    def send_request(self, method: str, params: Optional[Dict] = None, timeout: float = 5.0) -> Dict[str, Any]:
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

                # Read response
                response_data = b''
                while True:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    response_data += chunk
                    # Check if we have complete JSON
                    try:
                        return json.loads(response_data.decode())
                    except json.JSONDecodeError:
                        continue

                if response_data:
                    return json.loads(response_data.decode())
                else:
                    raise ConnectionError("Empty response from server")
            except (socket.error, ConnectionError, json.JSONDecodeError) as e:
                last_error = e
                if attempt < self._max_retries - 1:
                    time.sleep(0.2 * (attempt + 1))  # Exponential backoff
            finally:
                sock.close()

        raise last_error or ConnectionError("Failed after retries")

    def ping(self) -> Dict[str, Any]:
        """Ping the server"""
        return self.send_request("ping")

    def get_dialogs(self) -> Dict[str, Any]:
        """Get list of dialogs/chats"""
        return self.send_request("get_dialogs")

    def get_messages(self, chat_id: int, limit: int = 50) -> Dict[str, Any]:
        """Get messages from a chat"""
        return self.send_request("get_messages", {"chat_id": chat_id, "limit": limit})

    def search_local(self, query: str, limit: int = 20) -> Dict[str, Any]:
        """Search local messages"""
        return self.send_request("search_local", {"query": query, "limit": limit})


class TelegramProcess:
    """Manages Telegram process for testing"""

    def __init__(self, app_path: str = APP_PATH):
        self.app_path = app_path
        self.process: Optional[subprocess.Popen] = None

    def start(self, timeout: int = STARTUP_TIMEOUT) -> bool:
        """Start Telegram with MCP flag"""
        if self.process is not None:
            return True

        # Start process
        self.process = subprocess.Popen(
            [self.app_path, "--mcp"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # Wait for IPC socket to be available
        start_time = time.time()
        while time.time() - start_time < timeout:
            if os.path.exists(IPC_SOCKET_PATH):
                # Verify socket is responsive
                try:
                    client = MCPClient()
                    response = client.ping()
                    if response.get("result", {}).get("status") == "pong":
                        return True
                except Exception:
                    pass
            time.sleep(0.5)

        return False

    def stop(self):
        """Stop Telegram process"""
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
            self.process = None

    def is_running(self) -> bool:
        """Check if process is running"""
        if self.process is None:
            return False
        return self.process.poll() is None


# Module-level fixtures
_telegram_process: Optional[TelegramProcess] = None


def get_telegram_process() -> TelegramProcess:
    """Get or create Telegram process singleton"""
    global _telegram_process
    if _telegram_process is None:
        _telegram_process = TelegramProcess()
    return _telegram_process


@pytest.fixture(scope="session")
def telegram_process():
    """Session-scoped fixture for Telegram process"""
    process = get_telegram_process()

    # Check if already running (external)
    if os.path.exists(IPC_SOCKET_PATH):
        try:
            client = MCPClient()
            response = client.ping()
            if response.get("result", {}).get("status") == "pong":
                yield process
                return
        except Exception:
            pass

    # Start our own process
    if not process.start():
        pytest.skip("Failed to start Telegram with MCP")

    yield process

    # Cleanup
    process.stop()


@pytest.fixture
def mcp_client(telegram_process) -> MCPClient:
    """Fixture providing MCP client"""
    return MCPClient()


@pytest.fixture
def ensure_telegram_running():
    """Fixture that ensures Telegram is running (doesn't manage lifecycle)"""
    if not os.path.exists(IPC_SOCKET_PATH):
        pytest.skip("Telegram not running with --mcp flag. Start it first.")

    try:
        client = MCPClient()
        response = client.ping()
        if response.get("result", {}).get("status") != "pong":
            pytest.skip("MCP server not responding")
    except Exception as e:
        pytest.skip(f"Cannot connect to MCP server: {e}")
