-- Bot Framework Migration Script
-- This file is part of Telegram Desktop MCP Server.
-- Licensed under GPLv3 with OpenSSL exception.
--
-- Use this script to apply the bot framework schema to an existing database
-- Version: 1.0.0

-- =============================================================================
-- Pre-Migration Checks
-- =============================================================================

-- Check if bot framework is already installed
SELECT CASE
    WHEN EXISTS (SELECT 1 FROM sqlite_master WHERE type='table' AND name='bots')
    THEN 'Bot framework tables already exist. Run upgrade script instead.'
    ELSE 'Proceeding with fresh installation...'
END AS migration_status;

-- =============================================================================
-- Apply Main Schema
-- =============================================================================

-- Execute the main schema file
-- (This should be done by loading bot_framework_schema.sql)

-- =============================================================================
-- Insert Default Data
-- =============================================================================

-- Register built-in bots (these will be created at runtime, but we can pre-register them)

-- Context Assistant Bot
INSERT OR IGNORE INTO bots (
    id, name, version, description, author, tags, is_premium, is_enabled, is_running, created_at
) VALUES (
    'context_assistant',
    'Context-Aware AI Assistant',
    '1.0.0',
    'Proactively offers help based on conversation context',
    'Telegram MCP Framework',
    '["ai", "assistant", "context", "proactive"]',
    0,  -- Not premium
    1,  -- Enabled by default
    0,  -- Not running yet
    strftime('%s', 'now')
);

-- Grant default permissions to Context Assistant Bot
INSERT OR IGNORE INTO bot_permissions (bot_id, permission, granted_at, granted_by, is_active)
VALUES
    ('context_assistant', 'read:messages', strftime('%s', 'now'), 'system', 1),
    ('context_assistant', 'read:chats', strftime('%s', 'now'), 'system', 1),
    ('context_assistant', 'send:messages', strftime('%s', 'now'), 'system', 1),
    ('context_assistant', 'read:analytics', strftime('%s', 'now'), 'system', 1);

-- Default configuration for Context Assistant
INSERT OR IGNORE INTO bot_state (bot_id, key, value, value_type, created_at, updated_at)
VALUES
    ('context_assistant', 'max_context_messages', '10', 'number', strftime('%s', 'now'), strftime('%s', 'now')),
    ('context_assistant', 'context_timeout_minutes', '30', 'number', strftime('%s', 'now'), strftime('%s', 'now')),
    ('context_assistant', 'min_confidence', '0.7', 'number', strftime('%s', 'now'), strftime('%s', 'now')),
    ('context_assistant', 'enable_learning', 'true', 'boolean', strftime('%s', 'now'), strftime('%s', 'now'));

-- =============================================================================
-- Placeholder Entries for Other Bots
-- =============================================================================

-- Smart Scheduler Bot
INSERT OR IGNORE INTO bots (
    id, name, version, description, author, tags, is_premium, is_enabled, is_running, created_at
) VALUES (
    'smart_scheduler',
    'Smart Message Scheduler',
    '1.0.0',
    'Optimizes message send timing based on recipient activity',
    'Telegram MCP Framework',
    '["scheduling", "optimization", "smart"]',
    1,  -- Premium feature
    0,  -- Disabled by default (premium)
    0,
    strftime('%s', 'now')
);

-- Advanced Search Bot
INSERT OR IGNORE INTO bots (
    id, name, version, description, author, tags, is_premium, is_enabled, is_running, created_at
) VALUES (
    'advanced_search',
    'Advanced Search & Knowledge Management',
    '1.0.0',
    'Semantic search with AI-powered understanding',
    'Telegram MCP Framework',
    '["search", "ai", "knowledge", "semantic"]',
    1,  -- Premium feature
    0,  -- Disabled by default (premium)
    0,
    strftime('%s', 'now')
);

-- Analytics Bot
INSERT OR IGNORE INTO bots (
    id, name, version, description, author, tags, is_premium, is_enabled, is_running, created_at
) VALUES (
    'analytics',
    'Privacy-Preserving Analytics',
    '1.0.0',
    '100% local communication insights and social graph analysis',
    'Telegram MCP Framework',
    '["analytics", "privacy", "insights", "visualization"]',
    0,  -- Free tier
    1,  -- Enabled by default
    0,
    strftime('%s', 'now')
);

-- Ephemeral Capture Bot
INSERT OR IGNORE INTO bots (
    id, name, version, description, author, tags, is_premium, is_enabled, is_running, created_at
) VALUES (
    'ephemeral_capture',
    'Ephemeral Message Capture',
    '1.0.0',
    'Captures self-destructing and view-once messages',
    'Telegram MCP Framework',
    '["ephemeral", "capture", "privacy", "compliance"]',
    1,  -- Premium feature
    0,  -- Disabled by default (sensitive)
    0,
    strftime('%s', 'now')
);

-- Grant special permission for ephemeral capture
INSERT OR IGNORE INTO bot_permissions (bot_id, permission, granted_at, granted_by, is_active)
VALUES
    ('ephemeral_capture', 'read:ephemeral', strftime('%s', 'now'), 'system', 0),  -- Not active by default
    ('ephemeral_capture', 'privacy:capture_ephemeral', strftime('%s', 'now'), 'system', 0);

-- Multi-Chat Coordinator Bot
INSERT OR IGNORE INTO bots (
    id, name, version, description, author, tags, is_premium, is_enabled, is_running, created_at
) VALUES (
    'multi_chat_coordinator',
    'Multi-Chat Coordinator',
    '1.0.0',
    'Smart message forwarding and cross-chat synchronization',
    'Telegram MCP Framework',
    '["coordination", "forwarding", "multi-chat", "automation"]',
    1,  -- Premium feature
    0,  -- Disabled by default (premium)
    0,
    strftime('%s', 'now')
);

-- =============================================================================
-- Post-Migration Verification
-- =============================================================================

-- Count tables created
SELECT 'Tables created: ' || COUNT(*) AS result
FROM sqlite_master
WHERE type='table' AND name LIKE 'bot%';

-- Count indexes created
SELECT 'Indexes created: ' || COUNT(*) AS result
FROM sqlite_master
WHERE type='index' AND name LIKE 'idx_bot%';

-- Count views created
SELECT 'Views created: ' || COUNT(*) AS result
FROM sqlite_master
WHERE type='view' AND name LIKE 'view_bot%';

-- Count registered bots
SELECT 'Bots registered: ' || COUNT(*) AS result FROM bots;

-- List all registered bots
SELECT
    id,
    name,
    version,
    is_enabled,
    is_premium,
    (SELECT COUNT(*) FROM bot_permissions WHERE bot_id = bots.id AND is_active = 1) AS permissions
FROM bots
ORDER BY is_premium, name;

-- =============================================================================
-- Rollback Script (for emergencies)
-- =============================================================================

-- CAUTION: This will delete all bot framework data!
-- Uncomment to execute rollback:

/*
DROP VIEW IF EXISTS view_bot_suggestion_analytics;
DROP VIEW IF EXISTS view_recent_bot_activity;
DROP VIEW IF EXISTS view_bot_stats;
DROP TABLE IF EXISTS bot_context;
DROP TABLE IF EXISTS bot_suggestions;
DROP TABLE IF EXISTS bot_user_preferences;
DROP TABLE IF EXISTS bot_metrics;
DROP TABLE IF EXISTS bot_execution_log;
DROP TABLE IF EXISTS bot_state;
DROP TABLE IF EXISTS bot_permissions;
DROP TABLE IF EXISTS bots;
DROP TABLE IF EXISTS bot_schema_version;

SELECT 'Bot framework completely removed' AS rollback_status;
*/

-- =============================================================================
-- End of Migration
-- =============================================================================

SELECT 'Migration completed successfully' AS final_status;
SELECT 'Schema version: 1.0.0' AS schema_version;
SELECT 'Run the application to start registered bots' AS next_step;
