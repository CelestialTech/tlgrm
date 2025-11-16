#!/usr/bin/env python3
"""
Telegram MCP Server - Entry Point

Starts the MCP server with stdio transport for AI model integration.
"""

import argparse
import asyncio
import sys
from pathlib import Path

import structlog
from rich.console import Console
from rich.logging import RichHandler

from .config import load_config
from .mcp_server import TelegramMCPServer

# Configure logging
structlog.configure(
    processors=[
        structlog.stdlib.add_log_level,
        structlog.stdlib.add_logger_name,
        structlog.processors.TimeStamper(fmt="iso"),
        structlog.processors.StackInfoRenderer(),
        structlog.processors.format_exc_info,
        structlog.dev.ConsoleRenderer(),
    ],
    wrapper_class=structlog.stdlib.BoundLogger,
    context_class=dict,
    logger_factory=structlog.stdlib.LoggerFactory(),
    cache_logger_on_first_use=True,
)

logger = structlog.get_logger()
console = Console()


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Telegram MCP Server - Bridge AI models with Telegram"
    )

    parser.add_argument(
        "--config",
        "-c",
        type=str,
        default=None,
        help="Path to configuration file (default: config.toml)",
    )

    parser.add_argument(
        "--env",
        "-e",
        type=str,
        choices=["default", "dev", "prod"],
        default="default",
        help="Environment (default, dev, prod)",
    )

    parser.add_argument(
        "--log-level",
        "-l",
        type=str,
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        default=None,
        help="Log level override",
    )

    return parser.parse_args()


async def main() -> int:
    """Main entry point."""
    args = parse_args()

    try:
        # Load configuration
        config = load_config(
            config_path=args.config,
            environment=args.env,
        )

        # Override log level if specified
        if args.log_level:
            config.mcp.log_level = args.log_level

        # Display startup banner
        console.print(
            f"[bold cyan]Telegram MCP Server[/bold cyan] v{config.mcp.server_version}"
        )
        console.print(f"Environment: [yellow]{args.env}[/yellow]")
        console.print(f"Log Level: [yellow]{config.mcp.log_level}[/yellow]")
        console.print()

        # Create and run MCP server
        server = TelegramMCPServer(config)

        logger.info(
            "telegram_mcp.starting",
            version=config.mcp.server_version,
            environment=args.env,
        )

        # Run server
        await server.run()

        return 0

    except KeyboardInterrupt:
        logger.info("telegram_mcp.interrupted")
        return 0

    except Exception as e:
        logger.error(
            "telegram_mcp.fatal_error",
            error=str(e),
            exc_info=True,
        )
        console.print(f"[bold red]Fatal error:[/bold red] {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
