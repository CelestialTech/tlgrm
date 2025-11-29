# Configuration Validation - Quick Reference

## What Was Implemented

Configuration validation system that checks:
- Environment variables (Telegram API credentials)
- File paths (cache database)
- Port numbers (health check, metrics, MCP server)

## Files

### Created
- `src/core/validation.py` - Core validation logic
- `test_validation.py` - Test script
- `VALIDATION_USAGE.md` - Full documentation
- `example_validation_output.txt` - Error message examples

### Modified
- `src/core/config.py` - Integrated validation into load_config()
- `src/core/__init__.py` - Exported validation functions

## Key Functions

### `validate_config(config: Config) -> List[str]`
Returns list of error messages (empty if valid)

### `ConfigurationError`
Exception raised when validation fails

## Usage

### Automatic (Recommended)
```python
from src.core import load_config, ConfigurationError

try:
    config = load_config()
except ConfigurationError as e:
    print(f"Error: {e}")
    exit(1)
```

### Manual
```python
from src.core import Config, validate_config

errors = validate_config(config)
if errors:
    # Handle errors
```

## Validations Performed

### Environment Variables
- ✓ TELEGRAM_API_ID exists and is numeric
- ✓ TELEGRAM_API_HASH exists and is non-empty
- ✓ TELEGRAM_BOT_TOKEN is non-empty (if set)

### File Paths
- ✓ Cache database parent directory exists
- ✓ Cache database path is writable (if use_sqlite=true)

### Ports
- ✓ All ports in range 1024-65535
- ✓ No port conflicts between services
- ✓ Validates: health_check_port, metrics_port, mcp.port

## Testing

```bash
python3 test_validation.py
```

## Quick Setup

1. **Create .env file:**
```bash
TELEGRAM_API_ID=12345678
TELEGRAM_API_HASH=abc123def456789
```

2. **Create data directory (if using SQLite cache):**
```bash
mkdir -p ./data
```

3. **Load configuration:**
```python
config = load_config()  # Validates automatically
```

## Common Errors and Fixes

### Missing API ID
```
Fix: Set TELEGRAM_API_ID in your .env file
Obtain from: https://my.telegram.org/apps
```

### Invalid Port
```
Fix: Set health_check_port to a value between 1024 and 65535
```

### Port Conflict
```
Fix: Set metrics_port and health_check_port to different values
```

### Missing Directory
```
Fix: Create the directory: mkdir -p /path/to/directory
```

## Error Message Format

```
Configuration validation failed:

  1. [Problem description]
     Fix: [Specific solution]
     [Optional: URL or command]

  2. [Next problem...]
```

## Integration

Validation runs automatically when:
- Calling `load_config()`
- Starting the server
- Running any script that loads configuration

If validation fails:
- Errors are logged
- ConfigurationError is raised
- Server startup is prevented

## Documentation

- Full guide: `VALIDATION_USAGE.md`
- Implementation details: `IMPLEMENTATION_SUMMARY.md`
- Examples: `example_validation_output.txt`
- Test script: `test_validation.py`
