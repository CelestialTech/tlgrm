// Context-Aware Personal AI Assistant Bot - Implementation
// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "context_assistant_bot.h"
#include "semantic_search.h"
#include "analytics.h"
#include "chat_archiver.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QDebug>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <algorithm>

namespace MCP {

ContextAssistantBot::ContextAssistantBot(QObject *parent)
	: BotBase(parent) {
}

ContextAssistantBot::~ContextAssistantBot() {
}

// BotBase interface

BotBase::BotInfo ContextAssistantBot::info() const {
	BotInfo info;
	info.id = "context_assistant";
	info.name = "Context-Aware AI Assistant";
	info.version = "1.0.0";
	info.description = "Proactively offers help based on conversation context";
	info.author = "Telegram MCP Framework";
	info.tags = QStringList{"ai", "assistant", "context", "proactive"};
	info.isPremium = false;
	return info;
}

bool ContextAssistantBot::onInitialize() {
	logInfo("Initializing Context Assistant Bot...");

	// Register required permissions
	addRequiredPermission(Permissions::READ_MESSAGES);
	addRequiredPermission(Permissions::READ_CHATS);
	addRequiredPermission(Permissions::SEND_MESSAGES);
	addRequiredPermission(Permissions::READ_ANALYTICS);

	// Load configuration
	QJsonObject cfg = config();
	_maxContextMessages = cfg.value("max_context_messages").toInt(10);
	_contextTimeoutMinutes = cfg.value("context_timeout_minutes").toInt(30);
	_minConfidenceThreshold = cfg.value("min_confidence").toDouble(0.7);
	_enableLearning = cfg.value("enable_learning").toBool(true);

	// Load saved state
	_totalSuggestionsOffered = loadState("total_suggestions", 0).toLongLong();
	_totalSuggestionsAccepted = loadState("suggestions_accepted", 0).toLongLong();
	_intentsClassified = loadState("intents_classified", 0).toLongLong();

	_lastCleanup = QDateTime::currentDateTime();

	logInfo(QString("Bot initialized. Context size: %1, Timeout: %2 min")
		.arg(_maxContextMessages)
		.arg(_contextTimeoutMinutes));

	return true;
}

void ContextAssistantBot::onShutdown() {
	logInfo("Shutting down Context Assistant Bot...");

	// Save statistics
	saveState("total_suggestions", _totalSuggestionsOffered);
	saveState("suggestions_accepted", _totalSuggestionsAccepted);
	saveState("intents_classified", _intentsClassified);

	// Clear context
	_contexts.clear();
	_userPreferences.clear();

	logInfo("Shutdown complete");
}

void ContextAssistantBot::onMessage(const Message &msg) {
	// Ignore empty messages
	if (msg.text.trimmed().isEmpty()) {
		return;
	}

	// Update context
	updateContext(msg);

	// Classify intent
	MessageIntent intent = classifyIntent(msg);
	_intentsClassified++;

	// Get context for this chat
	ConversationContext context = getContext(msg.chatId);
	context.lastIntent = intent;

	// Update context statistics
	if (intent == MessageIntent::Question) {
		context.questionCount++;
	} else if (intent == MessageIntent::Task) {
		context.taskCount++;
	}

	_contexts[msg.chatId] = context;

	// Check if we should offer help
	if (shouldOfferHelp(context)) {
		offerHelp(context);
	}

	// Specific intent handling
	switch (intent) {
		case MessageIntent::Question:
			if (context.questionCount >= 2) {
				suggestSearch(msg.chatId, msg.text);
			}
			break;

		case MessageIntent::Search:
			suggestSearch(msg.chatId, msg.text);
			break;

		case MessageIntent::Summarization:
			suggestSummarization(msg.chatId);
			break;

		case MessageIntent::Task:
			suggestTask(msg.chatId, msg.text);
			break;

		case MessageIntent::Decision:
			// Log important decisions for later retrieval
			if (semanticSearch()) {
				logInfo(QString("Important decision detected in chat %1").arg(msg.chatId));
			}
			break;

		default:
			break;
	}

	// Periodic cleanup of old contexts
	QDateTime now = QDateTime::currentDateTime();
	if (_lastCleanup.secsTo(now) > 3600) { // Every hour
		for (auto it = _contexts.begin(); it != _contexts.end(); ) {
			if (it->lastActivity.secsTo(now) > _contextTimeoutMinutes * 60) {
				it = _contexts.erase(it);
			} else {
				++it;
			}
		}
		_lastCleanup = now;
	}
}

void ContextAssistantBot::onCommand(const QString &cmd, const QJsonObject &args) {
	logInfo(QString("Command received: %1").arg(cmd));

	if (cmd == "help") {
		qint64 chatId = args.value("chat_id").toVariant().toLongLong();
		sendMessage(chatId,
			"Context Assistant Bot\n\n"
			"Commands:\n"
			"/help - Show this help\n"
			"/analyze - Analyze conversation context\n"
			"/settings - View/change settings\n"
			"/stats - Show bot statistics"
		);

	} else if (cmd == "analyze") {
		qint64 chatId = args.value("chat_id").toVariant().toLongLong();
		ConversationContext ctx = getContext(chatId);

		QString analysis = QString(
			"📊 Context Analysis:\n"
			"Recent messages: %1\n"
			"Topics: %2\n"
			"Questions: %3\n"
			"Tasks: %4\n"
			"Confidence: %5%"
		).arg(ctx.recentMessages.size())
		 .arg(ctx.detectedTopics.join(", "))
		 .arg(ctx.questionCount)
		 .arg(ctx.taskCount)
		 .arg(static_cast<int>(ctx.contextConfidence * 100));

		sendMessage(chatId, analysis);

	} else if (cmd == "stats") {
		qint64 chatId = args.value("chat_id").toVariant().toLongLong();

		double acceptanceRate = _totalSuggestionsOffered > 0
			? (static_cast<double>(_totalSuggestionsAccepted) / _totalSuggestionsOffered) * 100
			: 0.0;

		QString stats = QString(
			"📈 Bot Statistics:\n"
			"Intents classified: %1\n"
			"Suggestions offered: %2\n"
			"Suggestions accepted: %3\n"
			"Acceptance rate: %4%\n"
			"Active contexts: %5"
		).arg(_intentsClassified)
		 .arg(_totalSuggestionsOffered)
		 .arg(_totalSuggestionsAccepted)
		 .arg(acceptanceRate, 0, 'f', 1)
		 .arg(_contexts.size());

		sendMessage(chatId, stats);

	} else if (cmd == "settings") {
		qint64 userId = args.value("user_id").toVariant().toLongLong();
		UserPreferences prefs = getUserPreferences(userId);

		QString settings = QString(
			"⚙️ Your Settings:\n"
			"Proactive help: %1\n"
			"Cross-chat analysis: %2\n"
			"Confidence threshold: %3%\n"
			"Muted chats: %4"
		).arg(prefs.enableProactiveHelp ? "ON" : "OFF")
		 .arg(prefs.enableCrossChat ? "ON" : "OFF")
		 .arg(prefs.minConfidenceForSuggestion)
		 .arg(prefs.mutedChats.size());

		qint64 chatId = args.value("chat_id").toVariant().toLongLong();
		sendMessage(chatId, settings);

	} else {
		logWarning(QString("Unknown command: %1").arg(cmd));
	}
}

QJsonObject ContextAssistantBot::defaultConfig() const {
	QJsonObject config;
	config["max_context_messages"] = 10;
	config["context_timeout_minutes"] = 30;
	config["min_confidence"] = 0.7;
	config["enable_learning"] = true;
	config["enable_proactive_help"] = true;
	return config;
}

// Intent classification

MessageIntent ContextAssistantBot::classifyIntent(const Message &msg) const {
	QString text = msg.text.toLower().trimmed();

	// Question detection
	if (containsQuestion(text)) {
		return MessageIntent::Question;
	}

	// Task detection
	if (containsTaskKeywords(text)) {
		return MessageIntent::Task;
	}

	// Scheduling detection
	if (containsTimeReference(text)) {
		return MessageIntent::Scheduling;
	}

	// Search keywords
	QStringList searchKeywords = {"find", "search", "look for", "where is", "show me"};
	for (const QString &keyword : searchKeywords) {
		if (text.contains(keyword)) {
			return MessageIntent::Search;
		}
	}

	// Summarization keywords
	QStringList summaryKeywords = {"summarize", "summary", "recap", "overview", "tldr"};
	for (const QString &keyword : summaryKeywords) {
		if (text.contains(keyword)) {
			return MessageIntent::Summarization;
		}
	}

	// Decision keywords
	QStringList decisionKeywords = {"decide", "decision", "we should", "let's go with", "agreed"};
	for (const QString &keyword : decisionKeywords) {
		if (text.contains(keyword)) {
			return MessageIntent::Decision;
		}
	}

	// Instruction keywords
	QStringList instructionKeywords = {"step 1", "first", "second", "then", "finally", "how to"};
	for (const QString &keyword : instructionKeywords) {
		if (text.contains(keyword)) {
			return MessageIntent::Instruction;
		}
	}

	return MessageIntent::Unknown;
}

double ContextAssistantBot::calculateConfidence(const Message &msg, MessageIntent intent) const {
	// Simple confidence calculation based on keyword presence
	// In a real implementation, this would use ML models

	QString text = msg.text.toLower();
	int matchCount = 0;
	int totalChecks = 5;

	switch (intent) {
		case MessageIntent::Question:
			if (text.contains("?")) matchCount++;
			if (text.startsWith("what") || text.startsWith("how") ||
			    text.startsWith("why") || text.startsWith("when") ||
			    text.startsWith("where")) matchCount++;
			if (text.contains("can you")) matchCount++;
			break;

		case MessageIntent::Task:
			if (containsTaskKeywords(text)) matchCount += 2;
			break;

		case MessageIntent::Search:
			if (text.contains("find") || text.contains("search")) matchCount += 2;
			break;

		default:
			return 0.5;
	}

	return static_cast<double>(matchCount) / totalChecks;
}

// Context analysis

void ContextAssistantBot::updateContext(const Message &msg) {
	ConversationContext &context = _contexts[msg.chatId];

	context.chatId = msg.chatId;
	context.lastActivity = QDateTime::currentDateTime();

	// Add to recent messages
	context.recentMessages.append(msg);

	// Keep only last N messages
	while (context.recentMessages.size() > _maxContextMessages) {
		context.recentMessages.removeFirst();
	}

	// Extract topics and entities
	context.detectedTopics = extractTopics(msg.text);
	context.detectedEntities = extractEntities(msg.text);

	// Calculate context confidence
	context.contextConfidence = static_cast<double>(context.recentMessages.size()) / _maxContextMessages;
}

ConversationContext ContextAssistantBot::getContext(qint64 chatId) const {
	return _contexts.value(chatId, ConversationContext());
}

QStringList ContextAssistantBot::extractTopics(const QString &text) const {
	QStringList topics;

	// Simple keyword-based topic extraction
	// In a real implementation, use NLP/topic modeling

	QStringList keywords = {"meeting", "project", "deadline", "payment",
	                        "schedule", "bug", "feature", "release"};

	QString lowerText = text.toLower();
	for (const QString &keyword : keywords) {
		if (lowerText.contains(keyword)) {
			topics.append(keyword);
		}
	}

	return topics;
}

QStringList ContextAssistantBot::extractEntities(const QString &text) const {
	QStringList entities;

	// Extract dates
	QRegularExpression dateRegex("\\d{1,2}[/-]\\d{1,2}[/-]\\d{2,4}");
	QRegularExpressionMatchIterator it = dateRegex.globalMatch(text);
	while (it.hasNext()) {
		QRegularExpressionMatch match = it.next();
		entities.append("date:" + match.captured(0));
	}

	// Extract times
	QRegularExpression timeRegex("\\d{1,2}:\\d{2}");
	it = timeRegex.globalMatch(text);
	while (it.hasNext()) {
		QRegularExpressionMatch match = it.next();
		entities.append("time:" + match.captured(0));
	}

	// Extract mentions
	QRegularExpression mentionRegex("@\\w+");
	it = mentionRegex.globalMatch(text);
	while (it.hasNext()) {
		QRegularExpressionMatch match = it.next();
		entities.append("mention:" + match.captured(0));
	}

	return entities;
}

bool ContextAssistantBot::shouldOfferHelp(const ConversationContext &context) const {
	// Don't offer help if context is too fresh
	if (context.recentMessages.size() < 3) {
		return false;
	}

	// Offer help if user asked multiple questions
	if (context.questionCount >= 2) {
		return true;
	}

	// Offer help if multiple tasks mentioned
	if (context.taskCount >= 2) {
		return true;
	}

	// Offer help if context confidence is high
	if (context.contextConfidence >= _minConfidenceThreshold) {
		return true;
	}

	return false;
}

// Proactive assistance

void ContextAssistantBot::offerHelp(const ConversationContext &context) {
	QString suggestion = generateHelpSuggestion(context);

	if (!suggestion.isEmpty()) {
		sendMessage(context.chatId, suggestion);
		_totalSuggestionsOffered++;

		logInfo(QString("Offered help in chat %1").arg(context.chatId));
	}
}

QString ContextAssistantBot::generateHelpSuggestion(const ConversationContext &context) const {
	QString suggestion;

	if (context.questionCount >= 2) {
		suggestion = "💡 I noticed you're asking questions. Would you like me to search through your message history to find relevant information?";
	} else if (context.taskCount >= 2) {
		suggestion = "💡 I detected multiple tasks being discussed. Would you like me to help organize them into a task list?";
	} else if (!context.detectedTopics.isEmpty()) {
		suggestion = QString("💡 I see you're discussing %1. Would you like me to find related conversations?")
			.arg(context.detectedTopics.first());
	}

	return suggestion;
}

void ContextAssistantBot::suggestSearch(qint64 chatId, const QString &query) {
	if (!semanticSearch()) {
		return;
	}

	QString suggestion = QString(
		"🔍 Would you like me to search for \"%1\" in your message history?"
	).arg(query);

	sendMessage(chatId, suggestion);
	_totalSuggestionsOffered++;
}

void ContextAssistantBot::suggestSummarization(qint64 chatId) {
	QString suggestion =
		"📝 Would you like me to summarize the recent conversation in this chat?";

	sendMessage(chatId, suggestion);
	_totalSuggestionsOffered++;
}

void ContextAssistantBot::suggestTask(qint64 chatId, const QString &taskDescription) {
	QString suggestion = QString(
		"✅ I detected a task: \"%1\"\n"
		"Would you like me to add it to your task list?"
	).arg(taskDescription);

	sendMessage(chatId, suggestion);
	_totalSuggestionsOffered++;
}

// Cross-chat intelligence

void ContextAssistantBot::analyzeUserBehavior(qint64 userId) {
	if (!isFeatureEnabledForUser(userId, "cross_chat")) {
		return;
	}

	QVector<Message> userMessages = getUserMessagesAcrossChats(userId, 24);

	logInfo(QString("Analyzed %1 messages for user %2")
		.arg(userMessages.size())
		.arg(userId));

	if (userMessages.isEmpty()) {
		return;
	}

	// Detect peak activity hours
	QVector<int> hourBuckets(24, 0);
	for (const Message &msg : userMessages) {
		QDateTime dt = QDateTime::fromSecsSinceEpoch(msg.timestamp);
		int hour = dt.time().hour();
		if (hour >= 0 && hour < 24) {
			hourBuckets[hour]++;
		}
	}

	// Find peak hour
	int peakHour = 0;
	int peakCount = 0;
	for (int h = 0; h < 24; ++h) {
		if (hourBuckets[h] > peakCount) {
			peakCount = hourBuckets[h];
			peakHour = h;
		}
	}

	// Store behavioral data in user preferences
	UserPreferences prefs = getUserPreferences(userId);
	prefs.lastUpdated = QDateTime::currentDateTime();
	_userPreferences.insert(userId, prefs);
}

QVector<Message> ContextAssistantBot::getUserMessagesAcrossChats(qint64 userId, int hours) const {
	QVector<Message> userMessages;

	// Iterate through all contexts and find user's messages
	for (auto it = _contexts.constBegin(); it != _contexts.constEnd(); ++it) {
		const ConversationContext &ctx = it.value();

		for (const Message &msg : ctx.recentMessages) {
			if (msg.userId == userId) {
				userMessages.append(msg);
			}
		}
	}

	return userMessages;
}

QStringList ContextAssistantBot::findRelatedConversations(const QStringList &topics) const {
	QStringList relatedChats;

	for (auto it = _contexts.constBegin(); it != _contexts.constEnd(); ++it) {
		const ConversationContext &ctx = it.value();

		// Check if any topics match
		for (const QString &topic : topics) {
			if (ctx.detectedTopics.contains(topic)) {
				relatedChats.append(QString::number(ctx.chatId));
				break;
			}
		}
	}

	return relatedChats;
}

// User preferences

UserPreferences ContextAssistantBot::getUserPreferences(qint64 userId) const {
	if (_userPreferences.contains(userId)) {
		return _userPreferences.value(userId);
	}

	// Return default preferences
	UserPreferences prefs;
	prefs.userId = userId;
	prefs.enableProactiveHelp = true;
	prefs.enableCrossChat = true;
	prefs.minConfidenceForSuggestion = 70;
	prefs.lastUpdated = QDateTime::currentDateTime();

	return prefs;
}

void ContextAssistantBot::updateUserPreferences(qint64 userId, const UserPreferences &prefs) {
	_userPreferences.insert(userId, prefs);

	logInfo(QString("Updated preferences for user %1").arg(userId));

	// Persist to database via archiver
	if (archiver()) {
		QSqlDatabase db = archiver()->database();
		if (db.isOpen()) {
			QSqlQuery query(db);
			query.prepare(R"(
				INSERT OR REPLACE INTO user_preferences
				(user_id, enable_proactive, enable_cross_chat, min_confidence, updated_at)
				VALUES (:uid, :proactive, :cross_chat, :confidence, :updated)
			)");
			query.bindValue(":uid", userId);
			query.bindValue(":proactive", prefs.enableProactiveHelp ? 1 : 0);
			query.bindValue(":cross_chat", prefs.enableCrossChat ? 1 : 0);
			query.bindValue(":confidence", prefs.minConfidenceForSuggestion);
			query.bindValue(":updated", prefs.lastUpdated.toSecsSinceEpoch());
			query.exec();
		}
	}
}

bool ContextAssistantBot::isFeatureEnabledForUser(qint64 userId, const QString &feature) const {
	UserPreferences prefs = getUserPreferences(userId);

	if (feature == "proactive_help") {
		return prefs.enableProactiveHelp;
	} else if (feature == "cross_chat") {
		return prefs.enableCrossChat;
	}

	return true;
}

// NLP utilities

bool ContextAssistantBot::containsQuestion(const QString &text) const {
	QString lowerText = text.toLower().trimmed();

	// Check for question mark
	if (lowerText.contains("?")) {
		return true;
	}

	// Check for question words
	QStringList questionWords = {"what", "how", "why", "when", "where", "who", "which", "can", "could", "would", "should"};

	for (const QString &word : questionWords) {
		if (lowerText.startsWith(word + " ")) {
			return true;
		}
	}

	return false;
}

bool ContextAssistantBot::containsTaskKeywords(const QString &text) const {
	QString lowerText = text.toLower();

	QStringList taskKeywords = {
		"todo", "task", "need to", "have to", "must", "should do",
		"remember to", "don't forget", "make sure", "remind me"
	};

	for (const QString &keyword : taskKeywords) {
		if (lowerText.contains(keyword)) {
			return true;
		}
	}

	return false;
}

bool ContextAssistantBot::containsTimeReference(const QString &text) const {
	QString lowerText = text.toLower();

	QStringList timeKeywords = {
		"tomorrow", "today", "tonight", "morning", "afternoon", "evening",
		"monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday",
		"next week", "next month", "later", "soon", "schedule", "meeting"
	};

	for (const QString &keyword : timeKeywords) {
		if (lowerText.contains(keyword)) {
			return true;
		}
	}

	// Check for date/time patterns
	if (lowerText.contains(QRegularExpression("\\d{1,2}:\\d{2}"))) {
		return true;
	}

	if (lowerText.contains(QRegularExpression("\\d{1,2}[/-]\\d{1,2}"))) {
		return true;
	}

	return false;
}

QStringList ContextAssistantBot::tokenize(const QString &text) const {
	// Simple word tokenization
	return text.split(QRegularExpression("\\W+"), Qt::SkipEmptyParts);
}

double ContextAssistantBot::calculateSimilarity(const QString &text1, const QString &text2) const {
	// Simple Jaccard similarity
	QStringList tokens1 = tokenize(text1.toLower());
	QStringList tokens2 = tokenize(text2.toLower());

	QSet<QString> set1 = QSet<QString>(tokens1.begin(), tokens1.end());
	QSet<QString> set2 = QSet<QString>(tokens2.begin(), tokens2.end());

	QSet<QString> intersection = set1;
	intersection.intersect(set2);

	QSet<QString> unionSet = set1;
	unionSet.unite(set2);

	if (unionSet.isEmpty()) {
		return 0.0;
	}

	return static_cast<double>(intersection.size()) / unionSet.size();
}

} // namespace MCP
