# Configuration Validation Usage Guide

## Overview

The configuration validation system ensures that all required settings are properly configured before the Telegram MCP Server starts. This prevents runtime errors and provides clear, actionable error messages.

## Automatic Validation

Validation runs automatically when you load configuration:

```python
from src.core import load_config, ConfigurationError

try:
    config = load_config()
except ConfigurationError as e:
    print(f"Configuration error: {e}")
    # Fix the issues and retry
```

## Manual Validation

You can also validate a configuration object manually:

```python
from src.core import Config, validate_config

config = Config()
errors = validate_config(config)

if errors:
    for error in errors:
        print(f"Error: {error}")
else:
    print("Configuration is valid!")
```

## What Gets Validated

### 1. Environment Variables

#### TELEGRAM_API_ID
- **Required**: Yes
- **Type**: Numeric
- **Error if**: Missing or non-numeric
- **Fix**: Set in `.env` file or environment
- **Obtain from**: https://my.telegram.org/apps

```bash
TELEGRAM_API_ID=12345678
```

#### TELEGRAM_API_HASH
- **Required**: Yes
- **Type**: Non-empty string
- **Error if**: Missing or empty/whitespace
- **Fix**: Set in `.env` file or environment
- **Obtain from**: https://my.telegram.org/apps

```bash
TELEGRAM_API_HASH=abc123def456789
```

#### TELEGRAM_BOT_TOKEN
- **Required**: No (only for bot mode)
- **Type**: Non-empty string
- **Error if**: Set but empty/whitespace
- **Fix**: Either set to valid token or remove
- **Obtain from**: @BotFather on Telegram

```bash
TELEGRAM_BOT_TOKEN=123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11
```

### 2. File Paths

#### Cache Database Path (if SQLite enabled)
- **Validates**: Parent directory exists and is writable
- **Config**: `cache.db_path` when `cache.use_sqlite = true`
- **Error if**: Parent directory doesn't exist or isn't writable
- **Fix**: Create directory with write permissions

```toml
[cache]
use_sqlite = true
db_path = "./data/cache.db"  # Ensure ./data exists
```

```bash
mkdir -p ./data
chmod u+w ./data
```

### 3. Port Numbers

#### Health Check Port
- **Validates**: Port in range 1024-65535
- **Config**: `monitoring.health_check_port`
- **Error if**: Port < 1024 or > 65535
- **Fix**: Set to valid port number

```toml
[monitoring]
enable_health_check = true
health_check_port = 8080  # Valid range: 1024-65535
```

#### Metrics Port
- **Validates**: Port in range 1024-65535, no conflicts
- **Config**: `monitoring.metrics_port`
- **Error if**: Port < 1024, > 65535, or conflicts with health_check_port
- **Fix**: Set to valid, unique port number

```toml
[monitoring]
enable_metrics = true
metrics_port = 9090  # Must be different from health_check_port
```

#### MCP Server Port (non-stdio transport)
- **Validates**: Port in range 1024-65535, no conflicts
- **Config**: `mcp.port`
- **Error if**: Port < 1024, > 65535, or conflicts with monitoring ports
- **Fix**: Set to valid, unique port number

```toml
[mcp]
transport = "http"
port = 8000  # Must be different from monitoring ports
```

## Error Message Format

Error messages are designed to be helpful and actionable:

```
Configuration validation failed:

  1. Missing required environment variable: TELEGRAM_API_ID
     Fix: Set TELEGRAM_API_ID in your .env file or environment.
     Obtain from: https://my.telegram.org/apps

  2. Invalid metrics_port: 70000
     Fix: Set metrics_port to a value between 1024 and 65535
```

## Example: Complete Setup

### 1. Create `.env` file:

```bash
# Required Telegram credentials
TELEGRAM_API_ID=12345678
TELEGRAM_API_HASH=abc123def456789

# Optional: For bot mode
TELEGRAM_BOT_TOKEN=123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11

# Optional: Access control
ALLOWED_CHATS=chat1,chat2
ALLOWED_USERS=user1,user2

# Optional: Logging
LOG_LEVEL=INFO
```

### 2. Create `config.toml`:

```toml
[mcp]
server_name = "Telegram MCP Server"
transport = "stdio"
log_level = "INFO"

[telegram]
session_name = "telegram_mcp"

[cache]
use_sqlite = true
db_path = "./data/cache.db"
max_messages = 50
ttl_seconds = 300

[monitoring]
enable_health_check = true
health_check_port = 8080
enable_metrics = true
metrics_port = 9090
```

### 3. Create data directory:

```bash
mkdir -p ./data
```

### 4. Load and validate:

```python
from src.core import load_config, ConfigurationError

try:
    config = load_config()
    print("Configuration loaded successfully!")
except ConfigurationError as e:
    print(f"Configuration error:\n{e}")
    exit(1)
```

## Testing Validation

Run the test script to see validation in action:

```bash
python3 test_validation.py
```

This demonstrates various validation scenarios including:
- Missing environment variables
- Invalid API credentials
- Port conflicts
- Invalid file paths
- Valid configurations

## Integration in Main Server

The validation is automatically integrated into the server startup:

```python
# In main.py or server startup
from src.core import load_config, ConfigurationError

try:
    config = load_config(environment="prod")
except ConfigurationError as e:
    logger.error(f"Failed to start: {e}")
    sys.exit(1)

# Server starts with validated configuration
```

## Validation Function Reference

### `validate_config(config: Config) -> List[str]`

Validates a configuration object and returns a list of error messages.

**Parameters:**
- `config`: Configuration instance to validate

**Returns:**
- List of error messages (empty list if validation passes)

**Example:**

```python
from src.core import Config, validate_config

config = Config()
errors = validate_config(config)

if not errors:
    print("Valid configuration!")
else:
    print(f"Found {len(errors)} errors:")
    for error in errors:
        print(f"  - {error}")
```

### `ConfigurationError`

Exception raised when configuration validation fails.

**Example:**

```python
from src.core import load_config, ConfigurationError

try:
    config = load_config()
except ConfigurationError as e:
    # Handle configuration errors
    print(f"Config error: {e}")
```

## Best Practices

1. **Always validate early**: Load and validate configuration at startup
2. **Check error messages**: They contain specific fix instructions
3. **Use environment files**: Keep credentials in `.env`, not in code
4. **Test configurations**: Use the test script to verify changes
5. **Handle errors gracefully**: Catch ConfigurationError and exit cleanly
