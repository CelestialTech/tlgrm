#!/usr/bin/env python3
"""
Health Check HTTP Server

Provides liveness and readiness endpoints for production deployments.

Endpoints:
- GET /health/live - Liveness probe (is the server running?)
- GET /health/ready - Readiness probe (can it serve traffic?)
- GET /health/components - Detailed component status
"""

import json
from typing import Optional, Dict, Any, Callable
from dataclasses import dataclass
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler
from threading import Thread

import structlog

logger = structlog.get_logger()


@dataclass
class ComponentStatus:
    """Status of a single component."""

    name: str
    healthy: bool
    message: str = ""
    last_check: Optional[datetime] = None


class HealthCheckHandler(BaseHTTPRequestHandler):
    """HTTP request handler for health check endpoints."""

    # Class variable to store the server instance reference
    health_server: 'HealthCheckServer' = None

    def do_GET(self):
        """Handle GET requests."""
        if self.path == "/health/live":
            self._handle_liveness()
        elif self.path == "/health/ready":
            self._handle_readiness()
        elif self.path == "/health/components":
            self._handle_components()
        else:
            self.send_error(404, "Not Found")

    def _handle_liveness(self):
        """Liveness probe - is the server running?"""
        response = {
            "status": "alive",
            "timestamp": datetime.utcnow().isoformat()
        }
        self._send_json_response(200, response)

    def _handle_readiness(self):
        """Readiness probe - can the server serve traffic?"""
        if not self.health_server:
            self.send_error(500, "Health server not initialized")
            return

        # Get component statuses
        components = self.health_server.get_component_statuses()

        # Server is ready if all components are healthy
        all_healthy = all(c.healthy for c in components.values())

        response = {
            "status": "ready" if all_healthy else "not_ready",
            "timestamp": datetime.utcnow().isoformat(),
            "components": {
                name: status.healthy
                for name, status in components.items()
            }
        }

        status_code = 200 if all_healthy else 503
        self._send_json_response(status_code, response)

    def _handle_components(self):
        """Detailed component status."""
        if not self.health_server:
            self.send_error(500, "Health server not initialized")
            return

        components = self.health_server.get_component_statuses()

        response = {
            "timestamp": datetime.utcnow().isoformat(),
            "components": {
                name: {
                    "healthy": status.healthy,
                    "message": status.message,
                    "last_check": status.last_check.isoformat() if status.last_check else None
                }
                for name, status in components.items()
            }
        }

        self._send_json_response(200, response)

    def _send_json_response(self, status_code: int, data: Dict[str, Any]):
        """Send JSON response."""
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(data, indent=2).encode())

    def log_message(self, format: str, *args):
        """Override to use structlog instead of print."""
        logger.info("health_check.request", path=self.path, method=self.command)


class HealthCheckServer:
    """
    HTTP server for health check endpoints.

    Runs in a separate thread to avoid blocking the main MCP server.
    """

    def __init__(self, port: int = 8080):
        self.port = port
        self.server: Optional[HTTPServer] = None
        self.thread: Optional[Thread] = None
        self.component_checks: Dict[str, Callable[[], bool]] = {}
        self._running = False

    def register_component(self, name: str, check_fn: Callable[[], bool]):
        """
        Register a component health check.

        Args:
            name: Component name (e.g., "bridge", "telegram", "aiml")
            check_fn: Function that returns True if component is healthy
        """
        self.component_checks[name] = check_fn
        logger.info("health_check.component_registered", component=name)

    def get_component_statuses(self) -> Dict[str, ComponentStatus]:
        """Get status of all registered components."""
        statuses = {}

        for name, check_fn in self.component_checks.items():
            try:
                healthy = check_fn()
                statuses[name] = ComponentStatus(
                    name=name,
                    healthy=healthy,
                    message="OK" if healthy else "Component unavailable",
                    last_check=datetime.utcnow()
                )
            except Exception as e:
                statuses[name] = ComponentStatus(
                    name=name,
                    healthy=False,
                    message=f"Check failed: {str(e)}",
                    last_check=datetime.utcnow()
                )
                logger.error("health_check.component_check_failed", component=name, error=str(e))

        return statuses

    def start(self):
        """Start the health check HTTP server in a background thread."""
        if self._running:
            logger.warning("health_check.already_running")
            return

        try:
            # Set class variable so handler can access this instance
            HealthCheckHandler.health_server = self

            # Create HTTP server
            self.server = HTTPServer(("0.0.0.0", self.port), HealthCheckHandler)

            # Start server in background thread
            self.thread = Thread(target=self._run_server, daemon=True)
            self.thread.start()

            self._running = True
            logger.info("health_check.started", port=self.port)

        except Exception as e:
            logger.error("health_check.start_failed", error=str(e), port=self.port)
            raise

    def _run_server(self):
        """Run the HTTP server (called in background thread)."""
        try:
            self.server.serve_forever()
        except Exception as e:
            logger.error("health_check.server_error", error=str(e))

    def stop(self):
        """Stop the health check server."""
        if not self._running:
            return

        try:
            if self.server:
                self.server.shutdown()
                self.server.server_close()

            if self.thread:
                self.thread.join(timeout=5.0)

            self._running = False
            logger.info("health_check.stopped")

        except Exception as e:
            logger.error("health_check.stop_failed", error=str(e))

    def is_running(self) -> bool:
        """Check if the health check server is running."""
        return self._running
