// Context-Aware Personal AI Assistant Bot
// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include "bot_base.h"

#include <QtCore/QVector>
#include <QtCore/QMap>
#include <QtCore/QDateTime>
#include <QtCore/QSet>

namespace MCP {

// Intent classification results
enum class MessageIntent {
	Unknown,
	Question,          // User is asking a question
	Task,              // User mentions a task/todo
	Scheduling,        // User mentions time/date/scheduling
	Search,            // User wants to find something
	Summarization,     // User wants a summary
	Decision,          // Important decision being made
	Instruction        // Step-by-step instructions
};

// Context state for a conversation
struct ConversationContext {
	qint64 chatId;
	QVector<Message> recentMessages;
	MessageIntent lastIntent = MessageIntent::Unknown;
	QDateTime lastActivity;
	QStringList detectedTopics;
	QStringList detectedEntities;  // People, places, dates mentioned
	int questionCount = 0;
	int taskCount = 0;
	bool userNeedsHelp = false;
	double contextConfidence = 0.0;  // 0.0 to 1.0
};

// User preferences
struct UserPreferences {
	qint64 userId;
	bool enableProactiveHelp = true;
	bool enableCrossChat = true;
	int minConfidenceForSuggestion = 70;  // 0-100
	QStringList mutedTopics;
	QSet<qint64> mutedChats;
	QDateTime lastUpdated;
};

// Context-Aware Personal AI Assistant Bot
class ContextAssistantBot : public BotBase {
	Q_OBJECT

public:
	explicit ContextAssistantBot(QObject *parent = nullptr);
	~ContextAssistantBot() override;

	// BotBase interface
	BotInfo info() const override;
	bool onInitialize() override;
	void onShutdown() override;
	void onMessage(const Message &msg) override;
	void onCommand(const QString &cmd, const QJsonObject &args) override;

	QJsonObject defaultConfig() const override;

private:
	// Intent classification
	MessageIntent classifyIntent(const Message &msg) const;
	double calculateConfidence(const Message &msg, MessageIntent intent) const;

	// Context analysis
	void updateContext(const Message &msg);
	ConversationContext getContext(qint64 chatId) const;
	QStringList extractTopics(const QString &text) const;
	QStringList extractEntities(const QString &text) const;
	bool shouldOfferHelp(const ConversationContext &context) const;

	// Proactive assistance
	void offerHelp(const ConversationContext &context);
	QString generateHelpSuggestion(const ConversationContext &context) const;
	void suggestSearch(qint64 chatId, const QString &query);
	void suggestSummarization(qint64 chatId);
	void suggestTask(qint64 chatId, const QString &taskDescription);

	// Cross-chat intelligence
	void analyzeUserBehavior(qint64 userId);
	QVector<Message> getUserMessagesAcrossChats(qint64 userId, int hours = 24) const;
	QStringList findRelatedConversations(const QStringList &topics) const;

	// User preferences
	UserPreferences getUserPreferences(qint64 userId) const;
	void updateUserPreferences(qint64 userId, const UserPreferences &prefs);
	bool isFeatureEnabledForUser(qint64 userId, const QString &feature) const;

	// NLP utilities
	bool containsQuestion(const QString &text) const;
	bool containsTaskKeywords(const QString &text) const;
	bool containsTimeReference(const QString &text) const;
	QStringList tokenize(const QString &text) const;
	double calculateSimilarity(const QString &text1, const QString &text2) const;

	// State management
	QMap<qint64, ConversationContext> _contexts;  // chatId -> context
	QMap<qint64, UserPreferences> _userPreferences;  // userId -> preferences
	QDateTime _lastCleanup;

	// Configuration
	int _maxContextMessages = 10;
	int _contextTimeoutMinutes = 30;
	double _minConfidenceThreshold = 0.7;
	bool _enableLearning = true;

	// Statistics
	qint64 _totalSuggestionsOffered = 0;
	qint64 _totalSuggestionsAccepted = 0;
	qint64 _intentsClassified = 0;
};

} // namespace MCP
