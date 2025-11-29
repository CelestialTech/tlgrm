"""
Configuration validation for Telegram MCP Server.

Validates configuration settings including environment variables,
file paths, and port numbers to ensure proper server operation.
"""

import os
from pathlib import Path
from typing import List

from .config import Config


class ConfigurationError(Exception):
    """Raised when configuration validation fails."""

    pass


def validate_config(config: Config) -> List[str]:
    """
    Validate configuration and return list of error messages.

    Args:
        config: Configuration instance to validate

    Returns:
        List of error messages (empty if validation passes)
    """
    errors: List[str] = []

    # Validate required environment variables
    errors.extend(_validate_environment_variables(config))

    # Validate paths
    errors.extend(_validate_paths(config))

    # Validate ports
    errors.extend(_validate_ports(config))

    return errors


def _validate_environment_variables(config: Config) -> List[str]:
    """
    Validate required environment variables for Telegram configuration.

    Args:
        config: Configuration instance to validate

    Returns:
        List of error messages
    """
    errors: List[str] = []

    # Check TELEGRAM_API_ID
    api_id_var = config.telegram.api_id_env_var
    api_id_value = os.getenv(api_id_var)

    if not api_id_value:
        errors.append(
            f"Missing required environment variable: {api_id_var}\n"
            f"  Fix: Set {api_id_var} in your .env file or environment.\n"
            f"  Obtain from: https://my.telegram.org/apps"
        )
    else:
        # Validate it's numeric
        try:
            int(api_id_value)
        except ValueError:
            errors.append(
                f"Invalid {api_id_var}: must be numeric (got: '{api_id_value}')\n"
                f"  Fix: Set {api_id_var} to a valid numeric API ID.\n"
                f"  Obtain from: https://my.telegram.org/apps"
            )

    # Check TELEGRAM_API_HASH
    api_hash_var = config.telegram.api_hash_env_var
    api_hash_value = os.getenv(api_hash_var)

    if not api_hash_value:
        errors.append(
            f"Missing required environment variable: {api_hash_var}\n"
            f"  Fix: Set {api_hash_var} in your .env file or environment.\n"
            f"  Obtain from: https://my.telegram.org/apps"
        )
    elif not api_hash_value.strip():
        errors.append(
            f"Invalid {api_hash_var}: cannot be empty or whitespace only\n"
            f"  Fix: Set {api_hash_var} to a valid API hash.\n"
            f"  Obtain from: https://my.telegram.org/apps"
        )

    # Check TELEGRAM_BOT_TOKEN if bot mode is indicated
    # (bot mode is indicated by the presence of TELEGRAM_BOT_TOKEN)
    bot_token_var = config.telegram.bot_token_env_var
    bot_token_value = os.getenv(bot_token_var)

    if bot_token_value is not None:
        # If set, validate it's not empty
        if not bot_token_value.strip():
            errors.append(
                f"Invalid {bot_token_var}: cannot be empty or whitespace only\n"
                f"  Fix: Either set {bot_token_var} to a valid bot token or remove it.\n"
                f"  Obtain from: @BotFather on Telegram"
            )

    return errors


def _validate_paths(config: Config) -> List[str]:
    """
    Validate file paths in configuration.

    Args:
        config: Configuration instance to validate

    Returns:
        List of error messages
    """
    errors: List[str] = []

    # Check cache database path if SQLite is enabled
    if config.cache.use_sqlite:
        db_path = Path(config.cache.db_path)

        # Check if parent directory exists
        parent_dir = db_path.parent
        if not parent_dir.exists():
            errors.append(
                f"Cache database parent directory does not exist: {parent_dir}\n"
                f"  Fix: Create the directory: mkdir -p {parent_dir}"
            )
        else:
            # Check if path is writable (test by checking parent directory permissions)
            if not os.access(parent_dir, os.W_OK):
                errors.append(
                    f"Cache database path is not writable: {db_path}\n"
                    f"  Fix: Ensure write permissions for directory: {parent_dir}"
                )

            # If the database file exists, check if it's writable
            if db_path.exists() and not os.access(db_path, os.W_OK):
                errors.append(
                    f"Existing cache database file is not writable: {db_path}\n"
                    f"  Fix: Ensure write permissions: chmod u+w {db_path}"
                )

    return errors


def _validate_ports(config: Config) -> List[str]:
    """
    Validate port numbers in configuration.

    Args:
        config: Configuration instance to validate

    Returns:
        List of error messages
    """
    errors: List[str] = []

    # Valid port range (excluding well-known ports 0-1023)
    MIN_PORT = 1024
    MAX_PORT = 65535

    # Validate health check port
    if config.monitoring.enable_health_check:
        health_port = config.monitoring.health_check_port
        if not (MIN_PORT <= health_port <= MAX_PORT):
            errors.append(
                f"Invalid health_check_port: {health_port}\n"
                f"  Fix: Set health_check_port to a value between {MIN_PORT} and {MAX_PORT}"
            )

    # Validate metrics port
    if config.monitoring.enable_metrics:
        metrics_port = config.monitoring.metrics_port
        if not (MIN_PORT <= metrics_port <= MAX_PORT):
            errors.append(
                f"Invalid metrics_port: {metrics_port}\n"
                f"  Fix: Set metrics_port to a value between {MIN_PORT} and {MAX_PORT}"
            )

        # Check for port conflicts
        if (
            config.monitoring.enable_health_check
            and metrics_port == config.monitoring.health_check_port
        ):
            errors.append(
                f"Port conflict: metrics_port and health_check_port cannot be the same ({metrics_port})\n"
                f"  Fix: Set metrics_port and health_check_port to different values"
            )

    # Validate MCP server port (if not using stdio transport)
    if config.mcp.transport != "stdio":
        mcp_port = config.mcp.port
        if not (MIN_PORT <= mcp_port <= MAX_PORT):
            errors.append(
                f"Invalid MCP port: {mcp_port}\n"
                f"  Fix: Set mcp.port to a value between {MIN_PORT} and {MAX_PORT}"
            )

        # Check for port conflicts with monitoring ports
        if config.monitoring.enable_health_check and mcp_port == config.monitoring.health_check_port:
            errors.append(
                f"Port conflict: MCP port and health_check_port cannot be the same ({mcp_port})\n"
                f"  Fix: Set mcp.port and health_check_port to different values"
            )

        if config.monitoring.enable_metrics and mcp_port == config.monitoring.metrics_port:
            errors.append(
                f"Port conflict: MCP port and metrics_port cannot be the same ({mcp_port})\n"
                f"  Fix: Set mcp.port and metrics_port to different values"
            )

    return errors
