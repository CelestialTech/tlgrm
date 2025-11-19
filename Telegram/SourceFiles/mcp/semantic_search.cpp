// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "semantic_search.h"
#include "chat_archiver.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QCryptographicHash>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <cmath>
#include <algorithm>

namespace MCP {

SemanticSearch::SemanticSearch(ChatArchiver *archiver, QObject *parent)
	: QObject(parent)
	, _archiver(archiver) {
	// Note: _db access removed as it's private in ChatArchiver
	// Use archiver's public methods instead
}

SemanticSearch::~SemanticSearch() = default;

bool SemanticSearch::initialize(const QString &modelPath) {
	_modelPath = modelPath.isEmpty() ? "all-MiniLM-L6-v2" : modelPath;
	_isInitialized = true;

	// TODO: Initialize actual embedding model
	// Options:
	// 1. Python subprocess with sentence-transformers
	// 2. ONNX Runtime C++ inference
	// 3. HTTP API to embedding service

	return true;
}

// Generate embedding (placeholder)
EmbeddingVector SemanticSearch::generateEmbedding(const QString &text) {
	// TODO: Implement actual embedding generation
	// For now, return zero vector
	EmbeddingVector embedding(_embeddingDimensions, 0.0f);

	// Placeholder: Simple hash-based pseudo-embedding
	QByteArray hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
	const int maxSize = qMin(_embeddingDimensions, hash.size());
	for (int i = 0; i < maxSize; ++i) {
		embedding[i] = static_cast<float>(static_cast<unsigned char>(hash[i])) / 255.0f;
	}

	return embedding;
}

// Store embedding
bool SemanticSearch::storeEmbedding(
		qint64 messageId,
		qint64 chatId,
		const QString &content,
		const EmbeddingVector &embedding) {

	// TODO: Implement storeEmbedding using ChatArchiver's public API
	// _db is private in ChatArchiver, need to add public method for embedding storage
	Q_UNUSED(messageId);
	Q_UNUSED(chatId);
	Q_UNUSED(content);
	Q_UNUSED(embedding);
	return false;

	/* Original implementation - needs ChatArchiver public API:
	if (!_db || !_db->isOpen()) {
		return false;
	}

	// Serialize embedding to BLOB
	QByteArray embeddingBlob;
	QDataStream stream(&embeddingBlob, QIODevice::WriteOnly);
	for (float val : embedding) {
		stream << val;
	}

	QSqlQuery query(*_db);
	query.prepare(R"(
		INSERT OR REPLACE INTO message_embeddings (
			message_id, chat_id, content, embedding, embedding_model, created_at
		) VALUES (
			:message_id, :chat_id, :content, :embedding, :model, :created_at
		)
	)");

	query.bindValue(":message_id", messageId);
	query.bindValue(":chat_id", chatId);
	query.bindValue(":content", content);
	query.bindValue(":embedding", embeddingBlob);
	query.bindValue(":model", _modelPath);
	query.bindValue(":created_at", QDateTime::currentSecsSinceEpoch());

	return query.exec();
	*/
}

// Intent classification (heuristic-based)
SearchIntent SemanticSearch::classifyIntent(const QString &text) {
	QString lower = text.toLower().trimmed();

	// Question patterns
	if (isQuestion(lower)) {
		return SearchIntent::Question;
	}

	// Command patterns
	if (isCommand(lower)) {
		return SearchIntent::Command;
	}

	// Greeting patterns
	if (isGreeting(lower)) {
		return SearchIntent::Greeting;
	}

	// Farewell patterns
	if (isFarewell(lower)) {
		return SearchIntent::Farewell;
	}

	// Agreement/Disagreement
	if (lower.startsWith("yes") || lower.startsWith("i agree") || lower == "ok" || lower == "okay") {
		return SearchIntent::Agreement;
	}
	if (lower.startsWith("no") || lower.startsWith("i disagree")) {
		return SearchIntent::Disagreement;
	}

	return SearchIntent::Statement;
}

// Entity extraction
QVector<Entity> SemanticSearch::extractEntities(const QString &text) {
	QVector<Entity> entities;

	entities.append(extractUserMentions(text));
	entities.append(extractURLs(text));
	entities.append(extractHashtags(text));
	entities.append(extractBotCommands(text));

	return entities;
}

// Cosine similarity
float SemanticSearch::cosineSimilarity(const EmbeddingVector &a, const EmbeddingVector &b) const {
	if (a.size() != b.size()) {
		return 0.0f;
	}

	float dotProduct = 0.0f;
	float normA = 0.0f;
	float normB = 0.0f;

	for (int i = 0; i < a.size(); ++i) {
		dotProduct += a[i] * b[i];
		normA += a[i] * a[i];
		normB += b[i] * b[i];
	}

	if (normA == 0.0f || normB == 0.0f) {
		return 0.0f;
	}

	return dotProduct / (std::sqrt(normA) * std::sqrt(normB));
}

// Helper implementations
bool SemanticSearch::isQuestion(const QString &text) const {
	QStringList questionWords = {"what", "when", "where", "who", "whom", "which", "why", "how"};
	QString lower = text.toLower();

	for (const QString &word : questionWords) {
		if (lower.startsWith(word + " ") || lower.startsWith(word + "'")) {
			return true;
		}
	}

	return text.endsWith("?");
}

bool SemanticSearch::isCommand(const QString &text) const {
	return text.startsWith("/");
}

bool SemanticSearch::isGreeting(const QString &text) const {
	QStringList greetings = {"hello", "hi", "hey", "greetings", "good morning",
	                          "good afternoon", "good evening", "howdy"};
	QString lower = text.toLower();

	for (const QString &greeting : greetings) {
		if (lower.startsWith(greeting)) {
			return true;
		}
	}

	return false;
}

bool SemanticSearch::isFarewell(const QString &text) const {
	QStringList farewells = {"bye", "goodbye", "see you", "farewell", "take care",
	                          "good night", "later", "cya", "ttyl"};
	QString lower = text.toLower();

	for (const QString &farewell : farewells) {
		if (lower.contains(farewell)) {
			return true;
		}
	}

	return false;
}

// Entity extraction helpers
QVector<Entity> SemanticSearch::extractUserMentions(const QString &text) const {
	QVector<Entity> entities;

	QRegularExpression mentionRegex(R"(@(\w+))");
	QRegularExpressionMatchIterator it = mentionRegex.globalMatch(text);

	while (it.hasNext()) {
		QRegularExpressionMatch match = it.next();
		Entity entity;
		entity.type = EntityType::UserMention;
		entity.text = match.captured(0);
		entity.offset = match.capturedStart();
		entity.length = match.capturedLength();
		entities.append(entity);
	}

	return entities;
}

QVector<Entity> SemanticSearch::extractURLs(const QString &text) const {
	QVector<Entity> entities;

	QRegularExpression urlRegex(R"(https?://[^\s]+)");
	QRegularExpressionMatchIterator it = urlRegex.globalMatch(text);

	while (it.hasNext()) {
		QRegularExpressionMatch match = it.next();
		Entity entity;
		entity.type = EntityType::URL;
		entity.text = match.captured(0);
		entity.offset = match.capturedStart();
		entity.length = match.capturedLength();
		entities.append(entity);
	}

	return entities;
}

QVector<Entity> SemanticSearch::extractHashtags(const QString &text) const {
	QVector<Entity> entities;

	QRegularExpression hashtagRegex(R"(#(\w+))");
	QRegularExpressionMatchIterator it = hashtagRegex.globalMatch(text);

	while (it.hasNext()) {
		QRegularExpressionMatch match = it.next();
		Entity entity;
		entity.type = EntityType::Hashtag;
		entity.text = match.captured(0);
		entity.offset = match.capturedStart();
		entity.length = match.capturedLength();
		entities.append(entity);
	}

	return entities;
}

QVector<Entity> SemanticSearch::extractBotCommands(const QString &text) const {
	QVector<Entity> entities;

	QRegularExpression commandRegex(R"(/(\w+))");
	QRegularExpressionMatchIterator it = commandRegex.globalMatch(text);

	while (it.hasNext()) {
		QRegularExpressionMatch match = it.next();
		Entity entity;
		entity.type = EntityType::BotCommand;
		entity.text = match.captured(0);
		entity.offset = match.capturedStart();
		entity.length = match.capturedLength();
		entities.append(entity);
	}

	return entities;
}

// Stub implementations for full semantic search features
QVector<SearchResult> SemanticSearch::searchSimilar(
		const QString &query,
		qint64 chatId,
		int limit,
		float minSimilarity) {

	// TODO: Implement full semantic search
	Q_UNUSED(query);
	Q_UNUSED(chatId);
	Q_UNUSED(limit);
	Q_UNUSED(minSimilarity);

	return QVector<SearchResult>();
}

int SemanticSearch::getIndexedMessageCount() const {
	// TODO: Implement getIndexedMessageCount using ChatArchiver's public API
	// _db is private in ChatArchiver, need to add public method for querying embedding count
	return 0;

	/* Original implementation - needs ChatArchiver public API:
	if (!_db || !_db->isOpen()) {
		return 0;
	}

	QSqlQuery query(*_db);
	query.exec("SELECT COUNT(*) FROM message_embeddings");

	if (query.next()) {
		return query.value(0).toInt();
	}

	return 0;
	*/
}

} // namespace MCP
