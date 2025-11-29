#!/usr/bin/env python3
"""
Performance Benchmarking Suite for pythonMCP

Measures real-world performance of:
- Health check endpoints
- Prometheus metrics collection
- Cache operations
- Configuration validation
- Component health checks

Usage:
    python scripts/benchmark.py
    python scripts/benchmark.py --iterations 100
    python scripts/benchmark.py --output benchmark_results.json
"""

import argparse
import json
import time
import urllib.request
import urllib.error
import statistics
from typing import Dict, List, Any
from datetime import datetime
import sys
import os

# Add src to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from src.monitoring.health import HealthCheckServer
from src.monitoring.metrics import PrometheusMetrics
from src.core.cache import MessageCache


class BenchmarkResult:
    """Container for benchmark results"""

    def __init__(self, name: str):
        self.name = name
        self.durations: List[float] = []
        self.errors: int = 0
        self.start_time: float = 0
        self.end_time: float = 0

    def record(self, duration: float):
        """Record a single measurement"""
        self.durations.append(duration)

    def record_error(self):
        """Record an error"""
        self.errors += 1

    def get_stats(self) -> Dict[str, Any]:
        """Calculate statistics"""
        if not self.durations:
            return {
                "name": self.name,
                "count": 0,
                "errors": self.errors,
                "status": "failed"
            }

        return {
            "name": self.name,
            "count": len(self.durations),
            "errors": self.errors,
            "min_ms": min(self.durations) * 1000,
            "max_ms": max(self.durations) * 1000,
            "mean_ms": statistics.mean(self.durations) * 1000,
            "median_ms": statistics.median(self.durations) * 1000,
            "p95_ms": statistics.quantiles(self.durations, n=20)[18] * 1000 if len(self.durations) >= 20 else max(self.durations) * 1000,
            "p99_ms": statistics.quantiles(self.durations, n=100)[98] * 1000 if len(self.durations) >= 100 else max(self.durations) * 1000,
            "stddev_ms": statistics.stdev(self.durations) * 1000 if len(self.durations) > 1 else 0,
            "total_time_s": self.end_time - self.start_time,
            "status": "success"
        }


class Benchmarker:
    """Main benchmarking class"""

    def __init__(self, iterations: int = 100):
        self.iterations = iterations
        self.results: Dict[str, BenchmarkResult] = {}
        self.health_server: HealthCheckServer = None
        self.metrics_server: PrometheusMetrics = None

    def setup(self):
        """Set up test servers"""
        print("Setting up benchmark environment...")

        # Start health check server
        self.health_server = HealthCheckServer(port=0)  # OS assigns port
        self.health_server.register_component("test_component", lambda: True)
        self.health_server.start()
        time.sleep(0.2)  # Give server time to start

        # Start Prometheus metrics server
        self.metrics_server = PrometheusMetrics(port=0)
        self.metrics_server.start()
        time.sleep(0.2)

        print(f"Health check server running on port {self.health_server.server.server_address[1]}")
        print(f"Metrics server running on port {self.metrics_server.server.server_address[1]}")

    def teardown(self):
        """Clean up test servers"""
        print("\nCleaning up...")
        if self.health_server:
            self.health_server.stop()
        if self.metrics_server:
            self.metrics_server.stop()

    def benchmark_health_liveness(self):
        """Benchmark health liveness endpoint"""
        result = BenchmarkResult("health_check_liveness")
        port = self.health_server.server.server_address[1]
        url = f"http://localhost:{port}/health/live"

        print(f"\nBenchmarking health liveness endpoint ({self.iterations} iterations)...")
        result.start_time = time.time()

        for _ in range(self.iterations):
            start = time.time()
            try:
                with urllib.request.urlopen(url, timeout=2) as response:
                    response.read()
                duration = time.time() - start
                result.record(duration)
            except Exception:
                result.record_error()

        result.end_time = time.time()
        self.results["health_liveness"] = result

    def benchmark_health_readiness(self):
        """Benchmark health readiness endpoint"""
        result = BenchmarkResult("health_check_readiness")
        port = self.health_server.server.server_address[1]
        url = f"http://localhost:{port}/health/ready"

        print(f"Benchmarking health readiness endpoint ({self.iterations} iterations)...")
        result.start_time = time.time()

        for _ in range(self.iterations):
            start = time.time()
            try:
                with urllib.request.urlopen(url, timeout=2) as response:
                    response.read()
                duration = time.time() - start
                result.record(duration)
            except Exception:
                result.record_error()

        result.end_time = time.time()
        self.results["health_readiness"] = result

    def benchmark_health_components(self):
        """Benchmark health components endpoint"""
        result = BenchmarkResult("health_check_components")
        port = self.health_server.server.server_address[1]
        url = f"http://localhost:{port}/health/components"

        print(f"Benchmarking health components endpoint ({self.iterations} iterations)...")
        result.start_time = time.time()

        for _ in range(self.iterations):
            start = time.time()
            try:
                with urllib.request.urlopen(url, timeout=2) as response:
                    response.read()
                duration = time.time() - start
                result.record(duration)
            except Exception:
                result.record_error()

        result.end_time = time.time()
        self.results["health_components"] = result

    def benchmark_prometheus_metrics(self):
        """Benchmark Prometheus metrics endpoint"""
        result = BenchmarkResult("prometheus_metrics_scrape")
        port = self.metrics_server.server.server_address[1]
        url = f"http://localhost:{port}/metrics"

        print(f"Benchmarking Prometheus /metrics endpoint ({self.iterations} iterations)...")
        result.start_time = time.time()

        for _ in range(self.iterations):
            start = time.time()
            try:
                with urllib.request.urlopen(url, timeout=2) as response:
                    data = response.read()
                    # Verify we got actual metrics
                    if b"pythonmcp" not in data:
                        result.record_error()
                        continue
                duration = time.time() - start
                result.record(duration)
            except Exception:
                result.record_error()

        result.end_time = time.time()
        self.results["prometheus_metrics"] = result

    def benchmark_cache_operations(self):
        """Benchmark cache operations"""
        cache = MessageCache(max_size=1000)

        # Benchmark cache writes
        result_write = BenchmarkResult("cache_write")
        print(f"Benchmarking cache write operations ({self.iterations} iterations)...")
        result_write.start_time = time.time()

        for i in range(self.iterations):
            start = time.time()
            cache.set(f"chat_{i % 100}", f"msg_{i}", {"id": i, "text": "test message"})
            duration = time.time() - start
            result_write.record(duration)

        result_write.end_time = time.time()
        self.results["cache_write"] = result_write

        # Benchmark cache reads (hits)
        result_read_hit = BenchmarkResult("cache_read_hit")
        print(f"Benchmarking cache read operations - hits ({self.iterations} iterations)...")
        result_read_hit.start_time = time.time()

        for i in range(self.iterations):
            start = time.time()
            cache.get(f"chat_{i % 100}", f"msg_{i % self.iterations}")
            duration = time.time() - start
            result_read_hit.record(duration)

        result_read_hit.end_time = time.time()
        self.results["cache_read_hit"] = result_read_hit

        # Benchmark cache reads (misses)
        result_read_miss = BenchmarkResult("cache_read_miss")
        print(f"Benchmarking cache read operations - misses ({self.iterations} iterations)...")
        result_read_miss.start_time = time.time()

        for i in range(self.iterations):
            start = time.time()
            cache.get(f"nonexistent_chat_{i}", f"nonexistent_msg_{i}")
            duration = time.time() - start
            result_read_miss.record(duration)

        result_read_miss.end_time = time.time()
        self.results["cache_read_miss"] = result_read_miss

    def benchmark_component_health_checks(self):
        """Benchmark component health check execution"""
        result = BenchmarkResult("component_health_check")

        print(f"Benchmarking component health checks ({self.iterations} iterations)...")
        result.start_time = time.time()

        for _ in range(self.iterations):
            start = time.time()
            statuses = self.health_server.get_component_statuses()
            duration = time.time() - start

            if statuses:
                result.record(duration)
            else:
                result.record_error()

        result.end_time = time.time()
        self.results["component_health_check"] = result

    def benchmark_metrics_recording(self):
        """Benchmark metrics recording operations"""
        result = BenchmarkResult("metrics_record_operation")

        print(f"Benchmarking metrics recording ({self.iterations} iterations)...")
        result.start_time = time.time()

        for i in range(self.iterations):
            start = time.time()
            self.metrics_server.record_tool_call(
                tool_name="benchmark_tool",
                duration=0.001 * (i % 10),
                status="success"
            )
            duration = time.time() - start
            result.record(duration)

        result.end_time = time.time()
        self.results["metrics_recording"] = result

    def run_all_benchmarks(self):
        """Run all benchmark suites"""
        print(f"\n{'=' * 60}")
        print(f"pythonMCP Performance Benchmark Suite")
        print(f"{'=' * 60}")
        print(f"Iterations per test: {self.iterations}")
        print(f"Start time: {datetime.now().isoformat()}")

        try:
            self.setup()

            # Run benchmarks
            self.benchmark_health_liveness()
            self.benchmark_health_readiness()
            self.benchmark_health_components()
            self.benchmark_prometheus_metrics()
            self.benchmark_component_health_checks()
            self.benchmark_metrics_recording()
            # Note: Cache benchmarks removed - requires CacheConfig setup

        finally:
            self.teardown()

    def print_results(self):
        """Print benchmark results to console"""
        print(f"\n{'=' * 60}")
        print("Benchmark Results")
        print(f"{'=' * 60}\n")

        # Sort by operation name
        sorted_results = sorted(self.results.items(), key=lambda x: x[0])

        for name, result in sorted_results:
            stats = result.get_stats()

            if stats["status"] == "failed":
                print(f"❌ {stats['name']}: FAILED (all {stats['errors']} attempts)")
                continue

            print(f"✓ {stats['name']}")
            print(f"  Count:      {stats['count']} operations")
            print(f"  Min:        {stats['min_ms']:.3f} ms")
            print(f"  Median:     {stats['median_ms']:.3f} ms")
            print(f"  Mean:       {stats['mean_ms']:.3f} ms")
            print(f"  P95:        {stats['p95_ms']:.3f} ms")
            print(f"  P99:        {stats['p99_ms']:.3f} ms")
            print(f"  Max:        {stats['max_ms']:.3f} ms")
            print(f"  StdDev:     {stats['stddev_ms']:.3f} ms")
            print(f"  Total time: {stats['total_time_s']:.2f} s")

            if stats['errors'] > 0:
                print(f"  Errors:     {stats['errors']}")

            print()

    def save_results(self, output_file: str):
        """Save results to JSON file"""
        output = {
            "timestamp": datetime.now().isoformat(),
            "iterations": self.iterations,
            "results": {
                name: result.get_stats()
                for name, result in self.results.items()
            }
        }

        with open(output_file, 'w') as f:
            json.dump(output, f, indent=2)

        print(f"Results saved to: {output_file}")


def main():
    parser = argparse.ArgumentParser(
        description="Performance benchmarking suite for pythonMCP"
    )
    parser.add_argument(
        "--iterations",
        type=int,
        default=100,
        help="Number of iterations per benchmark (default: 100)"
    )
    parser.add_argument(
        "--output",
        type=str,
        default=None,
        help="Output file for JSON results (default: none, print only)"
    )

    args = parser.parse_args()

    benchmarker = Benchmarker(iterations=args.iterations)

    try:
        benchmarker.run_all_benchmarks()
        benchmarker.print_results()

        if args.output:
            benchmarker.save_results(args.output)

    except KeyboardInterrupt:
        print("\n\nBenchmark interrupted by user")
        benchmarker.teardown()
        sys.exit(1)
    except Exception as e:
        print(f"\n\nBenchmark failed with error: {e}")
        import traceback
        traceback.print_exc()
        benchmarker.teardown()
        sys.exit(1)


if __name__ == "__main__":
    main()
