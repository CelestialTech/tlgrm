#!/usr/bin/env python3
"""
Prometheus Metrics for pythonMCP

Provides comprehensive monitoring and observability through Prometheus metrics.

Metrics tracked:
- tool_calls_total: Count of MCP tool calls by tool name
- tool_call_duration_seconds: Latency of tool calls
- tool_call_errors_total: Count of errors by tool name
- messages_retrieved_total: Total messages retrieved
- bridge_requests_total: Requests to C++ bridge
- telegram_api_requests_total: Requests to Telegram API
"""

import asyncio
import time
import functools
from typing import Optional, Callable
from http.server import HTTPServer, BaseHTTPRequestHandler
from threading import Thread

import structlog
from prometheus_client import (
    Counter,
    Histogram,
    Gauge,
    generate_latest,
    REGISTRY,
    CONTENT_TYPE_LATEST,
)

logger = structlog.get_logger()


class MetricsHandler(BaseHTTPRequestHandler):
    """HTTP request handler for Prometheus metrics endpoint."""

    def do_GET(self):
        """Handle GET requests to /metrics."""
        if self.path == "/metrics":
            self._handle_metrics()
        elif self.path == "/health":
            self._handle_health()
        else:
            self.send_error(404, "Not Found")

    def _handle_metrics(self):
        """Serve Prometheus metrics."""
        try:
            metrics_output = generate_latest(REGISTRY)
            self.send_response(200)
            self.send_header("Content-Type", CONTENT_TYPE_LATEST)
            self.end_headers()
            self.wfile.write(metrics_output)
        except Exception as e:
            logger.error("metrics.serve_failed", error=str(e))
            self.send_error(500, "Internal Server Error")

    def _handle_health(self):
        """Health check endpoint for metrics server."""
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"OK")

    def log_message(self, format: str, *args):
        """Override to use structlog instead of print."""
        logger.debug("metrics.request", path=self.path, method=self.command)


class PrometheusMetrics:
    """
    Prometheus metrics collector for pythonMCP.

    Tracks performance, usage, and errors across all components.
    """

    def __init__(self, port: int = 9090):
        """
        Initialize Prometheus metrics.

        Args:
            port: HTTP port for metrics endpoint (default: 9090)
        """
        self.port = port
        self.server: Optional[HTTPServer] = None
        self.thread: Optional[Thread] = None
        self._running = False

        # Initialize metrics
        self._init_metrics()

        logger.info("metrics.initialized", port=port)

    def _init_metrics(self):
        """Initialize all Prometheus metric collectors."""

        # Tool call metrics
        self.tool_calls_total = Counter(
            "pythonmcp_tool_calls_total",
            "Total number of MCP tool calls",
            ["tool_name", "status"],  # status: success, error
        )

        self.tool_call_duration_seconds = Histogram(
            "pythonmcp_tool_call_duration_seconds",
            "Duration of MCP tool calls in seconds",
            ["tool_name"],
            buckets=(0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0),
        )

        self.tool_call_errors_total = Counter(
            "pythonmcp_tool_call_errors_total",
            "Total number of tool call errors",
            ["tool_name", "error_type"],
        )

        # Message retrieval metrics
        self.messages_retrieved_total = Counter(
            "pythonmcp_messages_retrieved_total",
            "Total number of messages retrieved",
            ["source"],  # source: bridge, telegram_api, cache
        )

        self.messages_retrieved_batch_size = Histogram(
            "pythonmcp_messages_retrieved_batch_size",
            "Number of messages retrieved per request",
            ["source"],
            buckets=(1, 5, 10, 20, 50, 100, 200, 500),
        )

        # Bridge metrics
        self.bridge_requests_total = Counter(
            "pythonmcp_bridge_requests_total",
            "Total number of requests to C++ bridge",
            ["operation", "status"],  # operation: get_messages, search, list_chats
        )

        self.bridge_request_duration_seconds = Histogram(
            "pythonmcp_bridge_request_duration_seconds",
            "Duration of C++ bridge requests in seconds",
            ["operation"],
            buckets=(0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0),
        )

        # Telegram API metrics
        self.telegram_api_requests_total = Counter(
            "pythonmcp_telegram_api_requests_total",
            "Total number of requests to Telegram API",
            ["method", "status"],  # method: get_messages, search_messages, etc.
        )

        self.telegram_api_request_duration_seconds = Histogram(
            "pythonmcp_telegram_api_request_duration_seconds",
            "Duration of Telegram API requests in seconds",
            ["method"],
            buckets=(0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0),
        )

        # Cache metrics
        self.cache_hits_total = Counter(
            "pythonmcp_cache_hits_total",
            "Total number of cache hits",
            ["cache_type"],  # cache_type: message, chat, user
        )

        self.cache_misses_total = Counter(
            "pythonmcp_cache_misses_total",
            "Total number of cache misses",
            ["cache_type"],
        )

        self.cache_size = Gauge(
            "pythonmcp_cache_size",
            "Current number of items in cache",
            ["cache_type"],
        )

        # AI/ML metrics
        self.aiml_requests_total = Counter(
            "pythonmcp_aiml_requests_total",
            "Total number of AI/ML requests",
            ["operation", "status"],  # operation: semantic_search, classify_intent
        )

        self.aiml_request_duration_seconds = Histogram(
            "pythonmcp_aiml_request_duration_seconds",
            "Duration of AI/ML requests in seconds",
            ["operation"],
            buckets=(0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0),
        )

        self.aiml_embeddings_generated_total = Counter(
            "pythonmcp_aiml_embeddings_generated_total",
            "Total number of embeddings generated",
        )

        # System metrics
        self.active_connections = Gauge(
            "pythonmcp_active_connections",
            "Number of active MCP connections",
        )

        self.server_uptime_seconds = Gauge(
            "pythonmcp_server_uptime_seconds",
            "Server uptime in seconds",
        )

        logger.info("metrics.collectors_initialized")

    def record_tool_call(
        self,
        tool_name: str,
        duration: float,
        status: str = "success",
        error_type: Optional[str] = None,
    ):
        """
        Record a tool call execution.

        Args:
            tool_name: Name of the MCP tool
            duration: Execution duration in seconds
            status: "success" or "error"
            error_type: Type of error if status is "error"
        """
        self.tool_calls_total.labels(tool_name=tool_name, status=status).inc()
        self.tool_call_duration_seconds.labels(tool_name=tool_name).observe(duration)

        if status == "error" and error_type:
            self.tool_call_errors_total.labels(
                tool_name=tool_name, error_type=error_type
            ).inc()

        logger.debug(
            "metrics.tool_call_recorded",
            tool_name=tool_name,
            duration=duration,
            status=status,
        )

    def record_error(self, tool_name: str, error_type: str):
        """
        Record a tool call error.

        Args:
            tool_name: Name of the MCP tool
            error_type: Type/class of the error
        """
        self.tool_call_errors_total.labels(
            tool_name=tool_name, error_type=error_type
        ).inc()

        logger.debug(
            "metrics.error_recorded", tool_name=tool_name, error_type=error_type
        )

    def record_bridge_request(
        self,
        operation: str,
        duration: float,
        status: str = "success",
    ):
        """
        Record a C++ bridge request.

        Args:
            operation: Bridge operation name (get_messages, search, etc.)
            duration: Request duration in seconds
            status: "success" or "error"
        """
        self.bridge_requests_total.labels(operation=operation, status=status).inc()
        self.bridge_request_duration_seconds.labels(operation=operation).observe(
            duration
        )

        logger.debug(
            "metrics.bridge_request_recorded",
            operation=operation,
            duration=duration,
            status=status,
        )

    def record_telegram_api_request(
        self,
        method: str,
        duration: float,
        status: str = "success",
    ):
        """
        Record a Telegram API request.

        Args:
            method: API method name
            duration: Request duration in seconds
            status: "success" or "error"
        """
        self.telegram_api_requests_total.labels(method=method, status=status).inc()
        self.telegram_api_request_duration_seconds.labels(method=method).observe(
            duration
        )

        logger.debug(
            "metrics.telegram_api_request_recorded",
            method=method,
            duration=duration,
            status=status,
        )

    def record_messages_retrieved(self, count: int, source: str):
        """
        Record messages retrieved.

        Args:
            count: Number of messages retrieved
            source: Source of messages (bridge, telegram_api, cache)
        """
        self.messages_retrieved_total.labels(source=source).inc(count)
        self.messages_retrieved_batch_size.labels(source=source).observe(count)

        logger.debug(
            "metrics.messages_retrieved_recorded", count=count, source=source
        )

    def record_cache_access(self, cache_type: str, hit: bool):
        """
        Record cache access (hit or miss).

        Args:
            cache_type: Type of cache (message, chat, user)
            hit: True if cache hit, False if miss
        """
        if hit:
            self.cache_hits_total.labels(cache_type=cache_type).inc()
        else:
            self.cache_misses_total.labels(cache_type=cache_type).inc()

        logger.debug("metrics.cache_access_recorded", cache_type=cache_type, hit=hit)

    def update_cache_size(self, cache_type: str, size: int):
        """
        Update cache size gauge.

        Args:
            cache_type: Type of cache
            size: Current size
        """
        self.cache_size.labels(cache_type=cache_type).set(size)

    def record_aiml_request(
        self,
        operation: str,
        duration: float,
        status: str = "success",
    ):
        """
        Record an AI/ML request.

        Args:
            operation: AI/ML operation (semantic_search, classify_intent, etc.)
            duration: Request duration in seconds
            status: "success" or "error"
        """
        self.aiml_requests_total.labels(operation=operation, status=status).inc()
        self.aiml_request_duration_seconds.labels(operation=operation).observe(
            duration
        )

        logger.debug(
            "metrics.aiml_request_recorded",
            operation=operation,
            duration=duration,
            status=status,
        )

    def record_embedding_generated(self):
        """Record that an embedding was generated."""
        self.aiml_embeddings_generated_total.inc()

    def start(self):
        """Start the Prometheus metrics HTTP server."""
        if self._running:
            logger.warning("metrics.already_running")
            return

        try:
            # Create HTTP server
            self.server = HTTPServer(("0.0.0.0", self.port), MetricsHandler)

            # Start server in background thread
            self.thread = Thread(target=self._run_server, daemon=True)
            self.thread.start()

            self._running = True
            logger.info("metrics.server_started", port=self.port, endpoint="/metrics")

        except Exception as e:
            logger.error("metrics.start_failed", error=str(e), port=self.port)
            raise

    def _run_server(self):
        """Run the HTTP server (called in background thread)."""
        try:
            self.server.serve_forever()
        except Exception as e:
            logger.error("metrics.server_error", error=str(e))

    def stop(self):
        """Stop the metrics server."""
        if not self._running:
            return

        try:
            if self.server:
                self.server.shutdown()
                self.server.server_close()

            if self.thread:
                self.thread.join(timeout=5.0)

            self._running = False
            logger.info("metrics.stopped")

        except Exception as e:
            logger.error("metrics.stop_failed", error=str(e))

    def is_running(self) -> bool:
        """Check if the metrics server is running."""
        return self._running


def instrument_tool(tool_name: str):
    """
    Decorator to automatically track metrics for MCP tools.

    Measures execution time and records success/failure status.

    Usage:
        @instrument_tool("get_messages")
        async def get_messages(chat_id: int, limit: int = 50):
            # ... implementation
            pass

    Args:
        tool_name: Name of the tool being instrumented
    """

    def decorator(func: Callable) -> Callable:
        @functools.wraps(func)
        async def async_wrapper(*args, **kwargs):
            # Get metrics instance from global state or first arg (self)
            metrics = None
            if args and hasattr(args[0], "metrics"):
                metrics = args[0].metrics

            start_time = time.time()
            status = "success"
            error_type = None

            try:
                result = await func(*args, **kwargs)
                return result
            except Exception as e:
                status = "error"
                error_type = type(e).__name__
                raise
            finally:
                duration = time.time() - start_time

                # Record metrics if available
                if metrics:
                    metrics.record_tool_call(
                        tool_name=tool_name,
                        duration=duration,
                        status=status,
                        error_type=error_type,
                    )

        @functools.wraps(func)
        def sync_wrapper(*args, **kwargs):
            # Get metrics instance
            metrics = None
            if args and hasattr(args[0], "metrics"):
                metrics = args[0].metrics

            start_time = time.time()
            status = "success"
            error_type = None

            try:
                result = func(*args, **kwargs)
                return result
            except Exception as e:
                status = "error"
                error_type = type(e).__name__
                raise
            finally:
                duration = time.time() - start_time

                # Record metrics if available
                if metrics:
                    metrics.record_tool_call(
                        tool_name=tool_name,
                        duration=duration,
                        status=status,
                        error_type=error_type,
                    )

        # Return appropriate wrapper based on function type
        if asyncio.iscoroutinefunction(func):
            return async_wrapper
        else:
            return sync_wrapper

    return decorator
