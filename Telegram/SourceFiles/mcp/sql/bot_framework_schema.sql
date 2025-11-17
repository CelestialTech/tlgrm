-- Bot Framework Database Schema
-- This file is part of Telegram Desktop MCP Server.
-- Licensed under GPLv3 with OpenSSL exception.
--
-- SQLite schema for bot framework data persistence
-- Version: 1.0.0

-- =============================================================================
-- Bot Registry Table
-- =============================================================================
-- Stores metadata and configuration for all registered bots

CREATE TABLE IF NOT EXISTS bots (
    id TEXT PRIMARY KEY NOT NULL,                  -- Unique bot identifier (e.g., "context_assistant")
    name TEXT NOT NULL,                            -- Display name
    version TEXT NOT NULL,                         -- Semantic version (e.g., "1.0.0")
    description TEXT,                              -- Short description
    author TEXT,                                   -- Author/organization
    tags TEXT,                                     -- JSON array of categorization tags
    is_premium BOOLEAN DEFAULT 0 NOT NULL,         -- Requires paid tier
    is_enabled BOOLEAN DEFAULT 1 NOT NULL,         -- User can enable/disable
    is_running BOOLEAN DEFAULT 0 NOT NULL,         -- Current runtime status
    config JSON,                                   -- Bot-specific configuration (JSON)
    created_at INTEGER NOT NULL,                   -- Unix timestamp (seconds)
    last_started INTEGER,                          -- Unix timestamp of last start
    last_stopped INTEGER,                          -- Unix timestamp of last stop
    total_runtime_seconds INTEGER DEFAULT 0,       -- Cumulative runtime
    UNIQUE(id)
);

-- Indexes for bot registry
CREATE INDEX IF NOT EXISTS idx_bots_enabled ON bots(is_enabled);
CREATE INDEX IF NOT EXISTS idx_bots_running ON bots(is_running);
CREATE INDEX IF NOT EXISTS idx_bots_premium ON bots(is_premium);
CREATE INDEX IF NOT EXISTS idx_bots_created ON bots(created_at);

-- =============================================================================
-- Bot Permissions Table
-- =============================================================================
-- Tracks permission grants for bots (RBAC integration)

CREATE TABLE IF NOT EXISTS bot_permissions (
    bot_id TEXT NOT NULL,                          -- Foreign key to bots.id
    permission TEXT NOT NULL,                      -- Permission string (e.g., "read:messages")
    granted_at INTEGER NOT NULL,                   -- Unix timestamp
    granted_by TEXT,                               -- User ID who granted permission
    revoked_at INTEGER,                            -- Unix timestamp (NULL if active)
    revoked_by TEXT,                               -- User ID who revoked permission
    is_active BOOLEAN DEFAULT 1 NOT NULL,          -- Current status
    PRIMARY KEY(bot_id, permission),
    FOREIGN KEY(bot_id) REFERENCES bots(id) ON DELETE CASCADE
);

-- Indexes for permissions
CREATE INDEX IF NOT EXISTS idx_bot_permissions_active ON bot_permissions(bot_id, is_active);
CREATE INDEX IF NOT EXISTS idx_bot_permissions_granted ON bot_permissions(granted_at);

-- =============================================================================
-- Bot State Table
-- =============================================================================
-- Key-value storage for bot persistent state

CREATE TABLE IF NOT EXISTS bot_state (
    bot_id TEXT NOT NULL,                          -- Foreign key to bots.id
    key TEXT NOT NULL,                             -- State key
    value TEXT,                                    -- JSON-encoded value
    value_type TEXT DEFAULT 'json',                -- Type hint: json, string, number, boolean
    updated_at INTEGER NOT NULL,                   -- Unix timestamp
    created_at INTEGER NOT NULL,                   -- Unix timestamp
    PRIMARY KEY(bot_id, key),
    FOREIGN KEY(bot_id) REFERENCES bots(id) ON DELETE CASCADE
);

-- Indexes for bot state
CREATE INDEX IF NOT EXISTS idx_bot_state_updated ON bot_state(bot_id, updated_at);
CREATE INDEX IF NOT EXISTS idx_bot_state_created ON bot_state(created_at);

-- =============================================================================
-- Bot Execution Log Table
-- =============================================================================
-- Tracks bot execution events for debugging and auditing

CREATE TABLE IF NOT EXISTS bot_execution_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    bot_id TEXT NOT NULL,                          -- Foreign key to bots.id
    event_type TEXT NOT NULL,                      -- 'message', 'command', 'error', 'startup', 'shutdown'
    event_subtype TEXT,                            -- Additional classification
    chat_id INTEGER,                               -- Related chat (if applicable)
    user_id INTEGER,                               -- Related user (if applicable)
    message_id INTEGER,                            -- Related message (if applicable)
    execution_time_ms INTEGER,                     -- Execution duration in milliseconds
    success BOOLEAN DEFAULT 1 NOT NULL,            -- Whether operation succeeded
    error_message TEXT,                            -- Error details (if failed)
    metadata JSON,                                 -- Additional event data
    timestamp INTEGER NOT NULL,                    -- Unix timestamp
    FOREIGN KEY(bot_id) REFERENCES bots(id) ON DELETE CASCADE
);

-- Indexes for execution log
CREATE INDEX IF NOT EXISTS idx_bot_exec_log_bot ON bot_execution_log(bot_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_bot_exec_log_event ON bot_execution_log(event_type, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_bot_exec_log_chat ON bot_execution_log(chat_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_bot_exec_log_success ON bot_execution_log(success);
CREATE INDEX IF NOT EXISTS idx_bot_exec_log_timestamp ON bot_execution_log(timestamp DESC);

-- =============================================================================
-- Bot Metrics Table
-- =============================================================================
-- Aggregated performance metrics for bots

CREATE TABLE IF NOT EXISTS bot_metrics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    bot_id TEXT NOT NULL,                          -- Foreign key to bots.id
    metric_type TEXT NOT NULL,                     -- 'performance', 'usage', 'error_rate', etc.
    metric_name TEXT NOT NULL,                     -- Specific metric (e.g., 'avg_response_time')
    metric_value REAL NOT NULL,                    -- Numeric value
    metric_unit TEXT,                              -- Unit (e.g., 'ms', 'count', 'percentage')
    period_start INTEGER NOT NULL,                 -- Unix timestamp (start of measurement period)
    period_end INTEGER NOT NULL,                   -- Unix timestamp (end of measurement period)
    sample_count INTEGER DEFAULT 1,                -- Number of samples in this metric
    metadata JSON,                                 -- Additional metric metadata
    recorded_at INTEGER NOT NULL,                  -- Unix timestamp
    FOREIGN KEY(bot_id) REFERENCES bots(id) ON DELETE CASCADE
);

-- Indexes for bot metrics
CREATE INDEX IF NOT EXISTS idx_bot_metrics_bot ON bot_metrics(bot_id, recorded_at DESC);
CREATE INDEX IF NOT EXISTS idx_bot_metrics_type ON bot_metrics(metric_type, metric_name);
CREATE INDEX IF NOT EXISTS idx_bot_metrics_period ON bot_metrics(period_start, period_end);
CREATE INDEX IF NOT EXISTS idx_bot_metrics_recorded ON bot_metrics(recorded_at DESC);

-- =============================================================================
-- User Preferences Table
-- =============================================================================
-- Stores user-specific bot preferences and settings

CREATE TABLE IF NOT EXISTS bot_user_preferences (
    user_id INTEGER NOT NULL,                      -- Telegram user ID
    bot_id TEXT NOT NULL,                          -- Foreign key to bots.id
    preferences JSON NOT NULL,                     -- User preferences (JSON object)
    enabled BOOLEAN DEFAULT 1 NOT NULL,            -- User has enabled this bot
    notification_level TEXT DEFAULT 'normal',      -- 'silent', 'normal', 'verbose'
    created_at INTEGER NOT NULL,                   -- Unix timestamp
    updated_at INTEGER NOT NULL,                   -- Unix timestamp
    PRIMARY KEY(user_id, bot_id),
    FOREIGN KEY(bot_id) REFERENCES bots(id) ON DELETE CASCADE
);

-- Indexes for user preferences
CREATE INDEX IF NOT EXISTS idx_bot_user_prefs_user ON bot_user_preferences(user_id);
CREATE INDEX IF NOT EXISTS idx_bot_user_prefs_bot ON bot_user_preferences(bot_id);
CREATE INDEX IF NOT EXISTS idx_bot_user_prefs_enabled ON bot_user_preferences(enabled);

-- =============================================================================
-- Bot Suggestions Table
-- =============================================================================
-- Tracks suggestions offered by bots and user responses

CREATE TABLE IF NOT EXISTS bot_suggestions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    bot_id TEXT NOT NULL,                          -- Foreign key to bots.id
    chat_id INTEGER NOT NULL,                      -- Chat where suggestion was offered
    user_id INTEGER NOT NULL,                      -- Target user
    suggestion_type TEXT NOT NULL,                 -- Type of suggestion (e.g., 'search', 'task', 'summary')
    suggestion_text TEXT NOT NULL,                 -- The actual suggestion
    context JSON,                                  -- Context that triggered suggestion
    confidence REAL,                               -- Confidence score (0.0 to 1.0)
    offered_at INTEGER NOT NULL,                   -- Unix timestamp
    responded_at INTEGER,                          -- Unix timestamp (NULL if no response)
    response_type TEXT,                            -- 'accepted', 'rejected', 'ignored', 'modified'
    response_data JSON,                            -- Response details
    FOREIGN KEY(bot_id) REFERENCES bots(id) ON DELETE CASCADE
);

-- Indexes for bot suggestions
CREATE INDEX IF NOT EXISTS idx_bot_suggestions_bot ON bot_suggestions(bot_id, offered_at DESC);
CREATE INDEX IF NOT EXISTS idx_bot_suggestions_chat ON bot_suggestions(chat_id, offered_at DESC);
CREATE INDEX IF NOT EXISTS idx_bot_suggestions_user ON bot_suggestions(user_id, offered_at DESC);
CREATE INDEX IF NOT EXISTS idx_bot_suggestions_type ON bot_suggestions(suggestion_type);
CREATE INDEX IF NOT EXISTS idx_bot_suggestions_response ON bot_suggestions(response_type);

-- =============================================================================
-- Bot Context Table
-- =============================================================================
-- Stores conversation context for context-aware bots

CREATE TABLE IF NOT EXISTS bot_context (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    bot_id TEXT NOT NULL,                          -- Foreign key to bots.id
    chat_id INTEGER NOT NULL,                      -- Chat ID
    context_type TEXT NOT NULL,                    -- Type of context (e.g., 'conversation', 'task', 'decision')
    context_data JSON NOT NULL,                    -- Serialized context data
    intent TEXT,                                   -- Classified intent
    topics TEXT,                                   -- JSON array of detected topics
    entities TEXT,                                 -- JSON array of extracted entities
    confidence REAL,                               -- Context confidence (0.0 to 1.0)
    created_at INTEGER NOT NULL,                   -- Unix timestamp
    updated_at INTEGER NOT NULL,                   -- Unix timestamp
    expires_at INTEGER,                            -- Unix timestamp (NULL for no expiration)
    FOREIGN KEY(bot_id) REFERENCES bots(id) ON DELETE CASCADE
);

-- Indexes for bot context
CREATE INDEX IF NOT EXISTS idx_bot_context_bot_chat ON bot_context(bot_id, chat_id, updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_bot_context_type ON bot_context(context_type);
CREATE INDEX IF NOT EXISTS idx_bot_context_intent ON bot_context(intent);
CREATE INDEX IF NOT EXISTS idx_bot_context_expires ON bot_context(expires_at);
CREATE INDEX IF NOT EXISTS idx_bot_context_updated ON bot_context(updated_at DESC);

-- =============================================================================
-- Schema Version Table
-- =============================================================================
-- Tracks schema migrations

CREATE TABLE IF NOT EXISTS bot_schema_version (
    version TEXT PRIMARY KEY NOT NULL,
    applied_at INTEGER NOT NULL,
    description TEXT
);

-- Insert initial version
INSERT OR IGNORE INTO bot_schema_version (version, applied_at, description)
VALUES ('1.0.0', strftime('%s', 'now'), 'Initial bot framework schema');

-- =============================================================================
-- Views for Common Queries
-- =============================================================================

-- View: Bot statistics overview
CREATE VIEW IF NOT EXISTS view_bot_stats AS
SELECT
    b.id,
    b.name,
    b.version,
    b.is_enabled,
    b.is_running,
    b.total_runtime_seconds,
    COUNT(DISTINCT bel.id) AS total_events,
    SUM(CASE WHEN bel.success = 0 THEN 1 ELSE 0 END) AS error_count,
    AVG(bel.execution_time_ms) AS avg_execution_time_ms,
    COUNT(DISTINCT bs.id) AS suggestion_count,
    SUM(CASE WHEN bs.response_type = 'accepted' THEN 1 ELSE 0 END) AS accepted_suggestions,
    (SELECT COUNT(*) FROM bot_permissions WHERE bot_id = b.id AND is_active = 1) AS active_permissions
FROM bots b
LEFT JOIN bot_execution_log bel ON b.id = bel.bot_id
LEFT JOIN bot_suggestions bs ON b.id = bs.bot_id
GROUP BY b.id;

-- View: Recent bot activity
CREATE VIEW IF NOT EXISTS view_recent_bot_activity AS
SELECT
    bel.bot_id,
    b.name AS bot_name,
    bel.event_type,
    bel.chat_id,
    bel.execution_time_ms,
    bel.success,
    bel.error_message,
    bel.timestamp
FROM bot_execution_log bel
JOIN bots b ON bel.bot_id = b.id
ORDER BY bel.timestamp DESC
LIMIT 100;

-- View: Bot suggestion analytics
CREATE VIEW IF NOT EXISTS view_bot_suggestion_analytics AS
SELECT
    bs.bot_id,
    b.name AS bot_name,
    bs.suggestion_type,
    COUNT(*) AS total_offered,
    SUM(CASE WHEN bs.response_type = 'accepted' THEN 1 ELSE 0 END) AS accepted,
    SUM(CASE WHEN bs.response_type = 'rejected' THEN 1 ELSE 0 END) AS rejected,
    SUM(CASE WHEN bs.response_type IS NULL THEN 1 ELSE 0 END) AS no_response,
    ROUND(AVG(bs.confidence), 3) AS avg_confidence,
    ROUND(
        100.0 * SUM(CASE WHEN bs.response_type = 'accepted' THEN 1 ELSE 0 END) / COUNT(*),
        2
    ) AS acceptance_rate_percent
FROM bot_suggestions bs
JOIN bots b ON bs.bot_id = b.id
GROUP BY bs.bot_id, bs.suggestion_type;

-- =============================================================================
-- Triggers for Automatic Timestamp Updates
-- =============================================================================

-- Trigger: Update bot updated_at timestamp (if we add this column later)
-- CREATE TRIGGER IF NOT EXISTS update_bot_timestamp
-- AFTER UPDATE ON bots
-- FOR EACH ROW
-- BEGIN
--     UPDATE bots SET updated_at = strftime('%s', 'now') WHERE id = NEW.id;
-- END;

-- =============================================================================
-- Utility Functions (stored as comments for reference)
-- =============================================================================

-- To get bot runtime statistics:
-- SELECT
--     bot_id,
--     COUNT(*) as execution_count,
--     AVG(execution_time_ms) as avg_time,
--     MAX(execution_time_ms) as max_time,
--     SUM(CASE WHEN success = 0 THEN 1 ELSE 0 END) as error_count
-- FROM bot_execution_log
-- WHERE timestamp > strftime('%s', 'now') - 86400  -- Last 24 hours
-- GROUP BY bot_id;

-- To get bot suggestion acceptance rate:
-- SELECT
--     bot_id,
--     suggestion_type,
--     ROUND(100.0 * SUM(CASE WHEN response_type = 'accepted' THEN 1 ELSE 0 END) / COUNT(*), 2) as acceptance_rate
-- FROM bot_suggestions
-- GROUP BY bot_id, suggestion_type;

-- To clean up old execution logs (keep last 30 days):
-- DELETE FROM bot_execution_log
-- WHERE timestamp < strftime('%s', 'now') - (30 * 86400);

-- To clean up expired contexts:
-- DELETE FROM bot_context
-- WHERE expires_at IS NOT NULL AND expires_at < strftime('%s', 'now');

-- =============================================================================
-- End of Schema
-- =============================================================================
