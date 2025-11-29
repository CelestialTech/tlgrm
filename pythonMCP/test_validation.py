#!/usr/bin/env python3
"""
Test script to demonstrate configuration validation functionality.

This script shows how the validation works with various scenarios.
"""

import os
import sys
from pathlib import Path

# Add src to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from src.core.config import Config, TelegramConfig, CacheConfig, MonitoringConfig
from src.core.validation import validate_config


def test_scenario(name: str, config: Config):
    """Test a validation scenario and print results."""
    print(f"\n{'='*60}")
    print(f"Scenario: {name}")
    print('='*60)

    errors = validate_config(config)

    if errors:
        print(f"❌ Validation FAILED ({len(errors)} errors):\n")
        for i, error in enumerate(errors, 1):
            print(f"{i}. {error}\n")
    else:
        print("✓ Validation PASSED")


def main():
    """Run validation test scenarios."""
    print("Configuration Validation Test Suite")
    print("="*60)

    # Scenario 1: Missing environment variables
    print("\nTest 1: Missing environment variables")
    os.environ.pop('TELEGRAM_API_ID', None)
    os.environ.pop('TELEGRAM_API_HASH', None)
    config1 = Config()
    test_scenario("Missing TELEGRAM_API_ID and TELEGRAM_API_HASH", config1)

    # Scenario 2: Invalid API ID (non-numeric)
    print("\n" + "="*60)
    print("\nTest 2: Invalid API ID (non-numeric)")
    os.environ['TELEGRAM_API_ID'] = 'not_a_number'
    os.environ['TELEGRAM_API_HASH'] = 'valid_hash_123'
    config2 = Config()
    test_scenario("Non-numeric API ID", config2)

    # Scenario 3: Empty API HASH
    print("\n" + "="*60)
    print("\nTest 3: Empty API HASH")
    os.environ['TELEGRAM_API_ID'] = '12345678'
    os.environ['TELEGRAM_API_HASH'] = '   '
    config3 = Config()
    test_scenario("Empty API HASH", config3)

    # Scenario 4: Invalid ports
    print("\n" + "="*60)
    print("\nTest 4: Invalid ports")
    os.environ['TELEGRAM_API_ID'] = '12345678'
    os.environ['TELEGRAM_API_HASH'] = 'abc123def456'
    config4 = Config(
        monitoring=MonitoringConfig(
            enable_health_check=True,
            health_check_port=100,  # Too low
            enable_metrics=True,
            metrics_port=70000,  # Too high
        )
    )
    test_scenario("Invalid port numbers", config4)

    # Scenario 5: Port conflicts
    print("\n" + "="*60)
    print("\nTest 5: Port conflicts")
    config5 = Config(
        monitoring=MonitoringConfig(
            enable_health_check=True,
            health_check_port=8080,
            enable_metrics=True,
            metrics_port=8080,  # Same as health_check_port
        )
    )
    test_scenario("Duplicate ports", config5)

    # Scenario 6: Invalid cache path
    print("\n" + "="*60)
    print("\nTest 6: Invalid cache path")
    config6 = Config(
        cache=CacheConfig(
            use_sqlite=True,
            db_path="/nonexistent/directory/cache.db"
        )
    )
    test_scenario("Non-existent cache directory", config6)

    # Scenario 7: Valid configuration
    print("\n" + "="*60)
    print("\nTest 7: Valid configuration")
    os.environ['TELEGRAM_API_ID'] = '12345678'
    os.environ['TELEGRAM_API_HASH'] = 'abc123def456789'
    config7 = Config(
        cache=CacheConfig(
            use_sqlite=True,
            db_path="./test_cache.db"
        ),
        monitoring=MonitoringConfig(
            enable_health_check=True,
            health_check_port=8080,
            enable_metrics=True,
            metrics_port=9090,
        )
    )
    test_scenario("Valid configuration", config7)

    print("\n" + "="*60)
    print("\nTest suite completed!")
    print("="*60)


if __name__ == "__main__":
    main()
