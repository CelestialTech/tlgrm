// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtSql/QSqlDatabase>

namespace MCP {

class ChatArchiver;

// Embedding vector (typically 384 dimensions for all-MiniLM-L6-v2)
using EmbeddingVector = QVector<float>;

// Search result with similarity score
struct SearchResult {
	qint64 messageId;
	qint64 chatId;
	QString content;
	qint64 timestamp;
	QString username;
	float similarity;  // Cosine similarity 0.0-1.0
	QJsonObject messageData;
};

// Message cluster for topic grouping
struct MessageCluster {
	int clusterId;
	QString topicLabel;
	QVector<qint64> messageIds;
	QVector<QString> keyTerms;
	int messageCount;
	float cohesion;  // How tightly grouped the cluster is
};

// Intent classification for search/filtering
enum class SearchIntent {
	Question,      // "How do I...?", "What is...?"
	Answer,        // Direct responses
	Command,       // Bot commands, instructions
	Greeting,      // "Hello", "Hi"
	Farewell,      // "Bye", "See you"
	Agreement,     // "Yes", "I agree"
	Disagreement,  // "No", "I disagree"
	Statement,     // General declarative
	Other
};

// Entity types
enum class EntityType {
	UserMention,    // @username
	ChatMention,    // Chat references
	URL,            // Web links
	Email,          // Email addresses
	PhoneNumber,    // Phone numbers
	Hashtag,        // #topic
	BotCommand,     // /command
	CustomEmoji     // Telegram custom emojis
};

struct Entity {
	EntityType type;
	QString text;
	int offset;
	int length;
};

// Semantic search engine using embeddings
class SemanticSearch : public QObject {
	Q_OBJECT

public:
	explicit SemanticSearch(ChatArchiver *archiver, QObject *parent = nullptr);
	~SemanticSearch();

	// Initialization
	bool initialize(const QString &modelPath = QString());
	[[nodiscard]] bool isReady() const { return _isInitialized; }

	// Embedding generation
	EmbeddingVector generateEmbedding(const QString &text);
	bool storeEmbedding(qint64 messageId, qint64 chatId,
	                    const QString &content, const EmbeddingVector &embedding);

	// Index management
	bool indexMessage(qint64 messageId);
	bool indexChat(qint64 chatId, int limit = -1);
	bool indexAllChats();
	int getIndexedMessageCount() const;

	// Semantic search
	QVector<SearchResult> searchSimilar(
		const QString &query,
		qint64 chatId = 0,
		int limit = 10,
		float minSimilarity = 0.7
	);

	QVector<SearchResult> searchSimilarToMessage(
		qint64 messageId,
		int limit = 10,
		float minSimilarity = 0.7
	);

	// Clustering and topic detection
	QVector<MessageCluster> clusterMessages(
		const QVector<qint64> &messageIds,
		int numClusters = 5
	);

	QVector<MessageCluster> detectTopics(
		qint64 chatId,
		int numTopics = 5,
		const QDateTime &start = QDateTime(),
		const QDateTime &end = QDateTime()
	);

	// Intent classification
	SearchIntent classifyIntent(const QString &text);
	QJsonObject getIntentDistribution(qint64 chatId);

	// Entity extraction
	QVector<Entity> extractEntities(const QString &text);
	QJsonObject getEntityStatistics(qint64 chatId);

	// Export
	QJsonObject exportSearchResults(const QVector<SearchResult> &results);
	QJsonObject exportClusters(const QVector<MessageCluster> &clusters);

Q_SIGNALS:
	void indexingProgress(int current, int total);
	void indexingCompleted(int totalIndexed);
	void error(const QString &errorMessage);

private:
	// Similarity calculations
	float cosineSimilarity(const EmbeddingVector &a, const EmbeddingVector &b) const;
	float euclideanDistance(const EmbeddingVector &a, const EmbeddingVector &b) const;

	// Embedding storage/retrieval
	EmbeddingVector loadEmbedding(qint64 messageId) const;
	QVector<QPair<qint64, EmbeddingVector>> loadAllEmbeddings(qint64 chatId) const;

	// Clustering algorithms
	QVector<MessageCluster> kMeansClustering(
		const QVector<QPair<qint64, EmbeddingVector>> &data,
		int k
	);

	// Text preprocessing
	QString preprocessText(const QString &text) const;
	QStringList tokenize(const QString &text) const;

	// Intent detection helpers
	bool isQuestion(const QString &text) const;
	bool isCommand(const QString &text) const;
	bool isGreeting(const QString &text) const;
	bool isFarewell(const QString &text) const;

	// Entity extraction helpers
	QVector<Entity> extractUserMentions(const QString &text) const;
	QVector<Entity> extractURLs(const QString &text) const;
	QVector<Entity> extractHashtags(const QString &text) const;
	QVector<Entity> extractBotCommands(const QString &text) const;

	ChatArchiver *_archiver;
	bool _isInitialized = false;

	// Model configuration
	QString _modelPath;
	int _embeddingDimensions = 384;  // Default for all-MiniLM-L6-v2

	// NOTE: Actual embedding generation would require integration with:
	// - Python subprocess calling sentence-transformers
	// - ONNX Runtime for C++ inference
	// - External HTTP API (OpenAI, Cohere, etc.)
	// For now, this is a framework implementation
};

} // namespace MCP
