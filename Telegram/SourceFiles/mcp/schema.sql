-- ===================================
-- Telegram MCP Server - Database Schema
-- ===================================
-- Comprehensive database schema for message archival, analytics, and AI features
-- Based on discordMCP architecture with Telegram-specific adaptations

-- ===================================
-- 1. MESSAGE ARCHIVAL
-- ===================================

-- Main message archive table
CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    message_id INTEGER NOT NULL,
    chat_id INTEGER NOT NULL,
    user_id INTEGER,
    username TEXT,
    first_name TEXT,
    last_name TEXT,
    content TEXT,
    timestamp INTEGER NOT NULL,  -- Unix timestamp
    date TEXT,  -- ISO8601 format for queries
    message_type TEXT DEFAULT 'text',  -- text, photo, video, voice, sticker, document, etc.
    reply_to_message_id INTEGER,
    forward_from_chat_id INTEGER,
    forward_from_message_id INTEGER,
    edit_date INTEGER,
    media_path TEXT,  -- Local path to downloaded media
    media_url TEXT,   -- Telegram file_id or URL
    media_size INTEGER,
    media_mime_type TEXT,
    has_media BOOLEAN DEFAULT 0,
    is_forwarded BOOLEAN DEFAULT 0,
    is_reply BOOLEAN DEFAULT 0,
    metadata TEXT,  -- JSON blob for additional data
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    UNIQUE(chat_id, message_id)
);

-- Indexes for fast queries
CREATE INDEX IF NOT EXISTS idx_messages_chat_timestamp ON messages(chat_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_messages_user ON messages(user_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_messages_date ON messages(date);
CREATE INDEX IF NOT EXISTS idx_messages_type ON messages(message_type);
CREATE INDEX IF NOT EXISTS idx_messages_content_fts ON messages(content) WHERE content IS NOT NULL;

-- ===================================
-- 2. EPHEMERAL MESSAGE ARCHIVAL
-- ===================================

-- Self-destructing and view-once messages
CREATE TABLE IF NOT EXISTS ephemeral_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    message_id INTEGER NOT NULL,
    chat_id INTEGER NOT NULL,
    user_id INTEGER,
    username TEXT,
    ephemeral_type TEXT NOT NULL,  -- 'self_destruct', 'view_once', 'vanishing'
    ttl_seconds INTEGER,  -- Time to live (for self-destruct)
    content TEXT,
    media_type TEXT,  -- photo, video, voice
    media_path TEXT,  -- Saved media file
    captured_at INTEGER NOT NULL,  -- When we archived it
    scheduled_deletion INTEGER,  -- When it would have been deleted
    views_count INTEGER DEFAULT 0,  -- For view-once tracking
    metadata TEXT,  -- JSON blob
    UNIQUE(chat_id, message_id)
);

CREATE INDEX IF NOT EXISTS idx_ephemeral_chat ON ephemeral_messages(chat_id, captured_at DESC);
CREATE INDEX IF NOT EXISTS idx_ephemeral_type ON ephemeral_messages(ephemeral_type);

-- ===================================
-- 3. CHAT METADATA
-- ===================================

-- Chat/channel/group information
CREATE TABLE IF NOT EXISTS chats (
    chat_id INTEGER PRIMARY KEY,
    chat_type TEXT NOT NULL,  -- 'private', 'group', 'supergroup', 'channel'
    title TEXT,
    username TEXT,
    description TEXT,
    member_count INTEGER,
    photo_path TEXT,
    is_archived BOOLEAN DEFAULT 0,
    first_seen INTEGER,
    last_updated INTEGER,
    metadata TEXT  -- JSON blob
);

-- User information
CREATE TABLE IF NOT EXISTS users (
    user_id INTEGER PRIMARY KEY,
    username TEXT,
    first_name TEXT,
    last_name TEXT,
    phone TEXT,
    is_bot BOOLEAN DEFAULT 0,
    is_premium BOOLEAN DEFAULT 0,
    language_code TEXT,
    photo_path TEXT,
    first_seen INTEGER,
    last_updated INTEGER,
    metadata TEXT  -- JSON blob
);

-- ===================================
-- 4. ANALYTICS TABLES
-- ===================================

-- Daily message statistics (materialized view concept)
CREATE TABLE IF NOT EXISTS message_stats_daily (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    date TEXT NOT NULL,
    chat_id INTEGER NOT NULL,
    message_count INTEGER DEFAULT 0,
    unique_users INTEGER DEFAULT 0,
    avg_message_length REAL DEFAULT 0,
    total_words INTEGER DEFAULT 0,
    media_count INTEGER DEFAULT 0,
    UNIQUE(date, chat_id)
);

CREATE INDEX IF NOT EXISTS idx_stats_daily_date ON message_stats_daily(date DESC);
CREATE INDEX IF NOT EXISTS idx_stats_daily_chat ON message_stats_daily(chat_id, date DESC);

-- User activity summary
CREATE TABLE IF NOT EXISTS user_activity_summary (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    chat_id INTEGER NOT NULL,
    message_count INTEGER DEFAULT 0,
    word_count INTEGER DEFAULT 0,
    avg_message_length REAL DEFAULT 0,
    most_active_hour INTEGER,  -- 0-23
    first_message_date INTEGER,
    last_message_date INTEGER,
    days_active INTEGER DEFAULT 0,
    updated_at INTEGER,
    UNIQUE(user_id, chat_id)
);

CREATE INDEX IF NOT EXISTS idx_user_activity ON user_activity_summary(user_id, chat_id);

-- Chat activity summary
CREATE TABLE IF NOT EXISTS chat_activity_summary (
    chat_id INTEGER PRIMARY KEY,
    total_messages INTEGER DEFAULT 0,
    unique_users INTEGER DEFAULT 0,
    messages_per_day REAL DEFAULT 0,
    peak_hour INTEGER,  -- 0-23
    first_message_date INTEGER,
    last_message_date INTEGER,
    activity_trend TEXT,  -- 'increasing', 'decreasing', 'stable'
    updated_at INTEGER
);

-- ===================================
-- 5. SEMANTIC SEARCH & AI FEATURES
-- ===================================

-- Message embeddings for semantic search
-- Note: SQLite doesn't have native vector type, so we store as BLOB
CREATE TABLE IF NOT EXISTS message_embeddings (
    message_id INTEGER PRIMARY KEY,
    chat_id INTEGER NOT NULL,
    content TEXT NOT NULL,
    embedding BLOB,  -- Serialized vector (384 dimensions for all-MiniLM-L6-v2)
    embedding_model TEXT DEFAULT 'all-MiniLM-L6-v2',
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    FOREIGN KEY (message_id) REFERENCES messages(id)
);

CREATE INDEX IF NOT EXISTS idx_embeddings_chat ON message_embeddings(chat_id);

-- Intent classification results
CREATE TABLE IF NOT EXISTS message_intents (
    message_id INTEGER PRIMARY KEY,
    intent_type TEXT NOT NULL,  -- QUESTION, ANSWER, COMMAND, GREETING, etc.
    confidence REAL DEFAULT 0.0,
    entities TEXT,  -- JSON array of extracted entities
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    FOREIGN KEY (message_id) REFERENCES messages(id)
);

-- Message clusters (for topic grouping)
CREATE TABLE IF NOT EXISTS message_clusters (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    cluster_id INTEGER NOT NULL,
    message_id INTEGER NOT NULL,
    chat_id INTEGER NOT NULL,
    topic_label TEXT,
    similarity_score REAL,
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    FOREIGN KEY (message_id) REFERENCES messages(id)
);

-- ===================================
-- 6. SCHEDULED MESSAGES
-- ===================================

CREATE TABLE IF NOT EXISTS scheduled_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    chat_id INTEGER NOT NULL,
    content TEXT NOT NULL,
    schedule_type TEXT NOT NULL,  -- 'once', 'recurring', 'delayed'
    scheduled_time INTEGER,  -- Unix timestamp for 'once'
    delay_seconds INTEGER,  -- For 'delayed'
    recurrence_pattern TEXT,  -- 'hourly', 'daily', 'weekly', 'monthly'
    start_time INTEGER,
    max_occurrences INTEGER,
    occurrences_sent INTEGER DEFAULT 0,
    last_sent INTEGER,
    next_scheduled INTEGER,
    is_active BOOLEAN DEFAULT 1,
    created_by TEXT,  -- API key or user identifier
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    updated_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_scheduled_next ON scheduled_messages(next_scheduled, is_active);

-- ===================================
-- 7. AUDIT LOGGING
-- ===================================

CREATE TABLE IF NOT EXISTS audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type TEXT NOT NULL,  -- 'tool_invoked', 'auth_event', 'discord_op', 'system_event'
    event_subtype TEXT,  -- Specific operation
    user_id TEXT,  -- API key or user identifier
    tool_name TEXT,
    parameters TEXT,  -- JSON blob
    result_status TEXT,  -- 'success', 'failure', 'partial'
    error_message TEXT,
    duration_ms INTEGER,
    timestamp INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    metadata TEXT  -- JSON blob for additional context
);

CREATE INDEX IF NOT EXISTS idx_audit_type ON audit_log(event_type, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_audit_user ON audit_log(user_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_audit_tool ON audit_log(tool_name, timestamp DESC);

-- ===================================
-- 8. AUTHENTICATION & AUTHORIZATION
-- ===================================

-- API keys for multi-user access
CREATE TABLE IF NOT EXISTS api_keys (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    api_key_hash TEXT UNIQUE NOT NULL,  -- SHA-256 hash
    api_key_prefix TEXT,  -- First 8 chars for identification
    name TEXT,
    role TEXT NOT NULL DEFAULT 'readonly',  -- 'admin', 'developer', 'bot', 'readonly'
    permissions TEXT,  -- JSON array of specific permissions
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    expires_at INTEGER,
    last_used_at INTEGER,
    is_revoked BOOLEAN DEFAULT 0,
    metadata TEXT
);

CREATE INDEX IF NOT EXISTS idx_apikeys_hash ON api_keys(api_key_hash);
CREATE INDEX IF NOT EXISTS idx_apikeys_active ON api_keys(is_revoked, expires_at);

-- ===================================
-- 9. BATCH OPERATIONS
-- ===================================

-- Track batch operation results
CREATE TABLE IF NOT EXISTS batch_operations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    operation_type TEXT NOT NULL,  -- 'SEND_MESSAGES', 'DELETE_MESSAGES', etc.
    total_operations INTEGER NOT NULL,
    successful_operations INTEGER DEFAULT 0,
    failed_operations INTEGER DEFAULT 0,
    status TEXT DEFAULT 'pending',  -- 'pending', 'running', 'completed', 'failed', 'partial'
    started_at INTEGER,
    completed_at INTEGER,
    total_duration_ms INTEGER,
    results TEXT,  -- JSON blob with detailed results
    errors TEXT,  -- JSON array of errors
    created_by TEXT,
    metadata TEXT
);

CREATE INDEX IF NOT EXISTS idx_batch_status ON batch_operations(status, started_at DESC);

-- ===================================
-- 10. ANALYTICS EVENTS
-- ===================================

-- Generic event tracking for analytics
CREATE TABLE IF NOT EXISTS analytics_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type TEXT NOT NULL,
    event_name TEXT,
    chat_id INTEGER,
    user_id INTEGER,
    data TEXT,  -- JSON blob
    timestamp INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

CREATE INDEX IF NOT EXISTS idx_analytics_type ON analytics_events(event_type, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_analytics_chat ON analytics_events(chat_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_analytics_user ON analytics_events(user_id, timestamp DESC);

-- ===================================
-- 11. EXPORT METADATA
-- ===================================

-- Track export operations
CREATE TABLE IF NOT EXISTS export_metadata (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    export_type TEXT NOT NULL,  -- 'json', 'csv', 'jsonl'
    chat_id INTEGER,
    start_date TEXT,
    end_date TEXT,
    message_count INTEGER,
    file_path TEXT,
    file_size INTEGER,
    checksum TEXT,  -- SHA-256 of exported file
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    created_by TEXT
);

-- ===================================
-- 12. VOICE TRANSCRIPTIONS
-- ===================================

-- Store voice message transcriptions
CREATE TABLE IF NOT EXISTS voice_transcriptions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    message_id INTEGER NOT NULL,
    chat_id INTEGER NOT NULL,
    voice_file_path TEXT,
    transcription_text TEXT,
    language TEXT,
    confidence REAL,
    duration_seconds REAL,
    model TEXT,  -- 'whisper-tiny', 'whisper-base', etc.
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    FOREIGN KEY (message_id) REFERENCES messages(id),
    UNIQUE(message_id)
);

-- ===================================
-- 13. PROMETHEUS METRICS (Cached)
-- ===================================

-- Cache for Prometheus metrics
CREATE TABLE IF NOT EXISTS metrics_cache (
    metric_name TEXT PRIMARY KEY,
    metric_value REAL,
    metric_type TEXT,  -- 'counter', 'gauge', 'histogram'
    labels TEXT,  -- JSON blob
    updated_at INTEGER DEFAULT (strftime('%s', 'now'))
);

-- ===================================
-- VIEWS FOR QUICK QUERIES
-- ===================================

-- Recent activity view
CREATE VIEW IF NOT EXISTS recent_activity AS
SELECT
    m.chat_id,
    c.title as chat_title,
    COUNT(*) as message_count,
    COUNT(DISTINCT m.user_id) as unique_users,
    MAX(m.timestamp) as last_message_time
FROM messages m
LEFT JOIN chats c ON m.chat_id = c.chat_id
WHERE m.timestamp > (strftime('%s', 'now') - 86400)  -- Last 24 hours
GROUP BY m.chat_id, c.title;

-- Top users by message count
CREATE VIEW IF NOT EXISTS top_users AS
SELECT
    u.user_id,
    u.username,
    u.first_name,
    COUNT(m.id) as total_messages,
    MAX(m.timestamp) as last_message
FROM users u
LEFT JOIN messages m ON u.user_id = m.user_id
GROUP BY u.user_id, u.username, u.first_name
ORDER BY total_messages DESC;

-- ===================================
-- TRIGGERS FOR AUTOMATIC STATS UPDATES
-- ===================================

-- Update chat activity summary on new message
CREATE TRIGGER IF NOT EXISTS update_chat_stats_on_insert
AFTER INSERT ON messages
BEGIN
    INSERT OR REPLACE INTO chat_activity_summary (
        chat_id,
        total_messages,
        unique_users,
        first_message_date,
        last_message_date,
        updated_at
    )
    SELECT
        NEW.chat_id,
        COUNT(*),
        COUNT(DISTINCT user_id),
        MIN(timestamp),
        MAX(timestamp),
        strftime('%s', 'now')
    FROM messages
    WHERE chat_id = NEW.chat_id;
END;

-- ===================================
-- INITIALIZATION DATA
-- ===================================

-- Default admin API key (hash of 'admin_key_change_me')
-- IMPORTANT: Change this in production!
INSERT OR IGNORE INTO api_keys (api_key_hash, api_key_prefix, name, role, permissions)
VALUES (
    'ee26b0dd4af7e749aa1a8ee3c10ae9923f618980772e473f8819a5d4940e0db27ac185f8a0e1d5f84f88bc887fd67b143732c304cc5fa9ad8e6f57f50028a8ff',
    'admin_ke',
    'Default Admin Key',
    'admin',
    '["*"]'
);

-- ===================================
-- DATABASE VERSION
-- ===================================

CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    applied_at INTEGER DEFAULT (strftime('%s', 'now'))
);

INSERT OR REPLACE INTO schema_version (version) VALUES (1);
