# Telegram Bot Enhancement Use Cases
## Comprehensive Specification

> **Version**: 1.0.0
> **Date**: 2025-11-16
> **Status**: Production Specification

---

## Table of Contents
- [Overview](#overview)
- [Use Case Catalog](#use-case-catalog)
- [Technical Specifications](#technical-specifications)
- [User Workflows](#user-workflows)
- [Integration Points](#integration-points)

---

## Overview

This document specifies all bot enhancement use cases enabled by our custom Telegram Desktop client with MCP integration. Each use case leverages capabilities unavailable to standard Telegram bots.

### Core Advantages
- **Full message history access** (not just bot mentions)
- **Local processing** (privacy-preserving)
- **Semantic search** (AI-powered understanding)
- **Cross-chat coordination** (multi-conversation awareness)
- **Direct tdesktop integration** (bypass Bot API limitations)

---

## Use Case Catalog

### UC-001: Context-Aware Personal AI Assistant

**Priority**: HIGH
**Complexity**: HIGH
**Market Demand**: VERY HIGH

#### Description
An AI assistant that understands full conversation context across all chats, proactively offers help, and learns from your communication patterns.

#### User Stories

**US-001.1: Proactive Help Offering**
```
As a user
When I'm struggling with a technical problem in a chat
Then the bot should detect my struggle and offer relevant help
Without me explicitly asking
```

**US-001.2: Cross-Chat Context**
```
As a user
When I ask "What did we decide about the API?"
Then the bot should search all relevant chats
And provide a consolidated answer with sources
```

**US-001.3: Learning Preferences**
```
As a user
When I repeatedly dismiss certain suggestions
Then the bot should learn my preferences
And stop making similar suggestions
```

#### Functional Requirements

**FR-001.1: Semantic Understanding**
- Bot must analyze message intent using NLP
- Classify questions, requests, statements, commands
- Extract entities (people, dates, topics, decisions)
- Build knowledge graph from conversations

**FR-001.2: Context Window**
- Maintain rolling context window (configurable, default 50 messages)
- Include messages from last N hours (configurable, default 24)
- Weight recent messages higher in relevance scoring
- Support manual context expansion ("include messages from last week")

**FR-001.3: Proactive Triggers**
- Keyword detection: "help", "how do I", "can someone", "stuck", "error"
- Sentiment analysis: detect frustration, confusion
- Pattern recognition: repeated questions, escalating urgency
- Time-based: "you've been discussing this for 30 minutes"

**FR-001.4: Response Generation**
- Query semantic search for relevant past conversations
- Retrieve documentation/links shared previously on topic
- Generate response with sources cited
- Include confidence score
- Offer to search external knowledge bases if local search fails

#### Technical Specifications

**Data Flow**:
```
Incoming Message → Intent Classification → Context Retrieval →
Relevance Scoring → Decision (respond/ignore) → Response Generation →
Send Message → Log Interaction → Update Learning Model
```

**API Integration**:
```cpp
class ContextAwareAssistant : public MCPBot {
public:
    struct Context {
        QVector<Message> recentMessages;  // Last N messages
        QVector<Entity> extractedEntities; // People, topics, etc.
        QMap<QString, float> topicScores;  // Topic relevance
        float sentimentScore;               // -1.0 to 1.0
    };

    void onMessage(const Message &msg) override {
        auto intent = classifyIntent(msg.text);
        auto context = buildContext(msg.chatId, 50);

        if (shouldRespond(intent, context)) {
            auto response = generateResponse(msg, context);
            sendWithConfidence(msg.chatId, response);
        }
    }

private:
    Context buildContext(qint64 chatId, int messageCount);
    bool shouldRespond(MessageIntent intent, const Context &ctx);
    Response generateResponse(const Message &msg, const Context &ctx);
};
```

**Database Schema**:
```sql
CREATE TABLE assistant_context (
    id INTEGER PRIMARY KEY,
    chat_id INTEGER NOT NULL,
    message_id INTEGER NOT NULL,
    intent TEXT NOT NULL,
    entities JSON,
    relevance_score REAL,
    context_window_start INTEGER,
    context_window_end INTEGER,
    created_at INTEGER
);

CREATE TABLE assistant_interactions (
    id INTEGER PRIMARY KEY,
    user_id INTEGER NOT NULL,
    chat_id INTEGER NOT NULL,
    trigger_type TEXT,          -- 'explicit', 'proactive', 'suggestion'
    user_query TEXT,
    bot_response TEXT,
    confidence_score REAL,
    user_feedback TEXT,         -- 'helpful', 'not_helpful', 'ignored'
    created_at INTEGER
);

CREATE TABLE assistant_preferences (
    user_id INTEGER PRIMARY KEY,
    proactive_help_enabled BOOLEAN DEFAULT 1,
    min_confidence_threshold REAL DEFAULT 0.7,
    topic_preferences JSON,     -- {'coding': 'always', 'sports': 'never'}
    response_style TEXT,        -- 'concise', 'detailed', 'technical'
    last_updated INTEGER
);
```

**Configuration**:
```json
{
  "assistant": {
    "enabled": true,
    "proactive_mode": true,
    "context_window_messages": 50,
    "context_window_hours": 24,
    "min_confidence": 0.7,
    "trigger_keywords": ["help", "how", "what", "why", "stuck"],
    "sentiment_threshold": -0.5,
    "response_delay_seconds": 2,
    "max_responses_per_hour": 10,
    "learning_enabled": true
  }
}
```

#### User Interface

**Configuration Panel**:
```
┌─ Context-Aware Assistant Settings ────────────────────┐
│                                                        │
│  [✓] Enable AI Assistant                              │
│  [✓] Proactive help (offer suggestions without asking)│
│                                                        │
│  Context Window:                                       │
│    Messages: [50        ] (10-200)                     │
│    Hours:    [24        ] (1-168)                      │
│                                                        │
│  Response Behavior:                                    │
│    Confidence threshold: [0.7      ] (0.0-1.0)         │
│    Style: [Detailed ▼] (Concise/Detailed/Technical)   │
│    Max responses/hour: [10       ]                     │
│                                                        │
│  Topics:                                               │
│    [ View Topic Preferences... ]                       │
│                                                        │
│  Privacy:                                              │
│    [✓] Process locally (no external API calls)        │
│    [ ] Allow cloud AI for better responses            │
│                                                        │
│  [ Save ]  [ Cancel ]  [ Reset to Defaults ]          │
└────────────────────────────────────────────────────────┘
```

**In-Chat Interaction**:
```
You: I'm getting a CORS error when calling the API
You: The preflight request keeps failing
You: Anyone know how to fix this?

[10:30 AM] 💡 AI Assistant
I noticed you're having CORS issues. Based on our conversation
2 weeks ago in the #backend channel, you solved a similar issue
by adding these headers to the server config:

Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE
Access-Control-Allow-Headers: Content-Type, Authorization

Would you like me to search for the full solution thread?

[✓ Helpful] [✗ Not Helpful] [⚙️ Settings]
```

#### Success Metrics

**Adoption Metrics**:
- 70% of users enable the assistant within first week
- 50% enable proactive mode
- Average 10 interactions per user per day

**Quality Metrics**:
- 80% of proactive suggestions rated "helpful"
- Average confidence score > 0.75
- < 5% false positives (unwanted suggestions)

**Business Metrics**:
- 30% reduction in "how do I" questions in support chats
- 25% faster problem resolution time
- 60% user satisfaction score

#### Privacy & Security

**Data Handling**:
- All processing happens locally by default
- Context never leaves user's device
- Optional cloud AI requires explicit consent
- User can delete all assistant data anytime

**Permissions Required**:
- Read message history: ALL_CHATS or PER_CHAT
- Send messages: USER_AS_SENDER
- Access semantic search: LOCAL_PROCESSING
- External API access: CLOUD_AI (optional)

#### Pricing Tier
- **Free**: Basic assistant (local processing, 10 responses/day)
- **Pro ($4.99/month)**: Unlimited responses, advanced learning
- **Enterprise ($49/month)**: Multi-user coordination, custom training

---

### UC-002: Smart Message Queue & Scheduling

**Priority**: HIGH
**Complexity**: MEDIUM
**Market Demand**: HIGH

#### Description
Intelligent message scheduling based on recipient activity patterns, timezone awareness, and context optimization.

#### User Stories

**US-002.1: Activity-Based Scheduling**
```
As a user
When I compose a message at 2 AM
Then the bot should suggest sending it during recipient's active hours
To maximize response likelihood
```

**US-002.2: Timezone Intelligence**
```
As a user
When I send a birthday message to someone in a different timezone
Then the bot should schedule it for midnight in their timezone
Without me calculating the time difference
```

**US-002.3: Natural Language Scheduling**
```
As a user
When I type "Remind me to ask John about the report tomorrow morning"
Then the bot should extract the intent, recipient, message, and timing
And schedule it automatically
```

#### Functional Requirements

**FR-002.1: Activity Pattern Analysis**
- Analyze recipient's activity over last 30 days
- Identify peak activity hours (hourly histogram)
- Detect timezone from activity patterns
- Calculate optimal send times (highest response probability)
- Update patterns weekly

**FR-002.2: Schedule Types**
- **Immediate**: Send right now
- **Delayed**: Send in X minutes/hours
- **Optimal**: Send at recipient's peak activity time
- **Specific**: Send at exact date/time
- **Recurring**: Send daily/weekly/monthly

**FR-002.3: Natural Language Processing**
```
Input: "Remind me to ask Alice about the deadline tomorrow at 9 AM"
Parsed:
  - Action: send_message
  - Recipient: Alice
  - Content: "Quick reminder: Can you update me on the deadline?"
  - Time: Tomorrow 09:00 in user's timezone
```

**FR-002.4: Queue Management**
- View all scheduled messages
- Edit scheduled messages before sending
- Cancel scheduled messages
- Pause/resume queue
- Priority ordering

#### Technical Specifications

**Activity Analysis Algorithm**:
```cpp
struct ActivityPattern {
    qint64 userId;
    QVector<int> hourlyActivity;    // 24-hour histogram
    QString timezone;                // Detected timezone
    QSet<int> activeDays;           // Days of week (0-6)
    int peakHour;                   // Most active hour
    float avgResponseTime;          // Minutes
};

ActivityPattern analyzeUserActivity(qint64 userId) {
    auto messages = _archiver->getUserMessages(userId, 30 * 24 * 3600);

    QVector<int> hourly(24, 0);
    for (const auto &msg : messages) {
        QDateTime time = QDateTime::fromSecsSinceEpoch(msg.timestamp);
        hourly[time.time().hour()]++;
    }

    int peakHour = std::max_element(hourly.begin(), hourly.end())
                   - hourly.begin();

    QString tz = detectTimezone(messages);

    return {userId, hourly, tz, activeDays, peakHour, avgResponseTime};
}
```

**Scheduling Engine**:
```cpp
class MessageScheduler : public QObject {
    Q_OBJECT

public:
    struct ScheduledMessage {
        qint64 id;
        qint64 chatId;
        QString text;
        QDateTime scheduledFor;
        ScheduleType type;
        RecurrencePattern recurrence;
        int priority;           // 1-5
        QString metadata;       // JSON
    };

    qint64 scheduleOptimal(qint64 chatId, const QString &text,
                           qint64 recipientId) {
        auto pattern = analyzeUserActivity(recipientId);
        QDateTime optimalTime = calculateOptimalTime(pattern);
        return scheduleOnce(chatId, text, optimalTime);
    }

    qint64 scheduleNatural(const QString &naturalLanguage) {
        auto parsed = parseNaturalLanguage(naturalLanguage);
        return schedule(parsed.chatId, parsed.text,
                       parsed.scheduledFor, parsed.type);
    }

Q_SIGNALS:
    void messageSent(qint64 scheduleId, bool success);
    void scheduleCreated(qint64 scheduleId);
    void scheduleCancelled(qint64 scheduleId);
};
```

**Database Schema**:
```sql
CREATE TABLE message_queue (
    id INTEGER PRIMARY KEY,
    chat_id INTEGER NOT NULL,
    recipient_id INTEGER,      -- For optimal timing analysis
    message_text TEXT NOT NULL,
    scheduled_for INTEGER NOT NULL,
    schedule_type TEXT NOT NULL,  -- 'immediate', 'delayed', 'optimal', 'specific'
    recurrence_pattern TEXT,      -- 'once', 'daily', 'weekly', 'monthly'
    recurrence_end INTEGER,
    priority INTEGER DEFAULT 3,
    status TEXT DEFAULT 'pending',  -- 'pending', 'sent', 'cancelled', 'failed'
    created_at INTEGER NOT NULL,
    sent_at INTEGER,
    failure_reason TEXT,
    metadata JSON
);

CREATE TABLE recipient_activity_patterns (
    user_id INTEGER PRIMARY KEY,
    hourly_activity TEXT,       -- JSON array of 24 integers
    timezone TEXT,
    active_days TEXT,           -- JSON array
    peak_hour INTEGER,
    avg_response_time_minutes REAL,
    last_analyzed INTEGER,
    message_count INTEGER
);

CREATE INDEX idx_queue_scheduled ON message_queue(scheduled_for, status);
CREATE INDEX idx_queue_chat ON message_queue(chat_id, status);
```

**Natural Language Parser**:
```cpp
struct ParsedSchedule {
    qint64 chatId;
    QString recipientName;
    QString text;
    QDateTime scheduledFor;
    ScheduleType type;
};

ParsedSchedule parseNaturalLanguage(const QString &input) {
    // Extract time expressions
    QRegularExpression timeRegex(
        R"((tomorrow|today|tonight|next week)\s*(at\s*(\d{1,2}):?(\d{2})?\s*(am|pm)?)?)"
    );

    // Extract recipient mentions
    QRegularExpression recipientRegex(R"(@(\w+)|to\s+(\w+))");

    // Extract action keywords
    QRegularExpression actionRegex(
        R"((remind|tell|ask|send|message)\s+(.+?)\s+(about|to|that))"
    );

    // Parse and construct schedule
    ParsedSchedule result;
    // ... parsing logic ...
    return result;
}
```

#### User Interface

**Compose Window Enhancement**:
```
┌─ Send to: @alice ─────────────────────────────────────┐
│                                                        │
│  Hey Alice, can you send me the Q4 report?            │
│                                                        │
│  ──────────────────────────────────────────────────   │
│                                                        │
│  Send Options:                                         │
│  ○ Send now                                            │
│  ● Send at optimal time (Today 6:30 PM - Alice's      │
│    peak activity time)                                 │
│  ○ Schedule for specific time...                      │
│                                                        │
│  💡 Alice is typically most active at 6-7 PM          │
│  📊 Average response time: 23 minutes                  │
│                                                        │
│  [ Send ]  [ Schedule ]  [ Cancel ]                    │
└────────────────────────────────────────────────────────┘
```

**Queue Manager**:
```
┌─ Scheduled Messages ───────────────────────────────────┐
│                                                        │
│  📅 Today                                              │
│  ├─ 6:30 PM → @alice "Q4 report request"              │
│  │   [Edit] [Cancel] [Send Now]                       │
│  └─ 8:00 PM → #team "Meeting reminder"                │
│      [Edit] [Cancel] [Send Now]                       │
│                                                        │
│  📅 Tomorrow                                           │
│  └─ 9:00 AM → @john "Deadline check-in"               │
│      [Edit] [Cancel] [Send Now]                       │
│                                                        │
│  🔁 Recurring                                          │
│  └─ Every Monday 10 AM → #standup "Weekly reminder"   │
│      [Edit] [Pause] [Delete]                          │
│                                                        │
│  [ + New Schedule ]  [ Clear All ]  [ Export ]        │
└────────────────────────────────────────────────────────┘
```

**Natural Language Interface**:
```
You: /schedule Remind me to ask John about deployment tomorrow at 9 AM

Bot: ✅ Scheduled
     To: @john
     Message: "Hi John, quick reminder - can you update me on the
              deployment status?"
     When: Tomorrow (Nov 17) at 9:00 AM

     [View in Queue] [Edit] [Cancel]
```

#### Success Metrics

**Adoption**:
- 60% of users try scheduling feature in first month
- 40% use it weekly
- Average 5 scheduled messages per user per week

**Effectiveness**:
- 85% of optimally-scheduled messages get responses
- 30% faster average response time vs. immediate send
- 90% of scheduled messages sent successfully

**Business**:
- Premium feature conversion rate: 25%
- User retention increase: 15%
- NPS score: +45

#### Privacy & Security

**Data Minimization**:
- Activity patterns stored locally only
- No cloud sync of scheduled messages
- User can clear all pattern data

**Permissions**:
- Read message timestamps: METADATA_ONLY
- Send scheduled messages: USER_AS_SENDER
- Access activity patterns: LOCAL_ANALYTICS

#### Pricing Tier
- **Free**: 3 scheduled messages at a time, manual timing only
- **Pro ($4.99/month)**: Unlimited schedules, optimal timing
- **Enterprise ($49/month)**: Team coordination, analytics dashboard

---

### UC-003: Advanced Search & Knowledge Management

**Priority**: VERY HIGH
**Complexity**: HIGH
**Market Demand**: VERY HIGH

#### Description
AI-powered search that understands context, builds knowledge graphs, and extracts actionable information from conversations.

#### User Stories

**US-003.1: Semantic Search**
```
As a user
When I search for "API redesign discussion"
Then the bot should find relevant conversations
Even if they used different words like "API refactoring" or "endpoint restructure"
```

**US-003.2: Knowledge Extraction**
```
As a user
When I ask "What decisions were made about the deployment?"
Then the bot should extract all decision points
With who made them, when, and what was decided
```

**US-003.3: Topic Clustering**
```
As a user
When I view a long-running chat
Then the bot should show me main discussion topics
Grouped by theme and time period
```

#### Functional Requirements

**FR-003.1: Search Capabilities**
- **Exact match**: Traditional keyword search
- **Fuzzy match**: Handle typos, variations
- **Semantic match**: Understand meaning, synonyms
- **Regex search**: Advanced patterns
- **Multi-field**: Search across text, media captions, links
- **Combined**: Boolean operators (AND, OR, NOT)

**FR-003.2: Knowledge Graph**
```
Entities:
  - People: @alice, @bob
  - Topics: "API redesign", "deployment"
  - Decisions: "Use GraphQL", "Deploy on Friday"
  - Dates: "Nov 15", "next week"
  - Links: URLs shared in context
  - Files: Documents, images referenced

Relationships:
  - @alice PROPOSED "Use GraphQL" ON "Nov 15"
  - "API redesign" RELATES_TO "deployment"
  - "GraphQL" IS_ALTERNATIVE_TO "REST"
```

**FR-003.3: Result Ranking**
```cpp
float calculateRelevance(const SearchResult &result, const Query &query) {
    float score = 0.0f;

    // Semantic similarity (0.0 - 1.0)
    score += semanticSimilarity(result.content, query.text) * 0.4f;

    // Recency (newer = higher)
    int daysOld = (now() - result.timestamp) / 86400;
    score += (1.0f / (1.0f + daysOld * 0.1f)) * 0.2f;

    // Chat importance (weighted by activity)
    score += getChatWeight(result.chatId) * 0.2f;

    // User importance (weighted by interaction frequency)
    score += getUserWeight(result.userId) * 0.1f;

    // Exact keyword matches boost
    if (containsExactKeyword(result.content, query.keywords)) {
        score += 0.1f;
    }

    return std::min(score, 1.0f);
}
```

**FR-003.4: Export Formats**
- **JSON**: Structured data with metadata
- **Markdown**: Human-readable with links
- **PDF**: Report-style with charts
- **CSV**: Spreadsheet-compatible

#### Technical Specifications

**Search Architecture**:
```cpp
class KnowledgeManager : public QObject {
    Q_OBJECT

public:
    struct SearchQuery {
        QString text;                    // Search text
        QVector<QString> keywords;       // Extracted keywords
        QVector<qint64> chatIds;        // Limit to specific chats
        QVector<qint64> userIds;        // Limit to specific users
        QDateTime startDate;
        QDateTime endDate;
        SearchType type;                 // EXACT, FUZZY, SEMANTIC
        int limit;                       // Max results
        float minRelevance;             // Minimum score
    };

    struct SearchResult {
        qint64 messageId;
        qint64 chatId;
        qint64 userId;
        QString content;
        QDateTime timestamp;
        float relevanceScore;
        QVector<Entity> entities;        // Extracted entities
        QString snippet;                 // Highlighted excerpt
        QVector<qint64> relatedMessages; // Context messages
    };

    QVector<SearchResult> search(const SearchQuery &query);

    // Knowledge extraction
    KnowledgeGraph buildKnowledgeGraph(qint64 chatId,
                                        const QDateTime &since);
    QVector<Decision> extractDecisions(qint64 chatId);
    QVector<Topic> clusterTopics(qint64 chatId, int numTopics);

Q_SIGNALS:
    void searchCompleted(const QVector<SearchResult> &results);
    void knowledgeGraphUpdated(qint64 chatId);
};
```

**Knowledge Graph Implementation**:
```cpp
struct KnowledgeGraph {
    struct Node {
        QString id;              // Unique identifier
        QString type;            // "person", "topic", "decision", "date"
        QString label;           // Display name
        QJsonObject properties;  // Additional data
    };

    struct Edge {
        QString fromId;
        QString toId;
        QString relationship;    // "proposed", "relates_to", "decided"
        float weight;            // Strength of relationship
        QDateTime created;
    };

    QVector<Node> nodes;
    QVector<Edge> edges;

    // Graph algorithms
    QVector<Node> findRelated(const QString &nodeId, int depth = 2);
    QVector<QString> findPath(const QString &from, const QString &to);
    float calculateCentrality(const QString &nodeId);
};
```

**Database Schema**:
```sql
CREATE TABLE knowledge_entities (
    id TEXT PRIMARY KEY,        -- UUID
    chat_id INTEGER NOT NULL,
    entity_type TEXT NOT NULL,  -- 'person', 'topic', 'decision', 'date', 'url'
    label TEXT NOT NULL,
    properties JSON,
    created_at INTEGER,
    last_updated INTEGER
);

CREATE TABLE knowledge_relationships (
    id INTEGER PRIMARY KEY,
    from_entity TEXT NOT NULL,
    to_entity TEXT NOT NULL,
    relationship_type TEXT NOT NULL,  -- 'proposed', 'approved', 'relates_to'
    weight REAL DEFAULT 1.0,
    message_id INTEGER,      -- Source message
    created_at INTEGER,
    FOREIGN KEY(from_entity) REFERENCES knowledge_entities(id),
    FOREIGN KEY(to_entity) REFERENCES knowledge_entities(id)
);

CREATE TABLE search_cache (
    query_hash TEXT PRIMARY KEY,
    query_text TEXT NOT NULL,
    results JSON NOT NULL,
    created_at INTEGER,
    expires_at INTEGER
);

CREATE INDEX idx_entities_chat ON knowledge_entities(chat_id, entity_type);
CREATE INDEX idx_relationships_from ON knowledge_relationships(from_entity);
CREATE INDEX idx_relationships_to ON knowledge_relationships(to_entity);
```

**Decision Extraction Algorithm**:
```cpp
struct Decision {
    QString decision;        // "Use GraphQL for the new API"
    qint64 decidedBy;       // User ID
    QDateTime decidedAt;
    QString context;         // Surrounding conversation
    QVector<qint64> supportingMessages;
    float confidence;        // 0.0 - 1.0
};

QVector<Decision> extractDecisions(qint64 chatId) {
    QVector<Decision> decisions;

    // Search for decision keywords
    QStringList decisionKeywords = {
        "let's go with", "decided to", "we'll use",
        "final decision", "agreed on", "approved"
    };

    auto messages = _archiver->getMessages(chatId, 10000, 0);

    for (const auto &msg : messages) {
        for (const QString &keyword : decisionKeywords) {
            if (msg.text.contains(keyword, Qt::CaseInsensitive)) {
                Decision d;
                d.decidedBy = msg.userId;
                d.decidedAt = QDateTime::fromSecsSinceEpoch(msg.timestamp);
                d.decision = extractDecisionText(msg.text, keyword);
                d.context = getContext(chatId, msg.id, 5);
                d.confidence = calculateConfidence(msg, keyword);

                decisions.append(d);
            }
        }
    }

    return decisions;
}
```

#### User Interface

**Search Interface**:
```
┌─ Advanced Search ──────────────────────────────────────┐
│                                                        │
│  🔍 Search: [API redesign discussion____________]  🔎  │
│                                                        │
│  Search Type:                                          │
│  ○ Exact match   ● Semantic (AI)   ○ Fuzzy           │
│                                                        │
│  Filters:                                              │
│  Chats:    [All Chats ▼]                              │
│  Users:    [Anyone ▼]                                 │
│  Date:     [Last 30 days ▼]                           │
│  Type:     [All Messages ▼]                           │
│                                                        │
│  ── Results (47 found) ────────────────────────────   │
│                                                        │
│  📊 95% relevance | #backend | @alice | 2 days ago    │
│  "We should redesign the API to use GraphQL instead   │
│   of REST. It'll make the frontend..."                │
│  [View Message] [View Context] [Add to Collection]    │
│                                                        │
│  📊 89% relevance | #frontend | @bob | 1 week ago     │
│  "The API refactoring will require changes to how     │
│   we handle data fetching..."                         │
│  [View Message] [View Context] [Add to Collection]    │
│                                                        │
│  [ Export Results ]  [ Create Knowledge Graph ]       │
│                                                        │
└────────────────────────────────────────────────────────┘
```

**Knowledge Graph Viewer**:
```
┌─ Knowledge Graph: #backend (Last 30 days) ────────────┐
│                                                        │
│        ┌─────────┐                                     │
│        │   API   │◄────proposed────┐                  │
│        │Redesign │                 │                  │
│        └────┬────┘                 │                  │
│             │                  ┌───┴────┐             │
│      relates_to            Alice│        │             │
│             │              └────────┘             │
│        ┌────▼────┐                │                  │
│        │GraphQL  │◄───approved────┘                  │
│        └─────────┘                                     │
│             │                                          │
│     replaces │                                          │
│             ▼                                          │
│        ┌─────────┐                                     │
│        │  REST   │                                     │
│        └─────────┘                                     │
│                                                        │
│  Entities: 12 | Relationships: 18 | Period: 30 days   │
│                                                        │
│  [ Export Graph ]  [ View Timeline ]  [ Close ]       │
└────────────────────────────────────────────────────────┘
```

**Decision Log**:
```
┌─ Decisions in #product (Last quarter) ────────────────┐
│                                                        │
│  📅 Nov 15, 2024 | @alice                             │
│  "We'll use GraphQL for the new API"                  │
│  Context: Discussion about API architecture           │
│  Confidence: 95% | Supporting messages: 7              │
│  [View Thread] [Export]                                │
│                                                        │
│  📅 Nov 10, 2024 | @bob                                │
│  "Let's deploy on Fridays instead of Mondays"         │
│  Context: Deployment schedule planning                 │
│  Confidence: 88% | Supporting messages: 4              │
│  [View Thread] [Export]                                │
│                                                        │
│  📅 Nov 3, 2024 | @charlie                            │
│  "Approved: Move to microservices architecture"        │
│  Context: Technical infrastructure discussion          │
│  Confidence: 92% | Supporting messages: 12             │
│  [View Thread] [Export]                                │
│                                                        │
│  [ Export All ]  [ Generate Report ]  [ Close ]       │
└────────────────────────────────────────────────────────┘
```

#### Success Metrics

**Usage**:
- 80% of users search at least once per week
- Average 15 searches per user per week
- 70% use semantic search over exact match

**Quality**:
- 85% of searches return relevant results (user feedback)
- Average result relevance score > 0.75
- < 2 seconds for typical search

**Business**:
- 40% reduction in "looking for that message" time
- 25% increase in knowledge retention
- Top 3 most-used feature

#### Privacy & Security

**Data Encryption**:
- Knowledge graph stored encrypted
- Search cache encrypted at rest
- No cloud sync without consent

**Permissions**:
- Read all messages: FULL_HISTORY_ACCESS
- Build knowledge graph: LOCAL_PROCESSING
- Export data: USER_CONSENT_REQUIRED

#### Pricing Tier
- **Free**: Basic search (100 searches/day, exact match only)
- **Pro ($4.99/month)**: Unlimited semantic search, knowledge graphs
- **Enterprise ($49/month)**: Team knowledge graphs, advanced analytics

---

### UC-004: Privacy-Preserving Analytics Bot

**Priority**: HIGH
**Complexity**: MEDIUM
**Market Demand**: MEDIUM-HIGH

#### Description
Comprehensive communication analytics with 100% local processing, giving users insights into their messaging patterns without compromising privacy.

#### User Stories

**US-004.1: Personal Insights**
```
As a user
When I request my communication stats
Then I should see detailed analytics
About my messaging patterns, active hours, top contacts
All processed locally on my device
```

**US-004.2: Time Management**
```
As a user
When I view my weekly report
Then I should see how much time I spend in different chats
With recommendations for time management
```

**US-004.3: Social Network Visualization**
```
As a user
When I request my social graph
Then I should see a visualization of my contacts
Showing communication frequency and relationship strength
```

#### Functional Requirements

**FR-004.1: Analytics Categories**

**Personal Stats**:
- Messages sent/received per day/week/month
- Average message length
- Response time distribution
- Active hours heatmap
- Most used words/phrases
- Emoji usage statistics

**Social Stats**:
- Top contacts by message count
- Relationship strength scores
- Communication reciprocity (who initiates more)
- Group vs. private message ratio
- Response rate by contact

**Content Stats**:
- Media sharing frequency (photos, videos, files)
- Link sharing patterns
- Voice message usage
- Topic distribution
- Sentiment trends

**FR-004.2: Visualization Types**
- **Line charts**: Messages over time
- **Heatmaps**: Activity by hour/day
- **Bar charts**: Top contacts, top words
- **Network graphs**: Social connections
- **Pie charts**: Message type distribution
- **Trend lines**: Growth/decline patterns

**FR-004.3: Report Generation**
- **Daily digest**: Yesterday's activity summary
- **Weekly report**: Last 7 days comprehensive
- **Monthly report**: 30-day trends and insights
- **Annual review**: Year in review (like Spotify Wrapped)
- **Custom period**: User-defined date range

#### Technical Specifications

**Analytics Engine**:
```cpp
class AnalyticsBot : public MCPBot {
public:
    struct PersonalStats {
        // Message counts
        int totalMessages;
        int messagesSent;
        int messagesReceived;

        // Timing
        QVector<int> hourlyActivity;    // 24-hour distribution
        QVector<int> weeklyActivity;    // 7-day distribution
        float avgResponseTimeMinutes;

        // Content
        int mediaShared;
        int linksShared;
        int voiceMessages;
        float avgMessageLength;

        // Social
        int uniqueContacts;
        int groupChats;
        int privateChats;
    };

    struct SocialStats {
        struct Contact {
            qint64 userId;
            QString username;
            int messageCount;
            float relationshipStrength;  // 0.0 - 1.0
            float reciprocity;           // Who initiates more
            QDateTime lastInteraction;
        };

        QVector<Contact> topContacts;
        int totalContacts;
        float networkDensity;
    };

    PersonalStats generatePersonalStats(const QDateTime &since);
    SocialStats generateSocialStats(const QDateTime &since);
    QJsonObject generateReport(ReportType type, const QDateTime &since);

private:
    float calculateRelationshipStrength(qint64 userId);
    QImage generateHeatmap(const QVector<int> &hourlyData);
    QImage generateSocialGraph();
};
```

**Relationship Strength Algorithm**:
```cpp
float calculateRelationshipStrength(qint64 userId) {
    float score = 0.0f;

    // Message frequency (40%)
    int messages30d = getMessageCount(userId, 30);
    float freqScore = std::min(messages30d / 100.0f, 1.0f);
    score += freqScore * 0.4f;

    // Recency (20%)
    auto lastMessage = getLastMessageTime(userId);
    int daysAgo = daysSince(lastMessage);
    float recencyScore = 1.0f / (1.0f + daysAgo * 0.1f);
    score += recencyScore * 0.2f;

    // Response rate (20%)
    float responseRate = calculateResponseRate(userId);
    score += responseRate * 0.2f;

    // Conversation length (10%)
    float avgLength = getAvgConversationLength(userId);
    float lengthScore = std::min(avgLength / 10.0f, 1.0f);
    score += lengthScore * 0.1f;

    // Bidirectionality (10%)
    float reciprocity = calculateReciprocity(userId);
    score += (1.0f - std::abs(reciprocity - 0.5f) * 2.0f) * 0.1f;

    return score;
}
```

**Database Schema**:
```sql
CREATE TABLE analytics_cache (
    cache_key TEXT PRIMARY KEY,
    cache_type TEXT NOT NULL,    -- 'personal_stats', 'social_stats', etc.
    period_start INTEGER,
    period_end INTEGER,
    data JSON NOT NULL,
    computed_at INTEGER,
    expires_at INTEGER
);

CREATE TABLE analytics_snapshots (
    id INTEGER PRIMARY KEY,
    snapshot_date INTEGER NOT NULL,
    snapshot_type TEXT NOT NULL,  -- 'daily', 'weekly', 'monthly'
    stats JSON NOT NULL,
    created_at INTEGER
);

CREATE TABLE relationship_scores (
    user_id INTEGER PRIMARY KEY,
    relationship_strength REAL,
    message_frequency REAL,
    recency_score REAL,
    response_rate REAL,
    reciprocity REAL,
    last_computed INTEGER
);

CREATE INDEX idx_snapshots_date ON analytics_snapshots(snapshot_date, snapshot_type);
```

#### User Interface

**Analytics Dashboard**:
```
┌─ Your Telegram Analytics ──────────────────────────────┐
│                                                        │
│  📊 Last 30 Days                                       │
│                                                        │
│  ┌───────────────────────────────────────────────┐    │
│  │ Messages Sent:    2,347  (+12% vs last month) │    │
│  │ Active Hours:     78h    (-5% vs last month)  │    │
│  │ Unique Contacts:  43     (+3 new)             │    │
│  │ Response Time:    8min   (improved 2min)      │    │
│  └───────────────────────────────────────────────┘    │
│                                                        │
│  ⏰ Activity Heatmap                                   │
│  ┌───────────────────────────────────────────────┐    │
│  │ Mon │██████░░░░░░░░░░░░░░░░██████│             │    │
│  │ Tue │████████░░░░░░░░░░░░████████│             │    │
│  │ Wed │██████░░░░░░░░░░░░░░██████░░│             │    │
│  │ Thu │████████░░░░░░░░░░████████░░│             │    │
│  │ Fri │██████████░░░░░░████████████│             │    │
│  │ Sat │░░░░░░░░░░██████████████████│             │    │
│  │ Sun │░░░░░░░░████████████░░░░░░░░│             │    │
│  │     │0  4  8  12 16 20 24 (hour) │             │    │
│  └───────────────────────────────────────────────┘    │
│                                                        │
│  👥 Top Contacts                                       │
│  1. @alice      487 msgs  ████████████████░░ 82%      │
│  2. @bob        312 msgs  ████████████░░░░░░ 65%      │
│  3. @charlie    289 msgs  ███████████░░░░░░░ 61%      │
│  4. #team       245 msgs  ██████████░░░░░░░░ 58%      │
│  5. @david      198 msgs  ████████░░░░░░░░░░ 52%      │
│                                                        │
│  [ Weekly Report ]  [ Monthly Report ]  [ Export ]    │
│                                                        │
└────────────────────────────────────────────────────────┘
```

**Weekly Report Email/Notification**:
```
📧 Your Weekly Telegram Digest (Nov 11-17)

Hi there! Here's your communication summary:

📊 Overview
• 387 messages sent (↑ 15% vs last week)
• 42 hours active
• 23 unique conversations
• 9min avg response time

⏰ Peak Activity
Your most active day was Wednesday at 3 PM
Quietest period: Saturday mornings

👥 Top Conversations
1. @alice - 87 messages (work coordination)
2. #team - 62 messages (project updates)
3. @bob - 54 messages (planning)

💡 Insights
• You spent 15% more time in work chats this week
• Your response time improved by 3 minutes
• You started 12 new conversations

🎯 Recommendations
• Consider setting "focus time" on Wed afternoons (your busiest period)
• You haven't messaged @charlie in 2 weeks - might be time to catch up
• 23% of your messages are sent after 10 PM - maybe set a reminder to wind down earlier?

[View Full Report] [Manage Preferences]
```

**Social Graph Visualization**:
```
┌─ Your Social Network ──────────────────────────────────┐
│                                                        │
│         @alice ●─────● You                            │
│           │            │                               │
│           │            │                               │
│        @bob ●          ● @charlie                     │
│           │            │                               │
│           └────●───────┘                               │
│               #team                                     │
│                │                                        │
│         @david ●                                       │
│                                                        │
│  Node size = Message frequency                         │
│  Edge thickness = Shared conversations                 │
│  Color = Relationship strength (green=strong)          │
│                                                        │
│  Stats:                                                │
│  • Network density: 0.68 (highly connected)            │
│  • Central hub: #team (bridges 4 contacts)             │
│  • Strongest connection: You ↔ @alice (82%)           │
│                                                        │
│  [ Export Graph ]  [ Analyze Patterns ]  [ Close ]    │
└────────────────────────────────────────────────────────┘
```

#### Success Metrics

**Adoption**:
- 50% of users view analytics at least once
- 25% check weekly reports regularly
- 15% export data

**Engagement**:
- Average 3 analytics sessions per user per month
- 40% act on recommendations
- 8/10 satisfaction score

**Privacy Trust**:
- 95% trust score (user surveys)
- < 1% opt-out rate
- Zero privacy incidents

#### Privacy & Security

**Privacy Guarantees**:
- ✅ 100% local processing
- ✅ No data transmitted to servers
- ✅ No analytics collected by us
- ✅ User owns all data
- ✅ One-click delete all analytics
- ✅ Open source analytics code

**Data Retention**:
- Snapshots kept for 90 days by default
- User can configure retention (7-365 days or forever)
- Automatic purging of old data
- Export before delete option

#### Pricing Tier
- **Free**: Basic stats (last 7 days)
- **Pro ($4.99/month)**: Full analytics, unlimited history, exports
- **Enterprise ($49/month)**: Team analytics, custom reports, API access

---

### UC-005: Ephemeral Message Capture Bot

**Priority**: MEDIUM
**Complexity**: HIGH (requires tdesktop modification)
**Market Demand**: MEDIUM

#### Description
Capture and store self-destructing messages, view-once media, and disappearing content with user's explicit consent, for archival and legal compliance purposes.

#### User Stories

**US-005.1: Self-Destruct Capture**
```
As a user
When someone sends me a self-destructing message
Then I want the option to save it locally before it disappears
For my personal records
```

**US-005.2: View-Once Media**
```
As a user
When I receive a view-once photo
Then I want to save it (with consent notification to sender)
So I don't lose important information
```

**US-005.3: Legal Compliance**
```
As a business user
When operating in a regulated industry
Then I need to archive all communications including ephemeral messages
For compliance with record-keeping laws
```

#### Functional Requirements

**FR-005.1: Capture Triggers**
- **Self-destruct timer**: Capture message with countdown timer
- **View-once**: Capture on first view, before deletion
- **Disappearing mode**: Capture messages in disappearing chat
- **Manual**: User explicitly saves ephemeral message

**FR-005.2: Capture Metadata**
```cpp
struct EphemeralMessage {
    qint64 messageId;
    qint64 chatId;
    qint64 senderId;
    QString content;
    QByteArray mediaData;        // For view-once photos/videos
    QString mediaType;           // "text", "photo", "video", "voice"
    EphemeralType type;          // SELF_DESTRUCT, VIEW_ONCE, DISAPPEARING
    int originalTTL;             // Time-to-live in seconds
    QDateTime capturedAt;
    QDateTime scheduledDeletion; // When it would have been deleted
    bool senderNotified;         // Did we notify sender we saved it?
    QString captureReason;       // "user_manual", "auto_compliance", etc.
};
```

**FR-005.3: Ethical Notifications**
```
When user captures ephemeral content:
1. Show prominent disclosure: "This message will be saved"
2. Option to notify sender (default: ON for view-once, OFF for self-destruct)
3. Capture logged in audit trail
4. Visual indicator in chat that message was saved
```

**FR-005.4: Access Control**
- Captured ephemeral messages stored in separate encrypted database
- Require PIN/password to access
- Configurable auto-delete after N days
- Export control (no accidental leaks)

#### Technical Specifications

**tdesktop Integration Points**:
```cpp
// Hook into message display lifecycle
namespace Tdesktop {

class EphemeralMessageInterceptor {
public:
    // Called when ephemeral message is received
    void onEphemeralMessageReceived(const Message &msg) {
        if (_captureEnabled) {
            auto ephemeral = convertToEphemeral(msg);
            _archiver->captureEphemeral(ephemeral);

            if (_notifySender) {
                sendCaptureNotification(msg.chatId, msg.senderId);
            }
        }
    }

    // Called before message auto-deletes
    void onMessageAboutToDelete(qint64 messageId) {
        if (_captureEnabled) {
            auto msg = getMessage(messageId);
            if (isEphemeral(msg)) {
                onEphemeralMessageReceived(msg);
            }
        }
    }

    // Called when view-once media is opened
    void onViewOnceOpened(qint64 messageId) {
        if (_captureViewOnce) {
            auto media = downloadMedia(messageId);
            _archiver->captureViewOnce(messageId, media);
        }
    }
};

} // namespace Tdesktop
```

**Database Schema**:
```sql
CREATE TABLE ephemeral_messages (
    id INTEGER PRIMARY KEY,
    message_id INTEGER NOT NULL,
    chat_id INTEGER NOT NULL,
    sender_id INTEGER NOT NULL,
    content TEXT,
    media_data BLOB,             -- Encrypted media content
    media_type TEXT,
    ephemeral_type TEXT NOT NULL, -- 'self_destruct', 'view_once', 'disappearing'
    original_ttl INTEGER,
    captured_at INTEGER NOT NULL,
    scheduled_deletion INTEGER,
    sender_notified BOOLEAN DEFAULT 0,
    capture_reason TEXT,
    access_count INTEGER DEFAULT 0,
    last_accessed INTEGER,
    UNIQUE(chat_id, message_id)
);

CREATE TABLE ephemeral_access_log (
    id INTEGER PRIMARY KEY,
    ephemeral_id INTEGER NOT NULL,
    accessed_by INTEGER NOT NULL,  -- User ID
    accessed_at INTEGER NOT NULL,
    access_reason TEXT,
    FOREIGN KEY(ephemeral_id) REFERENCES ephemeral_messages(id)
);

CREATE INDEX idx_ephemeral_chat ON ephemeral_messages(chat_id, captured_at);
CREATE INDEX idx_ephemeral_type ON ephemeral_messages(ephemeral_type);
```

**Encryption**:
```cpp
class EphemeralArchiver {
    // Encrypt captured content with user's key
    QByteArray encryptContent(const QByteArray &content) {
        QByteArray key = getUserEncryptionKey();
        return AES256::encrypt(content, key);
    }

    // Decrypt when accessed (requires PIN)
    QByteArray decryptContent(qint64 ephemeralId, const QString &pin) {
        if (!verifyPIN(pin)) {
            logFailedAccess(ephemeralId);
            return QByteArray();
        }

        auto encrypted = loadEncryptedContent(ephemeralId);
        QByteArray key = deriveKeyFromPIN(pin);

        logAccess(ephemeralId);
        return AES256::decrypt(encrypted, key);
    }
};
```

#### User Interface

**Capture Prompt** (on receiving ephemeral message):
```
┌─ Ephemeral Message Received ───────────────────────────┐
│                                                        │
│  ⏱️ Self-Destructing Message (30 seconds)             │
│                                                        │
│  From: @alice                                          │
│  Content: "The password for the server is abc123"     │
│                                                        │
│  This message will delete itself in 25 seconds.        │
│                                                        │
│  Do you want to save it?                               │
│                                                        │
│  [✓] Notify sender that I saved this message           │
│      (Ethical disclosure - recommended)                │
│                                                        │
│  [ Save Message ]  [ Let It Delete ]                   │
│                                                        │
│  Note: Saved messages are stored encrypted and         │
│  require your PIN to access.                           │
│                                                        │
└────────────────────────────────────────────────────────┘
```

**Ephemeral Archive Viewer**:
```
┌─ Ephemeral Message Archive ────────────────────────────┐
│                                                        │
│  🔐 Enter PIN to access: [****____]  [Unlock]         │
│                                                        │
│  ──────────────────────────────────────────────────   │
│                                                        │
│  ⏱️ Self-Destruct Messages (3)                         │
│                                                        │
│  📅 Today, 2:30 PM | @alice                            │
│  "The password for the server is..."                   │
│  TTL: 30s | Saved: User manual                        │
│  [View Full] [Delete] [Export]                         │
│                                                        │
│  📅 Yesterday, 5:15 PM | @bob                          │
│  "Here's the confidential document..."                 │
│  TTL: 60s | Saved: Auto (compliance)                  │
│  [View Full] [Delete] [Export]                         │
│                                                        │
│  👁️ View-Once Media (2)                                │
│                                                        │
│  📅 Nov 15, 10:00 AM | @charlie                        │
│  [Photo thumbnail - blurred]                           │
│  Sender notified: Yes                                  │
│  [View] [Delete] [Export]                              │
│                                                        │
│  [ Settings ]  [ Export All ]  [ Clear Archive ]      │
│                                                        │
└────────────────────────────────────────────────────────┘
```

**Settings Panel**:
```
┌─ Ephemeral Capture Settings ───────────────────────────┐
│                                                        │
│  Capture Behavior:                                     │
│  [✓] Enable ephemeral message capture                  │
│  [✓] Auto-capture self-destructing messages            │
│  [✓] Auto-capture view-once media                      │
│  [ ] Auto-capture disappearing chat messages           │
│                                                        │
│  Notifications:                                        │
│  [✓] Always notify sender when saving view-once media  │
│  [ ] Notify sender when saving self-destruct messages  │
│  [✓] Show in-chat indicator that message was saved     │
│                                                        │
│  Security:                                             │
│  PIN for archive access: [Change PIN...]               │
│  Encryption: AES-256 ✓                                 │
│  Auto-lock after: [5 minutes ▼]                        │
│                                                        │
│  Retention:                                            │
│  Auto-delete captured messages after:                  │
│  [Never ▼] (Never/7 days/30 days/90 days)             │
│                                                        │
│  Compliance Mode (Enterprise):                         │
│  [ ] Legal compliance mode                             │
│      Requires all ephemeral messages to be captured    │
│      Notifications mandatory | Cannot be disabled      │
│                                                        │
│  [ Save ]  [ Cancel ]                                  │
│                                                        │
└────────────────────────────────────────────────────────┘
```

#### Success Metrics

**Adoption**:
- 30% of users enable ephemeral capture
- 60% of those use it monthly
- Enterprise: 90% enable compliance mode

**Usage**:
- Average 5 ephemeral messages captured per user per week
- 75% view captured messages within 24 hours
- 40% export captured messages

**Compliance**:
- 100% capture rate in compliance mode
- Zero unauthorized access incidents
- Audit trail complete and verifiable

#### Privacy & Security

**Ethical Considerations**:
1. **Sender Awareness**: Strong default to notify senders
2. **Visual Indicators**: Clear UI showing message was saved
3. **Purpose Limitation**: Capture only for stated purposes
4. **Retention Limits**: Encourage time-limited storage
5. **Access Logging**: Every view logged for accountability

**Security Measures**:
- PIN-protected access
- Encrypted at rest (AES-256)
- Encrypted in memory
- Auto-lock after inactivity
- Failed access attempt logging
- No cloud sync without explicit consent

**Legal Compliance**:
- GDPR Article 17 (right to be forgotten): User can delete all
- HIPAA: Encrypted, audit-logged, access-controlled
- SOC 2: Complete audit trail
- Configurable retention policies

#### Pricing Tier
- **Free**: Manual capture only (5 messages/month)
- **Pro ($4.99/month)**: Auto-capture, unlimited storage
- **Enterprise ($49/month)**: Compliance mode, team policies, legal export formats

---

### UC-006: Multi-Chat Coordinator Bot

**Priority**: MEDIUM-HIGH
**Complexity**: MEDIUM
**Market Demand**: HIGH

#### Description
Coordinate information across multiple chats, forward important messages, create digest channels, and manage cross-team communication.

#### User Stories

**US-006.1: Smart Forwarding**
```
As a team lead
When someone mentions "@me" or my name in any work chat
Then I want those messages automatically forwarded to my coordination channel
So I don't miss important mentions
```

**US-006.2: Digest Generation**
```
As a manager
When the day ends
Then I want a digest of all important updates from my 5 project chats
Summarized and sent to my personal channel
```

**US-006.3: Cross-Team Sync**
```
As a coordinator
When important decisions are made in the #backend chat
Then I want them automatically shared in #frontend and #product
So all teams stay aligned
```

#### Functional Requirements

**FR-006.1: Forwarding Rules**
```cpp
struct ForwardingRule {
    qint64 ruleId;
    QString name;                    // "Forward mentions to coordination"
    QVector<qint64> sourceChatIds;   // Chats to monitor
    qint64 destinationChatId;        // Where to forward

    // Filters
    QStringList keywords;            // "urgent", "@me", "help"
    QVector<qint64> userIds;         // Only from specific users
    MessageType messageTypes;        // TEXT, PHOTO, FILE, etc.

    // Conditions
    bool requireAllKeywords;         // AND vs OR logic
    int minMessageLength;
    QString regexPattern;

    // Actions
    ForwardMode mode;                // FORWARD, COPY, SUMMARIZE
    bool includeContext;             // Include prev/next messages
    QString template;                // Custom forward template

    // Schedule
    bool enabled;
    QVector<int> activeDays;         // Days of week (0-6)
    int activeHoursStart;            // Hour (0-23)
    int activeHoursEnd;
};
```

**FR-006.2: Digest Generation**
```cpp
struct DigestConfig {
    qint64 digestId;
    QString name;                    // "Daily team update"
    QVector<qint64> sourceChatIds;
    qint64 destinationChatId;

    // Schedule
    DigestFrequency frequency;       // HOURLY, DAILY, WEEKLY
    QTime sendTime;                  // When to send

    // Content
    DigestFormat format;             // SUMMARY, FULL, HIGHLIGHTS
    int maxMessages;                 // Limit number of messages
    bool groupByChat;               // Organize by source chat
    bool groupByTopic;              // Use AI to group by topic

    // Filters
    ImportanceLevel minImportance;   // CRITICAL, HIGH, NORMAL
    QStringList includeKeywords;
    QStringList excludeKeywords;
};
```

**FR-006.3: Message Importance Scoring**
```cpp
enum ImportanceLevel {
    CRITICAL,   // Mentions "urgent", "ASAP", has many reactions
    HIGH,       // Long message, from important person, in thread
    NORMAL,     // Standard message
    LOW         // Short, routine, automated
};

ImportanceLevel scoreImportance(const Message &msg) {
    float score = 0.0f;

    // Keywords
    if (containsAny(msg.text, {"urgent", "asap", "critical", "important"}))
        score += 0.3f;

    // Length (longer = more important)
    score += std::min(msg.text.length() / 500.0f, 0.2f);

    // Reactions (engagement indicator)
    score += std::min(msg.reactionCount / 10.0f, 0.2f);

    // Sender importance (configurable weights)
    score += getSenderImportance(msg.userId) * 0.2f;

    // Thread participation
    if (msg.isThreadStart || msg.threadReplyCount > 5)
        score += 0.1f;

    // Map to levels
    if (score >= 0.7f) return CRITICAL;
    if (score >= 0.4f) return HIGH;
    if (score >= 0.2f) return NORMAL;
    return LOW;
}
```

#### Technical Specifications

**Rule Engine**:
```cpp
class MultiChatCoordinator : public MCPBot {
public:
    void onMessage(const Message &msg) override {
        // Check all active rules
        for (const auto &rule : _rules) {
            if (!rule.enabled) continue;

            if (matchesRule(msg, rule)) {
                executeRule(msg, rule);
            }
        }

        // Check digest schedules
        for (const auto &digest : _digests) {
            if (shouldGenerateDigest(digest)) {
                generateAndSendDigest(digest);
            }
        }
    }

private:
    bool matchesRule(const Message &msg, const ForwardingRule &rule) {
        // Check source chat
        if (!rule.sourceChatIds.contains(msg.chatId))
            return false;

        // Check keywords
        if (!rule.keywords.isEmpty()) {
            bool matches = rule.requireAllKeywords
                ? containsAll(msg.text, rule.keywords)
                : containsAny(msg.text, rule.keywords);
            if (!matches) return false;
        }

        // Check user filter
        if (!rule.userIds.isEmpty() && !rule.userIds.contains(msg.userId))
            return false;

        // Check regex
        if (!rule.regexPattern.isEmpty()) {
            QRegularExpression regex(rule.regexPattern);
            if (!regex.match(msg.text).hasMatch())
                return false;
        }

        // Check schedule
        QDateTime now = QDateTime::currentDateTime();
        if (!rule.activeDays.contains(now.date().dayOfWeek() - 1))
            return false;

        int hour = now.time().hour();
        if (hour < rule.activeHoursStart || hour >= rule.activeHoursEnd)
            return false;

        return true;
    }

    void executeRule(const Message &msg, const ForwardingRule &rule) {
        switch (rule.mode) {
        case ForwardMode::FORWARD:
            forwardMessage(msg.chatId, rule.destinationChatId, msg.id);
            break;

        case ForwardMode::COPY:
            sendMessage(rule.destinationChatId, formatMessage(msg, rule.template));
            break;

        case ForwardMode::SUMMARIZE:
            QString summary = summarizeMessage(msg);
            sendMessage(rule.destinationChatId, summary);
            break;
        }

        _auditLogger->logTelegramOp("rule_executed", {
            {"rule_id", rule.ruleId},
            {"source_chat", msg.chatId},
            {"dest_chat", rule.destinationChatId}
        });
    }
};
```

**Digest Generator**:
```cpp
QString generateDigest(const DigestConfig &config) {
    QVector<Message> messages;

    // Collect messages from all source chats
    QDateTime since = getLastDigestTime(config.digestId);
    for (qint64 chatId : config.sourceChatIds) {
        auto chatMsgs = _archiver->getMessagesSince(chatId, since);
        messages.append(chatMsgs);
    }

    // Filter by importance
    messages = filterByImportance(messages, config.minImportance);

    // Filter by keywords
    if (!config.includeKeywords.isEmpty()) {
        messages = filterByKeywords(messages, config.includeKeywords, true);
    }
    if (!config.excludeKeywords.isEmpty()) {
        messages = filterByKeywords(messages, config.excludeKeywords, false);
    }

    // Sort by timestamp or importance
    std::sort(messages.begin(), messages.end(),
              [](const Message &a, const Message &b) {
        return scoreImportance(a) > scoreImportance(b);
    });

    // Limit count
    if (messages.size() > config.maxMessages) {
        messages = messages.mid(0, config.maxMessages);
    }

    // Format digest
    return formatDigest(messages, config);
}

QString formatDigest(const QVector<Message> &messages, const DigestConfig &config) {
    QString digest;

    digest += QString("📋 **%1**\n").arg(config.name);
    digest += QString("📅 %1\n").arg(QDateTime::currentDateTime().toString());
    digest += QString("📊 %1 updates\n\n").arg(messages.size());

    if (config.groupByChat) {
        // Group by source chat
        QMap<qint64, QVector<Message>> grouped;
        for (const auto &msg : messages) {
            grouped[msg.chatId].append(msg);
        }

        for (auto it = grouped.begin(); it != grouped.end(); ++it) {
            QString chatName = getChatName(it.key());
            digest += QString("## 💬 %1\n\n").arg(chatName);

            for (const auto &msg : it.value()) {
                digest += formatMessageSummary(msg) + "\n\n";
            }
        }
    } else if (config.groupByTopic) {
        // Use AI to group by topic
        auto topics = _semanticSearch->clusterTopics(messages);

        for (const auto &topic : topics) {
            digest += QString("## 📌 %1\n\n").arg(topic.name);

            for (const auto &msg : topic.messages) {
                digest += formatMessageSummary(msg) + "\n\n";
            }
        }
    } else {
        // Chronological
        for (const auto &msg : messages) {
            digest += formatMessageSummary(msg) + "\n\n";
        }
    }

    digest += QString("\n---\n📊 Generated by Multi-Chat Coordinator");

    return digest;
}
```

**Database Schema**:
```sql
CREATE TABLE forwarding_rules (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    source_chat_ids TEXT NOT NULL,  -- JSON array
    destination_chat_id INTEGER NOT NULL,
    keywords TEXT,                   -- JSON array
    user_ids TEXT,                   -- JSON array
    regex_pattern TEXT,
    require_all_keywords BOOLEAN DEFAULT 0,
    min_message_length INTEGER DEFAULT 0,
    forward_mode TEXT DEFAULT 'forward',
    include_context BOOLEAN DEFAULT 0,
    template TEXT,
    enabled BOOLEAN DEFAULT 1,
    active_days TEXT,                -- JSON array [0,1,2,3,4]
    active_hours_start INTEGER DEFAULT 0,
    active_hours_end INTEGER DEFAULT 24,
    created_at INTEGER,
    last_triggered INTEGER
);

CREATE TABLE digest_configs (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    source_chat_ids TEXT NOT NULL,
    destination_chat_id INTEGER NOT NULL,
    frequency TEXT NOT NULL,         -- 'hourly', 'daily', 'weekly'
    send_time TEXT,                  -- "HH:MM"
    format TEXT DEFAULT 'summary',
    max_messages INTEGER DEFAULT 50,
    group_by_chat BOOLEAN DEFAULT 1,
    group_by_topic BOOLEAN DEFAULT 0,
    min_importance TEXT DEFAULT 'normal',
    include_keywords TEXT,
    exclude_keywords TEXT,
    enabled BOOLEAN DEFAULT 1,
    last_sent INTEGER
);

CREATE TABLE forwarding_log (
    id INTEGER PRIMARY KEY,
    rule_id INTEGER,
    source_message_id INTEGER,
    source_chat_id INTEGER,
    destination_chat_id INTEGER,
    forwarded_at INTEGER,
    FOREIGN KEY(rule_id) REFERENCES forwarding_rules(id)
);

CREATE INDEX idx_rules_source ON forwarding_rules(source_chat_ids);
CREATE INDEX idx_digests_schedule ON digest_configs(frequency, send_time);
```

#### User Interface

**Rule Manager**:
```
┌─ Forwarding Rules ─────────────────────────────────────┐
│                                                        │
│  ✓ Urgent Mentions                                     │
│    From: #backend, #frontend, #product                 │
│    To: #my-coordination                                │
│    When: Contains "@me" OR "urgent"                    │
│    [ Edit ]  [ Disable ]  [ Delete ]                   │
│                                                        │
│  ✓ Bug Reports                                         │
│    From: #support                                      │
│    To: #dev-team                                       │
│    When: Contains "bug" OR "error" AND from @support  │
│    [ Edit ]  [ Disable ]  [ Delete ]                   │
│                                                        │
│  ○ After-Hours Mentions (disabled)                     │
│    From: All work chats                                │
│    To: #urgent-only                                    │
│    When: Mentions me, 6 PM - 9 AM                     │
│    [ Edit ]  [ Enable ]  [ Delete ]                    │
│                                                        │
│  [ + New Rule ]  [ Import ]  [ Export ]                │
│                                                        │
└────────────────────────────────────────────────────────┘
```

**Rule Editor**:
```
┌─ Create Forwarding Rule ───────────────────────────────┐
│                                                        │
│  Rule Name: [Urgent Mentions_________________]         │
│                                                        │
│  Source Chats:                                         │
│  [x] #backend   [x] #frontend   [x] #product          │
│  [ ] #design    [ ] #marketing  [ ] #sales            │
│  [ Select All ]  [ Clear ]                             │
│                                                        │
│  Destination:                                          │
│  [#my-coordination ▼]                                  │
│                                                        │
│  Trigger Conditions:                                   │
│  [ Any ▼] of the following:                            │
│  • Contains: [@me____________] [x]                     │
│  • Contains: [urgent__________] [x]                    │
│  • Contains: [______________] [ + Add ]                │
│                                                        │
│  Additional Filters:                                   │
│  Only from users: [Anyone ▼]                           │
│  Only these message types: [All types ▼]              │
│  Minimum length: [0___] characters                     │
│  Regex pattern: [_________________________]           │
│                                                        │
│  Action:                                               │
│  ○ Forward original message                            │
│  ● Copy with custom format                             │
│  ○ Send AI summary                                     │
│                                                        │
│  Format Template:                                      │
│  ┌──────────────────────────────────────────────┐     │
│  │ 🔔 Mention in {chat_name}                    │     │
│  │ From: @{username}                            │     │
│  │ {message_text}                               │     │
│  │ [View in Chat]                               │     │
│  └──────────────────────────────────────────────┘     │
│                                                        │
│  Schedule:                                             │
│  Active days: [x]Mon [x]Tue [x]Wed [x]Thu [x]Fri      │
│              [ ]Sat [ ]Sun                             │
│  Active hours: [09:00] to [18:00]                      │
│                                                        │
│  [✓] Enable this rule                                  │
│                                                        │
│  [ Create Rule ]  [ Cancel ]                           │
│                                                        │
└────────────────────────────────────────────────────────┘
```

**Digest Configuration**:
```
┌─ Configure Digest ─────────────────────────────────────┐
│                                                        │
│  Digest Name: [Daily Team Update_______________]       │
│                                                        │
│  Source Chats:                                         │
│  [x] #project-alpha   [x] #project-beta               │
│  [x] #dev-team        [ ] #design-team                │
│  [ Select All ]                                        │
│                                                        │
│  Send To: [#daily-digest ▼]                           │
│                                                        │
│  Schedule:                                             │
│  Frequency: [Daily ▼]                                  │
│  Time: [17:00] (5:00 PM)                              │
│  Days: [x]Mon [x]Tue [x]Wed [x]Thu [x]Fri            │
│                                                        │
│  Content:                                              │
│  Format: [Summary with highlights ▼]                   │
│  Max messages: [50___]                                 │
│  Minimum importance: [Normal ▼]                        │
│                                                        │
│  Organization:                                         │
│  [x] Group by source chat                              │
│  [ ] Group by topic (AI-powered)                       │
│  [ ] Chronological order                               │
│                                                        │
│  Filters:                                              │
│  Include keywords: [release, deployed, decision]       │
│  Exclude keywords: [bot, automated]                    │
│                                                        │
│  Preview Digest:                                       │
│  [ Generate Preview ]                                  │
│                                                        │
│  [✓] Enable this digest                                │
│                                                        │
│  [ Save ]  [ Cancel ]  [ Delete Digest ]              │
│                                                        │
└────────────────────────────────────────────────────────┘
```

**Sample Digest Output**:
```
📋 **Daily Team Update**
📅 November 16, 2024 - 5:00 PM
📊 12 updates

## 💬 #project-alpha

🔴 CRITICAL | @alice | 3:45 PM
Major deployment issue - API gateway is returning 502 errors.
Rolling back to previous version.
[View Message]

🟡 HIGH | @bob | 2:30 PM
Frontend integration complete. Ready for QA testing tomorrow.
[View Message]

🟢 NORMAL | @charlie | 11:15 AM
Updated documentation with latest API changes.
[View Message]

## 💬 #project-beta

🔴 CRITICAL | @david | 4:20 PM
Client requested urgent changes to dashboard. Need design review ASAP.
[View Message]

🟡 HIGH | @eve | 1:10 PM
Performance improvements deployed. Load time reduced by 40%.
[View Message]

## 💬 #dev-team

🟡 HIGH | @frank | 10:00 AM
Sprint planning completed. 23 story points committed for next week.
[View Message]

---
📊 Generated by Multi-Chat Coordinator
[View Full Archive] [Manage Digest Settings]
```

#### Success Metrics

**Adoption**:
- 40% of power users create at least one rule
- Average 3 rules per user
- 25% use digest feature

**Effectiveness**:
- 60% reduction in "did anyone see this?" messages
- 30% faster cross-team communication
- 85% of forwarded messages are relevant (user feedback)

**Engagement**:
- 90% of digests are opened
- 70% click through to source messages
- Average 2 digest subscriptions per user

#### Privacy & Security

**Access Control**:
- Can only forward from chats user has access to
- Forwarding rules visible to admins (enterprise)
- Audit trail of all forwards

**Data Retention**:
- Forwarding log kept for 90 days
- Digests stored separately (user-configurable)
- Can export forwarding rules

#### Pricing Tier
- **Free**: 1 forwarding rule, no digests
- **Pro ($4.99/month)**: 10 rules, 3 digests, AI summaries
- **Enterprise ($49/month)**: Unlimited, team templates, compliance logs

---

*[Continued in next section due to length...]*

