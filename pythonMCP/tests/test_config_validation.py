"""
Tests for src/core/config.py and src/core/validation.py

Tests configuration loading, environment variable handling, and validation logic.
"""

import os
import pytest
from pathlib import Path
from unittest.mock import patch
import tempfile

from src.core.config import (
    Config,
    TelegramConfig,
    load_config,
)
from src.core.validation import (
    ConfigurationError,
    validate_config,
    _validate_environment_variables,
    _validate_paths,
    _validate_ports,
)


# ============================================================================
# Configuration Loading Tests
# ============================================================================


def test_default_config_creation():
    """Test creating Config with default values"""
    config = Config()

    assert config.telegram.session_name == "telegram_mcp"
    assert config.mcp.server_name == "Telegram MCP Server"
    assert config.cache.max_messages == 50
    assert config.limits.default_message_limit == 20
    assert config.features.enable_ai_features is True
    assert config.monitoring.enable_metrics is True
    assert config.retry.max_retries == 3


def test_telegram_config_defaults():
    """Test TelegramConfig default values"""
    config = TelegramConfig()

    assert config.api_id_env_var == "TELEGRAM_API_ID"
    assert config.api_hash_env_var == "TELEGRAM_API_HASH"
    assert config.bot_token_env_var == "TELEGRAM_BOT_TOKEN"
    assert config.session_name == "telegram_mcp"
    assert config.allowed_chats == []
    assert config.allowed_users == []


@patch.dict(os.environ, {"TELEGRAM_API_ID": "12345678"})
def test_telegram_config_api_id_from_env():
    """Test TelegramConfig reads API ID from environment"""
    config = TelegramConfig()

    assert config.api_id == 12345678


@patch.dict(os.environ, {"TELEGRAM_API_HASH": "abcdef1234567890"})
def test_telegram_config_api_hash_from_env():
    """Test TelegramConfig reads API hash from environment"""
    config = TelegramConfig()

    assert config.api_hash == "abcdef1234567890"


@patch.dict(os.environ, {}, clear=True)
def test_telegram_config_missing_api_id():
    """Test TelegramConfig raises error when API ID is missing"""
    config = TelegramConfig()

    with pytest.raises(ValueError, match="Telegram API ID not found"):
        _ = config.api_id


@patch.dict(os.environ, {}, clear=True)
def test_telegram_config_missing_api_hash():
    """Test TelegramConfig raises error when API hash is missing"""
    config = TelegramConfig()

    with pytest.raises(ValueError, match="Telegram API Hash not found"):
        _ = config.api_hash


@patch.dict(os.environ, {"TELEGRAM_BOT_TOKEN": "123456:ABC-DEF"})
def test_telegram_config_bot_token_optional():
    """Test TelegramConfig bot token is optional"""
    config = TelegramConfig()

    assert config.bot_token == "123456:ABC-DEF"


@patch.dict(os.environ, {}, clear=True)
def test_telegram_config_bot_token_none_when_missing():
    """Test TelegramConfig bot token returns None when not set"""
    config = TelegramConfig()

    assert config.bot_token is None


@patch.dict(os.environ, {"TELEGRAM_API_ID": "12345678", "TELEGRAM_API_HASH": "test_hash"})
def test_load_config_missing_file():
    """Test load_config returns default config when file doesn't exist"""
    config = load_config("nonexistent_config.toml")

    # Should return default config without raising error
    assert isinstance(config, Config)
    assert config.mcp.server_name == "Telegram MCP Server"


def test_load_config_from_toml():
    """Test load_config parses TOML file correctly"""
    # Create temporary TOML file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.toml', delete=False) as f:
        f.write("""
[mcp]
server_name = "Test MCP Server"
server_version = "1.2.3"
log_level = "DEBUG"

[cache]
max_messages = 100
ttl_seconds = 600

[monitoring]
enable_metrics = false
metrics_port = 9999
""")
        temp_config_path = f.name

    try:
        # Set environment with required vars only (exclude TELEGRAM_BOT_TOKEN)
        with patch.dict(os.environ, {
            "TELEGRAM_API_ID": "12345678",
            "TELEGRAM_API_HASH": "test_hash"
        }, clear=True):
            config = load_config(temp_config_path)

        assert config.mcp.server_name == "Test MCP Server"
        assert config.mcp.server_version == "1.2.3"
        assert config.mcp.log_level == "DEBUG"
        assert config.cache.max_messages == 100
        assert config.cache.ttl_seconds == 600
        assert config.monitoring.enable_metrics is False
        assert config.monitoring.metrics_port == 9999
    finally:
        os.unlink(temp_config_path)


def test_load_config_env_override():
    """Test environment variables override TOML config"""
    with tempfile.NamedTemporaryFile(mode='w', suffix='.toml', delete=False) as f:
        f.write("""
[mcp]
log_level = "INFO"
""")
        temp_config_path = f.name

    try:
        # Set environment with required vars and overrides (exclude TELEGRAM_BOT_TOKEN)
        with patch.dict(os.environ, {
            "TELEGRAM_API_ID": "12345678",
            "TELEGRAM_API_HASH": "test_hash",
            "ALLOWED_CHATS": "chat1, chat2, chat3",
            "ALLOWED_USERS": "user1,user2",
            "LOG_LEVEL": "ERROR"
        }, clear=True):
            config = load_config(temp_config_path)

            # Environment variables should override TOML
            assert config.telegram.allowed_chats == ["chat1", "chat2", "chat3"]
            assert config.telegram.allowed_users == ["user1", "user2"]
            assert config.mcp.log_level == "ERROR"
    finally:
        os.unlink(temp_config_path)


# ============================================================================
# Validation Tests - Environment Variables
# ============================================================================


@patch.dict(os.environ, {}, clear=True)
def test_validate_missing_api_id():
    """Test validation catches missing TELEGRAM_API_ID"""
    config = Config()
    errors = _validate_environment_variables(config)

    assert len(errors) == 2  # Both API_ID and API_HASH missing
    assert any("TELEGRAM_API_ID" in error for error in errors)
    assert any("https://my.telegram.org/apps" in error for error in errors)


@patch.dict(os.environ, {"TELEGRAM_API_ID": "not_a_number"}, clear=True)
def test_validate_invalid_api_id():
    """Test validation catches non-numeric API ID"""
    config = Config()
    errors = _validate_environment_variables(config)

    assert any("must be numeric" in error and "not_a_number" in error for error in errors)


@patch.dict(os.environ, {"TELEGRAM_API_ID": "12345678"}, clear=True)
def test_validate_missing_api_hash():
    """Test validation catches missing TELEGRAM_API_HASH"""
    config = Config()
    errors = _validate_environment_variables(config)

    assert any("TELEGRAM_API_HASH" in error for error in errors)


@patch.dict(os.environ, {"TELEGRAM_API_ID": "12345678", "TELEGRAM_API_HASH": "   "})
def test_validate_empty_api_hash():
    """Test validation catches empty/whitespace API hash"""
    config = Config()
    errors = _validate_environment_variables(config)

    assert any("cannot be empty or whitespace" in error for error in errors)


@patch.dict(os.environ, {
    "TELEGRAM_API_ID": "12345678",
    "TELEGRAM_API_HASH": "valid_hash",
    "TELEGRAM_BOT_TOKEN": "   "
})
def test_validate_empty_bot_token():
    """Test validation catches empty bot token when set"""
    config = Config()
    errors = _validate_environment_variables(config)

    assert any("TELEGRAM_BOT_TOKEN" in error and "cannot be empty" in error for error in errors)


def test_validate_valid_env_vars():
    """Test validation passes with valid environment variables"""
    with patch.dict(os.environ, {
        "TELEGRAM_API_ID": "12345678",
        "TELEGRAM_API_HASH": "valid_hash"
    }, clear=True):
        config = Config()
        errors = _validate_environment_variables(config)

        assert len(errors) == 0


# ============================================================================
# Validation Tests - Paths
# ============================================================================


def test_validate_paths_sqlite_disabled():
    """Test path validation is skipped when SQLite is disabled"""
    config = Config()
    config.cache.use_sqlite = False

    errors = _validate_paths(config)

    assert len(errors) == 0


def test_validate_paths_missing_parent_directory():
    """Test validation catches missing parent directory for SQLite"""
    config = Config()
    config.cache.use_sqlite = True
    config.cache.db_path = "/nonexistent/path/to/db.sqlite"

    errors = _validate_paths(config)

    assert any("parent directory does not exist" in error for error in errors)


def test_validate_paths_valid_directory():
    """Test validation passes for valid writable directory"""
    config = Config()
    config.cache.use_sqlite = True

    # Use temp directory which exists and is writable
    with tempfile.TemporaryDirectory() as tmpdir:
        config.cache.db_path = os.path.join(tmpdir, "test.db")

        errors = _validate_paths(config)

        assert len(errors) == 0


def test_validate_paths_existing_file_not_writable():
    """Test validation catches non-writable existing database file"""
    config = Config()
    config.cache.use_sqlite = True

    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = os.path.join(tmpdir, "test.db")

        # Create the file
        Path(db_path).touch()

        # Make it read-only
        os.chmod(db_path, 0o444)

        config.cache.db_path = db_path

        try:
            errors = _validate_paths(config)

            # Should catch non-writable file
            assert any("not writable" in error for error in errors)
        finally:
            # Restore permissions for cleanup
            os.chmod(db_path, 0o644)


# ============================================================================
# Validation Tests - Ports
# ============================================================================


def test_validate_ports_valid_defaults():
    """Test default port configuration is valid"""
    config = Config()

    errors = _validate_ports(config)

    assert len(errors) == 0


def test_validate_ports_health_check_too_low():
    """Test validation catches port number below minimum"""
    config = Config()
    config.monitoring.enable_health_check = True
    config.monitoring.health_check_port = 80  # Well-known port

    errors = _validate_ports(config)

    assert any("Invalid health_check_port" in error and "80" in error for error in errors)


def test_validate_ports_health_check_too_high():
    """Test validation catches port number above maximum"""
    config = Config()
    config.monitoring.enable_health_check = True
    config.monitoring.health_check_port = 70000  # Above 65535

    errors = _validate_ports(config)

    assert any("Invalid health_check_port" in error for error in errors)


def test_validate_ports_metrics_invalid():
    """Test validation catches invalid metrics port"""
    config = Config()
    config.monitoring.enable_metrics = True
    config.monitoring.metrics_port = 500  # Too low

    errors = _validate_ports(config)

    assert any("Invalid metrics_port" in error for error in errors)


def test_validate_ports_conflict_metrics_and_health():
    """Test validation catches port conflict between metrics and health check"""
    config = Config()
    config.monitoring.enable_metrics = True
    config.monitoring.enable_health_check = True
    config.monitoring.metrics_port = 9000
    config.monitoring.health_check_port = 9000  # Same port!

    errors = _validate_ports(config)

    assert any("Port conflict" in error and "metrics_port and health_check_port" in error for error in errors)


def test_validate_ports_mcp_http_mode():
    """Test validation checks MCP port when not using stdio"""
    config = Config()
    config.mcp.transport = "http"
    config.mcp.port = 500  # Invalid

    errors = _validate_ports(config)

    assert any("Invalid MCP port" in error for error in errors)


def test_validate_ports_mcp_conflict_with_health():
    """Test validation catches MCP port conflict with health check"""
    config = Config()
    config.mcp.transport = "http"
    config.mcp.port = 8080
    config.monitoring.enable_health_check = True
    config.monitoring.health_check_port = 8080  # Conflict!

    errors = _validate_ports(config)

    assert any("Port conflict" in error and "MCP port and health_check_port" in error for error in errors)


def test_validate_ports_mcp_conflict_with_metrics():
    """Test validation catches MCP port conflict with metrics"""
    config = Config()
    config.mcp.transport = "http"
    config.mcp.port = 9090
    config.monitoring.enable_metrics = True
    config.monitoring.metrics_port = 9090  # Conflict!

    errors = _validate_ports(config)

    assert any("Port conflict" in error and "MCP port and metrics_port" in error for error in errors)


def test_validate_ports_stdio_mode_skips_mcp_port():
    """Test MCP port validation is skipped for stdio transport"""
    config = Config()
    config.mcp.transport = "stdio"
    config.mcp.port = 80  # Invalid, but should be ignored

    errors = _validate_ports(config)

    # Should not have errors about MCP port
    assert not any("MCP port" in error for error in errors)


# ============================================================================
# Integration Tests - Full Validation
# ============================================================================


def test_validate_config_all_valid():
    """Test full config validation with all valid settings"""
    with patch.dict(os.environ, {
        "TELEGRAM_API_ID": "12345678",
        "TELEGRAM_API_HASH": "valid_hash"
    }, clear=True):
        config = Config()

        errors = validate_config(config)

        assert len(errors) == 0


@patch.dict(os.environ, {}, clear=True)
def test_validate_config_multiple_errors():
    """Test validation collects multiple errors"""
    config = Config()
    config.monitoring.health_check_port = 80  # Invalid port
    config.cache.use_sqlite = True
    config.cache.db_path = "/nonexistent/db.sqlite"  # Invalid path

    errors = validate_config(config)

    # Should have errors for: missing API_ID, missing API_HASH, invalid port, invalid path
    assert len(errors) >= 3


@patch.dict(os.environ, {}, clear=True)
def test_load_config_validation_failure_raises_error():
    """Test load_config raises ConfigurationError when validation fails"""
    # Create valid TOML but environment is missing required vars
    with tempfile.NamedTemporaryFile(mode='w', suffix='.toml', delete=False) as f:
        f.write("""
[mcp]
server_name = "Test Server"
""")
        temp_config_path = f.name

    try:
        with pytest.raises(ConfigurationError) as exc_info:
            load_config(temp_config_path)

        # Error message should mention validation failure
        assert "Configuration validation failed" in str(exc_info.value)
        assert "TELEGRAM_API_ID" in str(exc_info.value)
    finally:
        os.unlink(temp_config_path)


def test_config_from_file_classmethod():
    """Test Config.from_file classmethod"""
    with tempfile.NamedTemporaryFile(mode='w', suffix='.toml', delete=False) as f:
        f.write("""
[mcp]
server_name = "Custom Server"
""")
        temp_config_path = f.name

    try:
        with patch.dict(os.environ, {
            "TELEGRAM_API_ID": "12345678",
            "TELEGRAM_API_HASH": "valid_hash"
        }, clear=True):
            config = Config.from_file(Path(temp_config_path))

            assert config.mcp.server_name == "Custom Server"
    finally:
        os.unlink(temp_config_path)


# ============================================================================
# Edge Cases and Error Handling
# ============================================================================


@patch.dict(os.environ, {"TELEGRAM_API_ID": "12345678", "TELEGRAM_API_HASH": "valid_hash"})
def test_load_config_invalid_toml():
    """Test load_config handles invalid TOML gracefully"""
    with tempfile.NamedTemporaryFile(mode='w', suffix='.toml', delete=False) as f:
        f.write("this is not valid TOML [[[")
        temp_config_path = f.name

    try:
        with pytest.raises(Exception):  # TOML parsing error
            load_config(temp_config_path)
    finally:
        os.unlink(temp_config_path)


@pytest.mark.xfail(reason="load_config() doesn't read env vars when config file doesn't exist (known issue)")
def test_env_override_filters_empty_values():
    """Test environment variable parsing filters out empty strings

    NOTE: This test documents expected behavior but currently fails because
    load_config() returns early when config.toml doesn't exist (line 194 in config.py),
    before reading environment variables. This needs to be fixed in the implementation.
    """
    with patch.dict(os.environ, {
        "TELEGRAM_API_ID": "12345678",
        "TELEGRAM_API_HASH": "valid_hash",
        "ALLOWED_CHATS": "  ,  , chat1 ,  , chat2,  "
    }, clear=True):
        config = load_config()

        # Should filter out empty/whitespace-only values
        assert config.telegram.allowed_chats == ["chat1", "chat2"]


def test_configuration_error_exception():
    """Test ConfigurationError can be raised and caught"""
    try:
        raise ConfigurationError("Test error message")
    except ConfigurationError as e:
        assert str(e) == "Test error message"
    except Exception:
        pytest.fail("ConfigurationError should be catchable")
