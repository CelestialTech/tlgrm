# Monitoring and Observability

pythonMCP includes comprehensive monitoring capabilities through Prometheus metrics and health checks.

## Features

### 1. Prometheus Metrics

Exposes detailed performance and usage metrics at `http://localhost:9090/metrics` (configurable).

#### Available Metrics

**Tool Call Metrics:**
- `pythonmcp_tool_calls_total` - Total number of MCP tool calls by tool name and status
- `pythonmcp_tool_call_duration_seconds` - Histogram of tool call latencies
- `pythonmcp_tool_call_errors_total` - Count of errors by tool name and error type

**Message Retrieval Metrics:**
- `pythonmcp_messages_retrieved_total` - Total messages retrieved by source (bridge/telegram_api/cache)
- `pythonmcp_messages_retrieved_batch_size` - Histogram of batch sizes per request

**Bridge Metrics:**
- `pythonmcp_bridge_requests_total` - Total requests to C++ bridge by operation and status
- `pythonmcp_bridge_request_duration_seconds` - Histogram of bridge request latencies

**Telegram API Metrics:**
- `pythonmcp_telegram_api_requests_total` - Total Telegram API requests by method and status
- `pythonmcp_telegram_api_request_duration_seconds` - Histogram of API request latencies

**Cache Metrics:**
- `pythonmcp_cache_hits_total` - Total cache hits by cache type
- `pythonmcp_cache_misses_total` - Total cache misses by cache type
- `pythonmcp_cache_size` - Current number of items in cache

**AI/ML Metrics:**
- `pythonmcp_aiml_requests_total` - Total AI/ML requests by operation and status
- `pythonmcp_aiml_request_duration_seconds` - Histogram of AI/ML request latencies
- `pythonmcp_aiml_embeddings_generated_total` - Total embeddings generated

**System Metrics:**
- `pythonmcp_active_connections` - Number of active MCP connections
- `pythonmcp_server_uptime_seconds` - Server uptime in seconds

### 2. Health Checks

Exposes health check endpoints at `http://localhost:8080` (configurable).

**Endpoints:**
- `GET /health/live` - Liveness probe (is the server running?)
- `GET /health/ready` - Readiness probe (can it serve traffic?)
- `GET /health/components` - Detailed component status

## Configuration

### Enable Monitoring

Add to your `config.toml`:

```toml
[monitoring]
# Prometheus metrics
enable_prometheus = true
metrics_port = 9090

# Health checks
enable_health_check = true
health_check_port = 8080
```

### Environment Variables

You can also configure via environment variables:
```bash
export ENABLE_PROMETHEUS=true
export METRICS_PORT=9090
export HEALTH_CHECK_PORT=8080
```

## Usage

### Starting the Server with Metrics

```bash
# Start with default config (metrics enabled if configured)
python -m src.mcp_server

# Specify custom config
python -m src.mcp_server --config config.toml
```

### Accessing Metrics

Once the server is running:

```bash
# View Prometheus metrics
curl http://localhost:9090/metrics

# Check health (liveness)
curl http://localhost:8080/health/live

# Check readiness
curl http://localhost:8080/health/ready

# View component details
curl http://localhost:8080/health/components
```

### Prometheus Integration

Configure Prometheus to scrape pythonMCP metrics:

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'pythonmcp'
    static_configs:
      - targets: ['localhost:9090']
    scrape_interval: 15s
```

### Grafana Dashboard

Example queries for Grafana:

**Tool Call Rate:**
```promql
rate(pythonmcp_tool_calls_total[5m])
```

**Tool Call Latency (p95):**
```promql
histogram_quantile(0.95, rate(pythonmcp_tool_call_duration_seconds_bucket[5m]))
```

**Error Rate:**
```promql
rate(pythonmcp_tool_call_errors_total[5m])
```

**Bridge vs API Usage:**
```promql
sum by (source) (rate(pythonmcp_messages_retrieved_total[5m]))
```

## Programmatic Usage

### Using PrometheusMetrics Directly

```python
from src.monitoring import PrometheusMetrics

# Initialize metrics server
metrics = PrometheusMetrics(port=9090)
metrics.start()

# Record tool call
metrics.record_tool_call(
    tool_name="get_messages",
    duration=0.234,
    status="success"
)

# Record messages retrieved
metrics.record_messages_retrieved(count=50, source="bridge")

# Record bridge request
metrics.record_bridge_request(
    operation="get_messages",
    duration=0.015,
    status="success"
)

# Record Telegram API request
metrics.record_telegram_api_request(
    method="get_messages",
    duration=1.234,
    status="success"
)

# Record AI/ML request
metrics.record_aiml_request(
    operation="semantic_search",
    duration=0.456,
    status="success"
)
```

### Using the @instrument_tool Decorator

```python
from src.monitoring import instrument_tool

class MyServer:
    def __init__(self):
        self.metrics = PrometheusMetrics()
        self.metrics.start()

    @instrument_tool("my_custom_tool")
    async def my_custom_tool(self, arg1: str, arg2: int):
        # Tool implementation
        # Metrics are automatically recorded
        pass
```

The decorator automatically:
- Measures execution time
- Records success/error status
- Captures error types
- Updates all relevant metrics

## Kubernetes Deployment

### Readiness and Liveness Probes

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: pythonmcp
spec:
  containers:
  - name: pythonmcp
    image: pythonmcp:latest
    ports:
    - containerPort: 9090
      name: metrics
    - containerPort: 8080
      name: health
    livenessProbe:
      httpGet:
        path: /health/live
        port: 8080
      initialDelaySeconds: 30
      periodSeconds: 10
    readinessProbe:
      httpGet:
        path: /health/ready
        port: 8080
      initialDelaySeconds: 5
      periodSeconds: 5
```

### Service Monitor (Prometheus Operator)

```yaml
apiVersion: monitoring.coreos.com/v1
kind: ServiceMonitor
metadata:
  name: pythonmcp
spec:
  selector:
    matchLabels:
      app: pythonmcp
  endpoints:
  - port: metrics
    interval: 30s
```

## Troubleshooting

### Metrics Not Available

1. Check configuration:
   ```bash
   # Verify monitoring is enabled
   grep -A 5 "\[monitoring\]" config.toml
   ```

2. Check logs:
   ```bash
   # Look for metrics initialization
   grep "metrics" server.log
   ```

3. Check port availability:
   ```bash
   # Ensure port is not in use
   lsof -i :9090
   ```

### Health Check Failing

1. Check component status:
   ```bash
   curl http://localhost:8080/health/components | jq
   ```

2. Review logs for component failures:
   ```bash
   grep "component.*failed" server.log
   ```

## Best Practices

1. **Always enable metrics in production** - Essential for debugging and performance monitoring
2. **Set up alerting** - Create alerts for high error rates, slow requests, etc.
3. **Monitor cache hit rates** - Low cache hit rates may indicate configuration issues
4. **Track bridge vs API usage** - Bridge should be preferred for performance
5. **Set up dashboards** - Create Grafana dashboards for at-a-glance monitoring
6. **Use health checks** - Configure Kubernetes/Docker to use health endpoints
7. **Monitor resource usage** - Track system metrics alongside application metrics

## Security Considerations

- Metrics endpoints expose operational data (request counts, latencies)
- **Do not expose metrics ports publicly** - Use firewall rules or network policies
- Consider authentication for metrics endpoint in production
- Health check endpoints can be public but provide minimal information

## Performance Impact

- Metrics collection has minimal overhead (<1% CPU, <10MB memory)
- Histogram buckets are optimized for typical latencies
- Metrics are collected in-memory and scraped periodically
- No disk I/O for metrics (unlike logging)
