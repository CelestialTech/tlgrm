# Bot Framework Architecture
## Technical Implementation Specification

> **Version**: 1.0.0
> **Date**: 2025-11-16
> **Language**: C++17 with Qt 6

---

## Table of Contents
- [Architecture Overview](#architecture-overview)
- [Core Components](#core-components)
- [Bot Lifecycle](#bot-lifecycle)
- [Plugin System](#plugin-system)
- [Security Architecture](#security-architecture)
- [Performance Optimization](#performance-optimization)

---

## Architecture Overview

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Telegram Desktop (tdesktop)               │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                   MCP Server (Port 3000)               │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │            Bot Framework Layer                   │  │  │
│  │  │  ┌────────────┐  ┌────────────┐  ┌──────────┐  │  │  │
│  │  │  │ Bot        │  │ Bot        │  │ Plugin   │  │  │  │
│  │  │  │ Registry   │  │ Manager    │  │ System   │  │  │  │
│  │  │  └────────────┘  └────────────┘  └──────────┘  │  │  │
│  │  │  ┌────────────┐  ┌────────────┐  ┌──────────┐  │  │  │
│  │  │  │Permission  │  │ Event      │  │ Config   │  │  │  │
│  │  │  │Manager     │  │ Bus        │  │ Manager  │  │  │  │
│  │  │  └────────────┘  └────────────┘  └──────────┘  │  │  │
│  │  └──────────────────────┬──────────────────────────┘  │  │
│  │                         │                              │  │
│  │  ┌──────────────────────▼──────────────────────────┐  │  │
│  │  │              Bot Instances                       │  │  │
│  │  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌──────┐  │  │  │
│  │  │  │Context  │ │Schedule │ │Search   │ │...   │  │  │  │
│  │  │  │Assistant│ │Bot      │ │Bot      │ │      │  │  │  │
│  │  │  └─────────┘ └─────────┘ └─────────┘ └──────┘  │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  └───────────────────────────┬─────────────────────────────┘  │
│                              │                                │
│  ┌───────────────────────────▼─────────────────────────────┐  │
│  │              MCP Core Services                          │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐      │  │
│  │  │Archiver │ │Analytics│ │Semantic │ │Scheduler│      │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘      │  │
│  └─────────────────────────────────────────────────────────┘  │
│                              │                                │
│  ┌───────────────────────────▼─────────────────────────────┐  │
│  │              tdesktop Core (Data::Session)              │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐                   │  │
│  │  │Messages │ │Chats    │ │Users    │                   │  │
│  │  └─────────┘ └─────────┘ └─────────┘                   │  │
│  └─────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

**Bot Framework Layer**:
- Bot lifecycle management (load, start, stop, unload)
- Event routing and dispatch
- Permission enforcement
- Configuration management
- Plugin discovery and loading

**Bot Instances**:
- Individual bot implementations
- Event handlers
- Business logic
- State management

**MCP Core Services**:
- Shared services (analytics, search, archiver)
- Data access layer
- Audit logging

**tdesktop Core**:
- Telegram protocol implementation
- Message sending/receiving
- UI integration

---

## Core Components

### 1. BotBase (Abstract Base Class)

```cpp
// bot_base.h

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtCore/QVector>
#include "mcp/mcp_server.h"

namespace MCP {

// Forward declarations
class ChatArchiver;
class Analytics;
class SemanticSearch;
class MessageScheduler;
class AuditLogger;
class RBAC;

class BotBase : public QObject {
    Q_OBJECT

public:
    // Bot metadata
    struct BotInfo {
        QString id;              // Unique identifier (e.g., "context_assistant")
        QString name;            // Display name
        QString version;         // Semantic version
        QString description;     // Short description
        QString author;          // Author/organization
        QStringList tags;        // Categorization tags
        bool isPremium;          // Requires paid tier
    };

    // Constructor
    explicit BotBase(QObject *parent = nullptr);
    virtual ~BotBase();

    // Pure virtual methods (must be implemented)
    virtual BotInfo info() const = 0;
    virtual bool onInitialize() = 0;
    virtual void onShutdown() = 0;
    virtual void onMessage(const Message &msg) = 0;
    virtual void onCommand(const QString &cmd, const QJsonObject &args) = 0;

    // Optional virtual methods
    virtual void onMessageEdited(const Message &oldMsg, const Message &newMsg) {}
    virtual void onMessageDeleted(qint64 messageId, qint64 chatId) {}
    virtual void onChatJoined(qint64 chatId) {}
    virtual void onChatLeft(qint64 chatId) {}
    virtual void onUserStatusChanged(qint64 userId, const QString &status) {}

    // Configuration
    virtual QJsonObject defaultConfig() const { return QJsonObject(); }
    QJsonObject config() const { return _config; }
    void setConfig(const QJsonObject &config);

    // State management
    bool isRunning() const { return _isRunning; }
    bool isEnabled() const { return _isEnabled; }
    void setEnabled(bool enabled);

    // Permissions
    QStringList requiredPermissions() const { return _requiredPermissions; }
    bool hasPermission(const QString &permission) const;

protected:
    // Access to MCP services
    ChatArchiver *archiver() const { return _archiver; }
    Analytics *analytics() const { return _analytics; }
    SemanticSearch *semanticSearch() const { return _semanticSearch; }
    MessageScheduler *scheduler() const { return _scheduler; }
    AuditLogger *auditLogger() const { return _auditLogger; }
    RBAC *rbac() const { return _rbac; }

    // Utility methods
    void sendMessage(qint64 chatId, const QString &text);
    void editMessage(qint64 chatId, qint64 messageId, const QString &newText);
    void deleteMessage(qint64 chatId, qint64 messageId);
    Message getMessage(qint64 chatId, qint64 messageId);
    QVector<Message> getMessages(qint64 chatId, int limit);

    // Logging
    void logInfo(const QString &message);
    void logWarning(const QString &message);
    void logError(const QString &message);

    // Configuration storage
    void saveState(const QString &key, const QVariant &value);
    QVariant loadState(const QString &key, const QVariant &defaultValue = QVariant());

    // Required permissions
    void addRequiredPermission(const QString &permission);

Q_SIGNALS:
    void configChanged();
    void stateChanged();
    void errorOccurred(const QString &error);
    void messagePosted(qint64 chatId, const QString &text);

private:
    friend class BotManager;

    // Internal initialization
    bool internalInitialize(
        ChatArchiver *archiver,
        Analytics *analytics,
        SemanticSearch *semanticSearch,
        MessageScheduler *scheduler,
        AuditLogger *auditLogger,
        RBAC *rbac
    );

    // Services (injected by BotManager)
    ChatArchiver *_archiver = nullptr;
    Analytics *_analytics = nullptr;
    SemanticSearch *_semanticSearch = nullptr;
    MessageScheduler *_scheduler = nullptr;
    AuditLogger *_auditLogger = nullptr;
    RBAC *_rbac = nullptr;

    // State
    bool _isRunning = false;
    bool _isEnabled = true;
    QJsonObject _config;
    QStringList _requiredPermissions;
    QMap<QString, QVariant> _state;
};

} // namespace MCP
```

---

### 2. BotManager

```cpp
// bot_manager.h

#pragma once

#include "bot_base.h"
#include <QtCore/QHash>
#include <QtCore/QVector>
#include <QtSql/QSqlDatabase>

namespace MCP {

class BotManager : public QObject {
    Q_OBJECT

public:
    explicit BotManager(QObject *parent = nullptr);
    ~BotManager();

    // Initialization
    bool initialize(
        ChatArchiver *archiver,
        Analytics *analytics,
        SemanticSearch *semanticSearch,
        MessageScheduler *scheduler,
        AuditLogger *auditLogger,
        RBAC *rbac,
        QSqlDatabase *db
    );

    // Bot registration
    bool registerBot(BotBase *bot);
    bool unregisterBot(const QString &botId);

    // Bot lifecycle
    bool startBot(const QString &botId);
    bool stopBot(const QString &botId);
    bool restartBot(const QString &botId);

    // Bot queries
    QVector<BotBase*> allBots() const;
    QVector<BotBase*> runningBots() const;
    QVector<BotBase*> enabledBots() const;
    BotBase* getBot(const QString &botId) const;
    bool hasBot(const QString &botId) const;

    // Event dispatch
    void dispatchMessage(const Message &msg);
    void dispatchCommand(const QString &botId, const QString &cmd,
                        const QJsonObject &args);
    void dispatchMessageEdited(const Message &oldMsg, const Message &newMsg);
    void dispatchMessageDeleted(qint64 messageId, qint64 chatId);

    // Configuration
    void setBotConfig(const QString &botId, const QJsonObject &config);
    QJsonObject getBotConfig(const QString &botId) const;
    void saveAllConfigs();
    void loadAllConfigs();

    // Statistics
    struct BotStats {
        int totalBots;
        int runningBots;
        int messagesProcessed;
        int commandsExecuted;
        QMap<QString, int> botMessageCounts;
    };
    BotStats getStats() const;

Q_SIGNALS:
    void botRegistered(const QString &botId);
    void botUnregistered(const QString &botId);
    void botStarted(const QString &botId);
    void botStopped(const QString &botId);
    void botError(const QString &botId, const QString &error);

private:
    // Bot storage
    QHash<QString, BotBase*> _bots;
    QVector<BotBase*> _botList;

    // Services
    ChatArchiver *_archiver = nullptr;
    Analytics *_analytics = nullptr;
    SemanticSearch *_semanticSearch = nullptr;
    MessageScheduler *_scheduler = nullptr;
    AuditLogger *_auditLogger = nullptr;
    RBAC *_rbac = nullptr;
    QSqlDatabase *_db = nullptr;

    // Statistics
    BotStats _stats;
};

} // namespace MCP
```

---

### 3. Permission System

```cpp
// bot_permissions.h

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace MCP {

// Permission categories
enum class PermissionCategory {
    READ,           // Read data
    WRITE,          // Modify data
    DELETE,         // Delete data
    SEND,           // Send messages
    ADMIN,          // Administrative actions
    PRIVACY         // Access to sensitive data
};

// Standard permissions
namespace Permissions {
    // Read permissions
    const QString READ_MESSAGES = "read:messages";
    const QString READ_CHATS = "read:chats";
    const QString READ_USERS = "read:users";
    const QString READ_HISTORY = "read:history";
    const QString READ_ANALYTICS = "read:analytics";
    const QString READ_EPHEMERAL = "read:ephemeral";

    // Write permissions
    const QString SEND_MESSAGES = "send:messages";
    const QString EDIT_MESSAGES = "edit:messages";
    const QString DELETE_MESSAGES = "delete:messages";
    const QString PIN_MESSAGES = "pin:messages";
    const QString FORWARD_MESSAGES = "forward:messages";
    const QString ADD_REACTIONS = "add:reactions";

    // Administrative
    const QString MANAGE_CHATS = "admin:chats";
    const QString MANAGE_USERS = "admin:users";
    const QString MANAGE_BOTS = "admin:bots";
    const QString ACCESS_AUDIT_LOG = "admin:audit_log";

    // Privacy-sensitive
    const QString CAPTURE_EPHEMERAL = "privacy:capture_ephemeral";
    const QString EXPORT_DATA = "privacy:export_data";
    const QString EXTERNAL_API = "privacy:external_api";

    // All permissions
    QStringList all();
}

// Permission manager
class PermissionManager : public QObject {
    Q_OBJECT

public:
    explicit PermissionManager(RBAC *rbac, QObject *parent = nullptr);

    // Check permissions
    bool hasPermission(const QString &botId, const QString &permission);
    bool hasAllPermissions(const QString &botId, const QStringList &permissions);
    bool hasAnyPermission(const QString &botId, const QStringList &permissions);

    // Grant/revoke
    void grantPermission(const QString &botId, const QString &permission);
    void revokePermission(const QString &botId, const QString &permission);
    void grantPermissions(const QString &botId, const QStringList &permissions);
    void revokePermissions(const QString &botId, const QStringList &permissions);

    // Query
    QStringList getPermissions(const QString &botId);
    QStringList getMissingPermissions(const QString &botId,
                                      const QStringList &required);

    // Consent
    bool requestUserConsent(const QString &botId, const QStringList &permissions);

private:
    RBAC *_rbac;
    QMap<QString, QSet<QString>> _botPermissions;
};

} // namespace MCP
```

---

### 4. Event System

```cpp
// bot_events.h

#pragma once

#include <QtCore/QObject>
#include <QtCore/QVariant>

namespace MCP {

// Event types
enum class BotEventType {
    MESSAGE_RECEIVED,
    MESSAGE_EDITED,
    MESSAGE_DELETED,
    CHAT_JOINED,
    CHAT_LEFT,
    USER_STATUS_CHANGED,
    COMMAND_RECEIVED,
    TIMER_FIRED,
    CUSTOM
};

// Base event class
struct BotEvent {
    BotEventType type;
    QDateTime timestamp;
    QVariantMap data;

    BotEvent(BotEventType t) : type(t), timestamp(QDateTime::currentDateTime()) {}
};

// Event bus for bot communication
class EventBus : public QObject {
    Q_OBJECT

public:
    static EventBus* instance();

    // Subscribe/unsubscribe
    void subscribe(BotBase *bot, BotEventType eventType);
    void unsubscribe(BotBase *bot, BotEventType eventType);
    void unsubscribeAll(BotBase *bot);

    // Publish events
    void publish(const BotEvent &event);
    void publishAsync(const BotEvent &event);

Q_SIGNALS:
    void eventPublished(const BotEvent &event);

private:
    explicit EventBus(QObject *parent = nullptr);
    static EventBus *_instance;

    QMultiMap<BotEventType, BotBase*> _subscribers;
};

} // namespace MCP
```

---

### 5. Plugin System

```cpp
// bot_plugin.h

#pragma once

#include "bot_base.h"
#include <QtCore/QPluginLoader>
#include <QtCore/QDir>

namespace MCP {

// Plugin interface
class BotPlugin {
public:
    virtual ~BotPlugin() = default;
    virtual BotBase* createBot() = 0;
    virtual QString pluginVersion() const = 0;
    virtual QString minimumFrameworkVersion() const = 0;
};

#define BotPlugin_iid "com.telegram.mcp.BotPlugin/1.0"
Q_DECLARE_INTERFACE(BotPlugin, BotPlugin_iid)

// Plugin loader
class PluginLoader : public QObject {
    Q_OBJECT

public:
    explicit PluginLoader(BotManager *manager, QObject *parent = nullptr);

    // Load plugins from directory
    int loadPlugins(const QString &pluginDir);
    int loadPlugin(const QString &pluginPath);

    // Unload
    void unloadPlugin(const QString &pluginId);
    void unloadAllPlugins();

    // Query
    QStringList loadedPlugins() const;
    BotPlugin* getPlugin(const QString &pluginId) const;

Q_SIGNALS:
    void pluginLoaded(const QString &pluginId);
    void pluginUnloaded(const QString &pluginId);
    void pluginError(const QString &pluginId, const QString &error);

private:
    BotManager *_manager;
    QHash<QString, QPluginLoader*> _loaders;
    QHash<QString, BotPlugin*> _plugins;
};

} // namespace MCP
```

---

## Bot Lifecycle

### State Machine

```
┌─────────┐
│ Created │ (Bot object instantiated)
└────┬────┘
     │ registerBot()
     ▼
┌─────────┐
│Registered│ (Added to BotManager)
└────┬────┘
     │ startBot()
     ▼
┌─────────┐
│Initializing│ (onInitialize() called)
└────┬────┘
     │ (success)
     ▼
┌─────────┐
│ Running │ ◄──┐ (Processing events)
└────┬────┘    │
     │         │ restartBot()
     │ stopBot()│
     ▼         │
┌─────────┐   │
│ Stopping│───┘
└────┬────┘
     │ (onShutdown() called)
     ▼
┌─────────┐
│ Stopped │
└────┬────┘
     │ unregisterBot()
     ▼
┌─────────┐
│ Removed │
└─────────┘
```

### Lifecycle Methods

```cpp
// Example bot implementation
class MyBot : public BotBase {
public:
    BotInfo info() const override {
        return {
            "my_bot",
            "My Bot",
            "1.0.0",
            "Does cool things",
            "Your Name",
            {"productivity", "automation"},
            false  // Not premium
        };
    }

    bool onInitialize() override {
        // Load configuration
        _config = config();

        // Initialize bot state
        _messageCount = loadState("message_count", 0).toInt();

        // Subscribe to events
        EventBus::instance()->subscribe(this, BotEventType::MESSAGE_RECEIVED);

        // Register required permissions
        addRequiredPermission(Permissions::READ_MESSAGES);
        addRequiredPermission(Permissions::SEND_MESSAGES);

        // Connect to database
        if (!_db.open()) {
            logError("Failed to open database");
            return false;
        }

        logInfo("Bot initialized successfully");
        return true;
    }

    void onShutdown() override {
        // Save state
        saveState("message_count", _messageCount);

        // Cleanup resources
        EventBus::instance()->unsubscribeAll(this);
        _db.close();

        logInfo("Bot shut down");
    }

    void onMessage(const Message &msg) override {
        // Process message
        _messageCount++;

        if (msg.text.contains("help")) {
            sendMessage(msg.chatId, "Here's how to use this bot...");
        }
    }

    void onCommand(const QString &cmd, const QJsonObject &args) override {
        if (cmd == "stats") {
            QString stats = QString("Processed %1 messages").arg(_messageCount);
            // Return stats
        }
    }

private:
    int _messageCount = 0;
    QSqlDatabase _db;
};
```

---

## Database Schema

### Bot Framework Tables

```sql
-- Bot registry
CREATE TABLE bots (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    version TEXT NOT NULL,
    description TEXT,
    author TEXT,
    tags TEXT,               -- JSON array
    is_premium BOOLEAN DEFAULT 0,
    is_enabled BOOLEAN DEFAULT 1,
    is_running BOOLEAN DEFAULT 0,
    config JSON,
    created_at INTEGER,
    last_started INTEGER,
    UNIQUE(id)
);

-- Bot permissions
CREATE TABLE bot_permissions (
    bot_id TEXT NOT NULL,
    permission TEXT NOT NULL,
    granted_at INTEGER,
    granted_by TEXT,
    PRIMARY KEY(bot_id, permission),
    FOREIGN KEY(bot_id) REFERENCES bots(id) ON DELETE CASCADE
);

-- Bot state (key-value store)
CREATE TABLE bot_state (
    bot_id TEXT NOT NULL,
    key TEXT NOT NULL,
    value TEXT,              -- JSON
    updated_at INTEGER,
    PRIMARY KEY(bot_id, key),
    FOREIGN KEY(bot_id) REFERENCES bots(id) ON DELETE CASCADE
);

-- Bot execution log
CREATE TABLE bot_execution_log (
    id INTEGER PRIMARY KEY,
    bot_id TEXT NOT NULL,
    event_type TEXT NOT NULL,
    event_data JSON,
    started_at INTEGER,
    completed_at INTEGER,
    status TEXT,             -- 'success', 'error', 'timeout'
    error_message TEXT,
    FOREIGN KEY(bot_id) REFERENCES bots(id)
);

-- Bot metrics
CREATE TABLE bot_metrics (
    bot_id TEXT NOT NULL,
    metric_name TEXT NOT NULL,
    metric_value REAL,
    recorded_at INTEGER,
    PRIMARY KEY(bot_id, metric_name, recorded_at),
    FOREIGN KEY(bot_id) REFERENCES bots(id)
);

CREATE INDEX idx_bot_log_bot ON bot_execution_log(bot_id, started_at);
CREATE INDEX idx_bot_metrics_bot ON bot_metrics(bot_id, metric_name);
```

---

## Security Architecture

### Sandboxing

```cpp
// Bot sandbox to limit resource usage
class BotSandbox {
public:
    struct Limits {
        int maxMemoryMB = 100;
        int maxCPUPercent = 50;
        int maxDatabaseQueries = 1000;  // per minute
        int maxAPIcalls = 100;          // per minute
        int maxMessagesPerMinute = 10;
    };

    void enforce(BotBase *bot, const Limits &limits);
    bool isViolating(BotBase *bot) const;
    void terminate(BotBase *bot, const QString &reason);
};
```

### Permission Enforcement

```cpp
// Decorator pattern for permission checks
template<typename Func>
auto requirePermission(const QString &permission, Func func) {
    return [=](auto&&... args) {
        if (!hasPermission(permission)) {
            throw PermissionDeniedException(permission);
        }
        return func(std::forward<decltype(args)>(args)...);
    };
}

// Usage
void BotBase::sendMessage(qint64 chatId, const QString &text) {
    auto send = requirePermission(Permissions::SEND_MESSAGES, [&]() {
        // Actual send logic
    });
    send();
}
```

---

## Performance Optimization

### Event Queue

```cpp
// Async event processing with worker threads
class EventQueue : public QObject {
    Q_OBJECT

public:
    void enqueue(const BotEvent &event);
    void process();  // Run in worker thread

private:
    QQueue<BotEvent> _queue;
    QMutex _mutex;
    QThreadPool _threadPool;
};
```

### Caching

```cpp
// Cache frequently accessed data
class BotCache {
public:
    template<typename T>
    T get(const QString &key, std::function<T()> fetcher) {
        if (_cache.contains(key)) {
            return qvariant_cast<T>(_cache.value(key));
        }

        T value = fetcher();
        _cache.insert(key, QVariant::fromValue(value));
        return value;
    }

private:
    QCache<QString, QVariant> _cache;
};
```

---

## Testing Strategy

### Unit Tests

```cpp
class BotBaseTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testInitialization();
    void testMessageHandling();
    void testPermissions();
    void testStateManagement();
};

void BotBaseTest::testInitialization() {
    TestBot bot;
    QVERIFY(bot.onInitialize());
    QVERIFY(bot.isRunning());
}
```

### Integration Tests

```cpp
class BotIntegrationTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testBotManagerRegistration();
    void testEventDispatch();
    void testPermissionEnforcement();
};
```

---

## Deployment

### Plugin Directory Structure

```
~/.telegram-mcp/
├── bots/
│   ├── context_assistant/
│   │   ├── libcontext_assistant.so
│   │   ├── config.json
│   │   └── README.md
│   ├── scheduler_bot/
│   │   ├── libscheduler_bot.so
│   │   ├── config.json
│   │   └── README.md
│   └── ...
├── config/
│   ├── bot_manager.json
│   └── permissions.json
├── data/
│   └── bots.db
└── logs/
    └── bots.log
```

### Configuration File Format

```json
{
  "bot_framework": {
    "version": "1.0.0",
    "plugin_dir": "~/.telegram-mcp/bots",
    "auto_load": true,
    "max_concurrent_bots": 50
  },
  "bots": {
    "context_assistant": {
      "enabled": true,
      "config": {
        "proactive_mode": true,
        "confidence_threshold": 0.7
      }
    },
    "scheduler_bot": {
      "enabled": true,
      "config": {
        "default_timezone": "America/New_York"
      }
    }
  }
}
```

---

**Last Updated**: 2025-11-16
**Next Review**: Monthly
**Maintained By**: Engineering Team

