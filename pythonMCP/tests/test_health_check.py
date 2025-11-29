"""
Unit tests for Health Check HTTP Server

Tests the health check monitoring system including:
- HTTP endpoints (liveness, readiness, components)
- Component registration and status tracking
- Server lifecycle management
"""

import pytest
import json
import time
import urllib.request
import urllib.error
from datetime import datetime
from threading import Thread

from src.monitoring.health import (
    HealthCheckServer,
    HealthCheckHandler,
    ComponentStatus
)


class TestHealthCheckEndpoints:
    """Tests for HTTP endpoint functionality"""

    @pytest.fixture
    def health_server(self):
        """Create and start a health check server instance for testing"""
        server = HealthCheckServer(port=0)  # Port 0 lets OS assign free port
        server.start()
        time.sleep(0.1)  # Give server time to start
        yield server
        server.stop()

    def _make_request(self, port, path):
        """Make HTTP request and return response"""
        url = f"http://localhost:{port}{path}"
        try:
            with urllib.request.urlopen(url, timeout=2) as response:
                return response.status, json.loads(response.read().decode())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read().decode()) if e.code != 404 else {}

    @pytest.mark.unit
    def test_liveness_endpoint_returns_200(self, health_server):
        """Test /health/live returns 200 OK"""
        port = health_server.server.server_address[1]

        status, response = self._make_request(port, '/health/live')

        assert status == 200
        assert response['status'] == 'alive'
        assert 'timestamp' in response

    @pytest.mark.unit
    def test_readiness_endpoint_all_healthy_returns_200(self, health_server):
        """Test /health/ready returns 200 when all components healthy"""
        # Register healthy components
        health_server.register_component("test_component_1", lambda: True)
        health_server.register_component("test_component_2", lambda: True)

        port = health_server.server.server_address[1]
        status, response = self._make_request(port, '/health/ready')

        assert status == 200
        assert response['status'] == 'ready'
        assert 'components' in response
        assert response['components']['test_component_1'] is True
        assert response['components']['test_component_2'] is True

    @pytest.mark.unit
    def test_readiness_endpoint_unhealthy_component_returns_503(self, health_server):
        """Test /health/ready returns 503 when any component unhealthy"""
        # Register mixed healthy/unhealthy components
        health_server.register_component("healthy_component", lambda: True)
        health_server.register_component("unhealthy_component", lambda: False)

        port = health_server.server.server_address[1]
        status, response = self._make_request(port, '/health/ready')

        assert status == 503
        assert response['status'] == 'not_ready'
        assert response['components']['healthy_component'] is True
        assert response['components']['unhealthy_component'] is False

    @pytest.mark.unit
    def test_components_endpoint_returns_detailed_status(self, health_server):
        """Test /health/components returns detailed component information"""
        # Register components
        health_server.register_component("component_1", lambda: True)
        health_server.register_component("component_2", lambda: False)

        port = health_server.server.server_address[1]
        status, response = self._make_request(port, '/health/components')

        assert status == 200
        assert 'timestamp' in response
        assert 'components' in response

        # Check component details
        comp1 = response['components']['component_1']
        assert comp1['healthy'] is True
        assert comp1['message'] == 'OK'
        assert 'last_check' in comp1

        comp2 = response['components']['component_2']
        assert comp2['healthy'] is False
        assert comp2['message'] == 'Component unavailable'
        assert 'last_check' in comp2

    @pytest.mark.unit
    def test_unknown_endpoint_returns_404(self, health_server):
        """Test unknown endpoint returns 404 Not Found"""
        port = health_server.server.server_address[1]
        status, _ = self._make_request(port, '/health/unknown')

        assert status == 404


class TestComponentRegistration:
    """Tests for component registration and health checks"""

    @pytest.fixture
    def health_server(self):
        """Create a health check server instance"""
        server = HealthCheckServer(port=0)
        yield server
        if server.is_running():
            server.stop()

    @pytest.mark.unit
    def test_register_component_success(self, health_server):
        """Test successful component registration"""
        # Define a simple health check
        def check_fn():
            return True

        # Register component
        health_server.register_component("test_component", check_fn)

        # Verify component is registered
        assert "test_component" in health_server.component_checks
        assert health_server.component_checks["test_component"] == check_fn

    @pytest.mark.unit
    def test_component_check_executes(self, health_server):
        """Test that component check function is executed"""
        # Track if check was called
        check_called = {'called': False}

        def mock_check():
            check_called['called'] = True
            return True

        # Register component
        health_server.register_component("test_component", mock_check)

        # Get statuses (which should execute checks)
        statuses = health_server.get_component_statuses()

        # Verify check was called
        assert check_called['called'] is True

        # Verify status is correct
        assert "test_component" in statuses
        status = statuses["test_component"]
        assert isinstance(status, ComponentStatus)
        assert status.name == "test_component"
        assert status.healthy is True
        assert status.message == "OK"
        assert status.last_check is not None

    @pytest.mark.unit
    def test_component_check_handles_exceptions(self, health_server):
        """Test that exceptions in component checks are handled gracefully"""
        # Create a check function that raises an exception
        def failing_check():
            raise RuntimeError("Component check failed")

        # Register component
        health_server.register_component("failing_component", failing_check)

        # Get statuses (should not raise exception)
        statuses = health_server.get_component_statuses()

        # Verify status reflects the failure
        assert "failing_component" in statuses
        status = statuses["failing_component"]
        assert status.healthy is False
        assert "Check failed: Component check failed" in status.message
        assert status.last_check is not None

    @pytest.mark.unit
    def test_multiple_component_registration(self, health_server):
        """Test registering multiple components"""
        # Register multiple components
        health_server.register_component("component_1", lambda: True)
        health_server.register_component("component_2", lambda: False)
        health_server.register_component("component_3", lambda: True)

        # Get statuses
        statuses = health_server.get_component_statuses()

        # Verify all components are tracked
        assert len(statuses) == 3
        assert statuses["component_1"].healthy is True
        assert statuses["component_2"].healthy is False
        assert statuses["component_3"].healthy is True

    @pytest.mark.unit
    def test_component_status_timestamp(self, health_server):
        """Test that component status includes timestamp"""
        health_server.register_component("test", lambda: True)

        # Get status
        statuses = health_server.get_component_statuses()
        status = statuses["test"]

        # Verify timestamp is recent
        assert status.last_check is not None
        assert isinstance(status.last_check, datetime)

        # Should be within last second
        time_diff = (datetime.utcnow() - status.last_check).total_seconds()
        assert time_diff < 1.0


class TestServerLifecycle:
    """Tests for server startup, shutdown, and lifecycle management"""

    @pytest.fixture
    def health_server(self):
        """Create a health check server instance"""
        server = HealthCheckServer(port=0)
        yield server
        if server.is_running():
            server.stop()

    @pytest.mark.unit
    def test_server_starts_successfully(self, health_server):
        """Test that server starts without errors"""
        # Start server
        health_server.start()
        time.sleep(0.1)  # Give server time to start

        # Verify server is marked as running
        assert health_server.is_running() is True
        assert health_server._running is True
        assert health_server.server is not None

    @pytest.mark.unit
    def test_server_stops_cleanly(self, health_server):
        """Test that server stops cleanly"""
        # Start server
        health_server.start()
        time.sleep(0.1)
        assert health_server.is_running() is True

        # Stop server
        health_server.stop()

        # Verify server is stopped
        assert health_server.is_running() is False
        assert health_server._running is False

    @pytest.mark.unit
    def test_server_runs_in_background_thread(self, health_server):
        """Test that server runs in a background daemon thread"""
        # Start server
        health_server.start()
        time.sleep(0.1)

        # Verify thread was created and is daemon
        assert health_server.thread is not None
        assert isinstance(health_server.thread, Thread)
        assert health_server.thread.daemon is True
        assert health_server.thread.is_alive() is True

    @pytest.mark.unit
    def test_start_already_running_server_does_nothing(self, health_server):
        """Test that starting an already running server is a no-op"""
        # Start server
        health_server.start()
        time.sleep(0.1)
        first_server = health_server.server

        # Try to start again
        health_server.start()

        # Should still be the same server instance
        assert health_server.server is first_server

    @pytest.mark.unit
    def test_stop_not_running_server_does_nothing(self, health_server):
        """Test that stopping a non-running server doesn't raise errors"""
        # Server is not started, should be safe to stop
        assert health_server.is_running() is False

        # Should not raise exception
        health_server.stop()

        assert health_server.is_running() is False

    @pytest.mark.unit
    def test_server_initialization(self):
        """Test server initialization with custom port"""
        server = HealthCheckServer(port=18082)

        assert server.port == 18082
        assert server.server is None
        assert server.thread is None
        assert server._running is False
        assert server.component_checks == {}

    @pytest.mark.unit
    def test_handler_class_variable_set(self, health_server):
        """Test that handler class variable is set when server starts"""
        # Initially should be None or previous value
        original_value = HealthCheckHandler.health_server

        # Start server
        health_server.start()
        time.sleep(0.1)

        # Handler should reference this server
        assert HealthCheckHandler.health_server is health_server

        # Cleanup
        health_server.stop()
        # Restore to avoid test pollution
        HealthCheckHandler.health_server = original_value


class TestComponentStatus:
    """Tests for ComponentStatus dataclass"""

    @pytest.mark.unit
    def test_component_status_creation(self):
        """Test creating a ComponentStatus instance"""
        now = datetime.utcnow()
        status = ComponentStatus(
            name="test_component",
            healthy=True,
            message="All good",
            last_check=now
        )

        assert status.name == "test_component"
        assert status.healthy is True
        assert status.message == "All good"
        assert status.last_check == now

    @pytest.mark.unit
    def test_component_status_defaults(self):
        """Test ComponentStatus default values"""
        status = ComponentStatus(
            name="test",
            healthy=False
        )

        assert status.name == "test"
        assert status.healthy is False
        assert status.message == ""
        assert status.last_check is None


class TestIntegration:
    """Integration tests combining multiple aspects"""

    @pytest.mark.integration
    def test_full_health_check_workflow(self):
        """Test complete workflow: register components, start server, check endpoints"""
        server = HealthCheckServer(port=0)

        try:
            # Register components
            server.register_component("database", lambda: True)
            server.register_component("cache", lambda: True)
            server.register_component("api", lambda: False)

            # Start server
            server.start()
            time.sleep(0.1)
            assert server.is_running()

            port = server.server.server_address[1]

            # Test liveness check
            url = f"http://localhost:{port}/health/live"
            with urllib.request.urlopen(url, timeout=2) as response:
                assert response.status == 200

            # Test readiness check (should be not ready due to api=False)
            url = f"http://localhost:{port}/health/ready"
            try:
                urllib.request.urlopen(url, timeout=2)
                assert False, "Should have returned 503"
            except urllib.error.HTTPError as e:
                assert e.code == 503

            # Stop server
            server.stop()
            assert not server.is_running()

        finally:
            if server.is_running():
                server.stop()

    @pytest.mark.integration
    def test_component_check_state_changes(self):
        """Test that component health status can change over time"""
        server = HealthCheckServer(port=0)

        try:
            # Create a component with changeable state
            component_state = {"healthy": True}

            def check_component():
                return component_state["healthy"]

            server.register_component("dynamic_component", check_component)

            # First check - healthy
            statuses = server.get_component_statuses()
            assert statuses["dynamic_component"].healthy is True

            # Change state to unhealthy
            component_state["healthy"] = False

            # Second check - unhealthy
            statuses = server.get_component_statuses()
            assert statuses["dynamic_component"].healthy is False

            # Change back to healthy
            component_state["healthy"] = True

            # Third check - healthy again
            statuses = server.get_component_statuses()
            assert statuses["dynamic_component"].healthy is True

        finally:
            if server.is_running():
                server.stop()


class TestErrorHandling:
    """Tests for error handling and edge cases"""

    @pytest.mark.unit
    @pytest.mark.xfail(reason="Port 1 binding behavior varies by OS/kernel - may not always raise")
    def test_server_start_failure_handling(self):
        """Test handling of server start failures"""
        # Try to start server on port 1 (requires root)
        server = HealthCheckServer(port=1)

        try:
            # Should raise permission error (OSError on some systems)
            with pytest.raises((PermissionError, OSError)):
                server.start()

            # Server should not be marked as running
            assert server.is_running() is False

        finally:
            if server.is_running():
                server.stop()

    @pytest.mark.unit
    def test_json_response_encoding(self):
        """Test that JSON responses are properly encoded"""
        server = HealthCheckServer(port=0)
        server.register_component("test", lambda: True)
        server.start()
        time.sleep(0.1)

        try:
            port = server.server.server_address[1]
            url = f"http://localhost:{port}/health/components"

            with urllib.request.urlopen(url, timeout=2) as response:
                data = response.read()
                # Verify data is bytes
                assert isinstance(data, bytes)

                # Verify it can be decoded as JSON
                decoded = json.loads(data.decode())
                assert 'components' in decoded
                assert 'test' in decoded['components']

        finally:
            server.stop()
