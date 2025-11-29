# Prometheus Metrics - Quick Reference

## Quick Start

```bash
# Enable metrics in config.toml
[monitoring]
enable_prometheus = true
metrics_port = 9090

# Start server
python -m src.mcp_server

# View metrics
curl http://localhost:9090/metrics
```

## Metrics Summary

### Tool Performance
| Metric | Type | Description | Labels |
|--------|------|-------------|--------|
| `pythonmcp_tool_calls_total` | Counter | Total tool calls | tool_name, status |
| `pythonmcp_tool_call_duration_seconds` | Histogram | Tool call latency | tool_name |
| `pythonmcp_tool_call_errors_total` | Counter | Tool errors | tool_name, error_type |

### Data Sources
| Metric | Type | Description | Labels |
|--------|------|-------------|--------|
| `pythonmcp_messages_retrieved_total` | Counter | Messages retrieved | source |
| `pythonmcp_messages_retrieved_batch_size` | Histogram | Batch sizes | source |
| `pythonmcp_bridge_requests_total` | Counter | Bridge requests | operation, status |
| `pythonmcp_bridge_request_duration_seconds` | Histogram | Bridge latency | operation |
| `pythonmcp_telegram_api_requests_total` | Counter | API requests | method, status |
| `pythonmcp_telegram_api_request_duration_seconds` | Histogram | API latency | method |

### Cache Performance
| Metric | Type | Description | Labels |
|--------|------|-------------|--------|
| `pythonmcp_cache_hits_total` | Counter | Cache hits | cache_type |
| `pythonmcp_cache_misses_total` | Counter | Cache misses | cache_type |
| `pythonmcp_cache_size` | Gauge | Cache size | cache_type |

### AI/ML Operations
| Metric | Type | Description | Labels |
|--------|------|-------------|--------|
| `pythonmcp_aiml_requests_total` | Counter | AI/ML requests | operation, status |
| `pythonmcp_aiml_request_duration_seconds` | Histogram | AI/ML latency | operation |
| `pythonmcp_aiml_embeddings_generated_total` | Counter | Embeddings created | - |

## Useful PromQL Queries

### Performance Monitoring

**Request Rate (per second):**
```promql
rate(pythonmcp_tool_calls_total[5m])
```

**Average Latency:**
```promql
rate(pythonmcp_tool_call_duration_seconds_sum[5m])
/
rate(pythonmcp_tool_call_duration_seconds_count[5m])
```

**P95 Latency:**
```promql
histogram_quantile(0.95,
  rate(pythonmcp_tool_call_duration_seconds_bucket[5m])
)
```

**P99 Latency:**
```promql
histogram_quantile(0.99,
  rate(pythonmcp_tool_call_duration_seconds_bucket[5m])
)
```

### Error Monitoring

**Error Rate:**
```promql
rate(pythonmcp_tool_call_errors_total[5m])
```

**Error Ratio:**
```promql
sum(rate(pythonmcp_tool_calls_total{status="error"}[5m]))
/
sum(rate(pythonmcp_tool_calls_total[5m]))
```

**Errors by Tool:**
```promql
sum by (tool_name) (rate(pythonmcp_tool_call_errors_total[5m]))
```

### Data Source Usage

**Bridge vs API Usage:**
```promql
sum by (source) (rate(pythonmcp_messages_retrieved_total[5m]))
```

**Bridge Success Rate:**
```promql
sum(rate(pythonmcp_bridge_requests_total{status="success"}[5m]))
/
sum(rate(pythonmcp_bridge_requests_total[5m]))
```

### Cache Efficiency

**Cache Hit Rate:**
```promql
sum(rate(pythonmcp_cache_hits_total[5m]))
/
(sum(rate(pythonmcp_cache_hits_total[5m])) + sum(rate(pythonmcp_cache_misses_total[5m])))
```

**Cache Hit Rate by Type:**
```promql
sum by (cache_type) (rate(pythonmcp_cache_hits_total[5m]))
/
(
  sum by (cache_type) (rate(pythonmcp_cache_hits_total[5m]))
  +
  sum by (cache_type) (rate(pythonmcp_cache_misses_total[5m]))
)
```

## Alerting Rules

### Critical Alerts

**High Error Rate:**
```yaml
- alert: HighErrorRate
  expr: |
    sum(rate(pythonmcp_tool_call_errors_total[5m]))
    /
    sum(rate(pythonmcp_tool_calls_total[5m]))
    > 0.05
  for: 5m
  annotations:
    summary: "Error rate above 5%"
```

**Slow Response Time:**
```yaml
- alert: SlowResponseTime
  expr: |
    histogram_quantile(0.95,
      rate(pythonmcp_tool_call_duration_seconds_bucket[5m])
    ) > 5.0
  for: 5m
  annotations:
    summary: "P95 latency above 5 seconds"
```

### Warning Alerts

**Low Cache Hit Rate:**
```yaml
- alert: LowCacheHitRate
  expr: |
    sum(rate(pythonmcp_cache_hits_total[5m]))
    /
    (
      sum(rate(pythonmcp_cache_hits_total[5m]))
      +
      sum(rate(pythonmcp_cache_misses_total[5m]))
    ) < 0.5
  for: 10m
  annotations:
    summary: "Cache hit rate below 50%"
```

**Bridge Connection Issues:**
```yaml
- alert: BridgeErrors
  expr: |
    sum(rate(pythonmcp_bridge_requests_total{status="error"}[5m]))
    /
    sum(rate(pythonmcp_bridge_requests_total[5m]))
    > 0.1
  for: 5m
  annotations:
    summary: "Bridge error rate above 10%"
```

## Grafana Dashboard JSON

See `docs/monitoring.md` for complete Grafana dashboard configuration.

**Quick Panels:**

1. **Request Rate** - Graph of `rate(pythonmcp_tool_calls_total[5m])`
2. **Error Rate** - Graph of error ratio
3. **Latency** - Graph of P50, P95, P99 latencies
4. **Data Sources** - Pie chart of bridge vs API usage
5. **Cache Hit Rate** - Gauge showing cache efficiency
6. **Active Operations** - Table of current operation rates

## Integration Examples

### Prometheus Configuration

```yaml
# prometheus.yml
global:
  scrape_interval: 15s

scrape_configs:
  - job_name: 'pythonmcp'
    static_configs:
      - targets: ['localhost:9090']
```

### Docker Compose

```yaml
version: '3.8'
services:
  pythonmcp:
    build: .
    ports:
      - "9090:9090"  # Metrics
      - "8080:8080"  # Health
    environment:
      - ENABLE_PROMETHEUS=true

  prometheus:
    image: prom/prometheus
    ports:
      - "9091:9090"
    volumes:
      - ./prometheus.yml:/etc/prometheus/prometheus.yml
    depends_on:
      - pythonmcp
```

## Troubleshooting

**No metrics appear:**
- Check config: `grep enable_prometheus config.toml`
- Check logs: `grep metrics server.log`
- Check port: `curl http://localhost:9090/metrics`

**Metrics reset to zero:**
- Expected behavior on server restart
- Use `rate()` or `increase()` for meaningful graphs

**High memory usage:**
- Check histogram bucket configuration
- Reduce label cardinality if needed
- Adjust scrape interval in Prometheus

## API Reference

See `/Users/pasha/xCode/tlgrm/pythonMCP/src/monitoring/metrics.py` for:
- `PrometheusMetrics` class - Main metrics collector
- `instrument_tool()` decorator - Auto-instrumentation
- `MetricsHandler` - HTTP server for /metrics endpoint

Full documentation: `docs/monitoring.md`
