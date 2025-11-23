"""
Configuration management for Telegram MCP Server.

Loads settings from:
1. TOML configuration files (config.toml, config.dev.toml, config.prod.toml)
2. Environment variables (.env)
3. Command-line arguments (optional)
"""

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

import structlog
import toml
from dotenv import load_dotenv

logger = structlog.get_logger()

# Load environment variables
load_dotenv()


@dataclass
class TelegramConfig:
    """Telegram client configuration (Pyrogram/MTProto)."""

    # API credentials from https://my.telegram.org
    api_id_env_var: str = "TELEGRAM_API_ID"
    api_hash_env_var: str = "TELEGRAM_API_HASH"

    # Optional bot token (if using bot mode instead of user mode)
    bot_token_env_var: str = "TELEGRAM_BOT_TOKEN"

    # Session name for Pyrogram
    session_name: str = "telegram_mcp"

    # Whitelists
    allowed_chats: List[str] = field(default_factory=list)
    allowed_users: List[str] = field(default_factory=list)

    @property
    def api_id(self) -> int:
        """Get API ID from environment."""
        api_id = os.getenv(self.api_id_env_var)
        if not api_id:
            raise ValueError(
                f"Telegram API ID not found in environment variable: {self.api_id_env_var}"
            )
        return int(api_id)

    @property
    def api_hash(self) -> str:
        """Get API Hash from environment."""
        api_hash = os.getenv(self.api_hash_env_var)
        if not api_hash:
            raise ValueError(
                f"Telegram API Hash not found in environment variable: {self.api_hash_env_var}"
            )
        return api_hash

    @property
    def bot_token(self) -> Optional[str]:
        """Get bot token from environment (optional for bot mode)."""
        return os.getenv(self.bot_token_env_var)


@dataclass
class MCPConfig:
    """MCP server configuration."""

    server_name: str = "Telegram MCP Server"
    server_version: str = "0.1.0"
    transport: str = "stdio"
    port: int = 8000
    log_level: str = "INFO"


@dataclass
class CacheConfig:
    """Message caching configuration."""

    max_messages: int = 50
    ttl_seconds: int = 300
    use_sqlite: bool = False
    db_path: str = "telegram_cache.db"


@dataclass
class LimitsConfig:
    """API limits configuration."""

    default_message_limit: int = 20
    max_message_limit: int = 100


@dataclass
class FeaturesConfig:
    """Feature flags."""

    enable_ai_features: bool = True
    enable_translation: bool = True
    enable_sentiment: bool = True
    enable_webhooks: bool = False
    enable_voice_info: bool = True


@dataclass
class MonitoringConfig:
    """Monitoring configuration."""

    enable_metrics: bool = True
    metrics_port: int = 9090
    enable_prometheus: bool = True
    enable_health_check: bool = True


@dataclass
class RetryConfig:
    """Retry configuration for API calls."""

    max_retries: int = 3
    initial_backoff: float = 1.0
    max_backoff: float = 30.0
    backoff_multiplier: float = 2.0


@dataclass
class Config:
    """Main configuration container."""

    telegram: TelegramConfig = field(default_factory=TelegramConfig)
    mcp: MCPConfig = field(default_factory=MCPConfig)
    cache: CacheConfig = field(default_factory=CacheConfig)
    limits: LimitsConfig = field(default_factory=LimitsConfig)
    features: FeaturesConfig = field(default_factory=FeaturesConfig)
    monitoring: MonitoringConfig = field(default_factory=MonitoringConfig)
    retry: RetryConfig = field(default_factory=RetryConfig)


def load_config(
    config_path: Optional[str] = None,
    environment: str = "default",
) -> Config:
    """
    Load configuration from TOML file and environment variables.

    Args:
        config_path: Path to config file (default: config.toml)
        environment: Environment name (default, dev, prod)

    Returns:
        Config instance
    """
    # Determine config file path
    if config_path is None:
        if environment == "dev":
            config_path = "config.dev.toml"
        elif environment == "prod":
            config_path = "config.prod.toml"
        else:
            config_path = "config.toml"

    config_file = Path(config_path)

    if not config_file.exists():
        logger.warning(
            "config.file_not_found",
            path=str(config_file),
            using_defaults=True,
        )
        return Config()

    # Load TOML
    try:
        data = toml.load(config_file)
    except Exception as e:
        logger.error("config.load_failed", error=str(e), path=str(config_file))
        raise

    # Parse configuration sections
    telegram_config = TelegramConfig(**data.get("telegram", {}))
    mcp_config = MCPConfig(**data.get("mcp", {}))
    cache_config = CacheConfig(**data.get("cache", {}))
    limits_config = LimitsConfig(**data.get("limits", {}))
    features_config = FeaturesConfig(**data.get("features", {}))
    monitoring_config = MonitoringConfig(**data.get("monitoring", {}))
    retry_config = RetryConfig(**data.get("retry", {}))

    # Override from environment variables
    if allowed_chats := os.getenv("ALLOWED_CHATS"):
        telegram_config.allowed_chats = [
            chat.strip() for chat in allowed_chats.split(",") if chat.strip()
        ]

    if allowed_users := os.getenv("ALLOWED_USERS"):
        telegram_config.allowed_users = [
            user.strip() for user in allowed_users.split(",") if user.strip()
        ]

    if log_level := os.getenv("LOG_LEVEL"):
        mcp_config.log_level = log_level

    config = Config(
        telegram=telegram_config,
        mcp=mcp_config,
        cache=cache_config,
        limits=limits_config,
        features=features_config,
        monitoring=monitoring_config,
        retry=retry_config,
    )

    logger.info(
        "config.loaded",
        environment=environment,
        server_name=config.mcp.server_name,
        version=config.mcp.server_version,
    )

    return config
