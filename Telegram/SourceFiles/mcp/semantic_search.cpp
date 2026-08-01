// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "semantic_search.h"
#include "chat_archiver.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <cmath>
#include <algorithm>

namespace MCP {

// ===== STOP WORDS (filtered out during tokenization) =====

static const QSet<QString> &stopWords() {
	static const QSet<QString> words = {
		"a", "an", "the", "is", "are", "was", "were", "be", "been", "being",
		"have", "has", "had", "do", "does", "did", "will", "would", "could",
		"should", "may", "might", "shall", "can", "need", "dare", "ought",
		"used", "to", "of", "in", "for", "on", "with", "at", "by", "from",
		"as", "into", "through", "during", "before", "after", "above",
		"below", "between", "out", "off", "over", "under", "again",
		"further", "then", "once", "here", "there", "when", "where", "why",
		"how", "all", "both", "each", "few", "more", "most", "other",
		"some", "such", "no", "nor", "not", "only", "own", "same", "so",
		"than", "too", "very", "just", "because", "but", "and", "or", "if",
		"while", "this", "that", "these", "those", "i", "me", "my", "we",
		"our", "you", "your", "he", "him", "his", "she", "her", "it", "its",
		"they", "them", "their", "what", "which", "who", "whom"
	};
	return words;
}

// ===== CONSTRUCTOR / DESTRUCTOR =====

SemanticSearch::SemanticSearch(ChatArchiver *archiver, QObject *parent)
	: QObject(parent)
	, _archiver(archiver) {
}

SemanticSearch::~SemanticSearch() = default;

// ===== INITIALIZATION =====

bool SemanticSearch::initialize(const QString &modelPath) {
	_modelPath = modelPath.isEmpty() ? "tfidf-local" : modelPath;

	if (!_archiver) {
		return false;
	}

	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) {
		return false;
	}

	// Ensure message_embeddings table exists
	QSqlQuery q(db);
	q.exec(
		"CREATE TABLE IF NOT EXISTS message_embeddings ("
		"message_id INTEGER, "
		"chat_id INTEGER, "
		"content TEXT, "
		"embedding BLOB, "
		"embedding_model TEXT, "
		"created_at INTEGER, "
		"PRIMARY KEY (message_id, chat_id))"
	);

	// Build global vocabulary from existing messages for IDF computation
	buildVocabulary();

	_isInitialized = true;
	return true;
}

// ===== VOCABULARY / IDF =====

void SemanticSearch::buildVocabulary() {
	if (!_archiver) return;
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return;

	_vocabulary.clear();
	_idfScores.clear();

	// Sample messages to build vocabulary and IDF
	QSqlQuery q(db);
	q.exec("SELECT content FROM messages ORDER BY ROWID DESC LIMIT 10000");

	QHash<QString, int> docFreq;  // how many docs contain each term
	int totalDocs = 0;

	while (q.next()) {
		QString content = q.value(0).toString();
		QStringList tokens = tokenize(content);
		QSet<QString> uniqueTokens(tokens.begin(), tokens.end());

		for (const QString &token : uniqueTokens) {
			docFreq[token]++;
		}
		totalDocs++;
	}

	if (totalDocs == 0) {
		totalDocs = 1;
	}

	// Build vocabulary: top terms by document frequency (capped at embedding dimensions)
	QVector<QPair<int, QString>> sortedTerms;
	sortedTerms.reserve(docFreq.size());
	for (auto it = docFreq.begin(); it != docFreq.end(); ++it) {
		// Only include terms appearing in at least 2 documents
		if (it.value() >= 2) {
			sortedTerms.append({it.value(), it.key()});
		}
	}
	std::sort(sortedTerms.begin(), sortedTerms.end(), [](const auto &a, const auto &b) {
		return a.first > b.first;
	});

	int vocabSize = qMin(_embeddingDimensions, sortedTerms.size());
	for (int i = 0; i < vocabSize; ++i) {
		const QString &term = sortedTerms[i].second;
		_vocabulary[term] = i;
		// IDF = log(totalDocs / docFreq)
		_idfScores[term] = std::log(static_cast<float>(totalDocs) / static_cast<float>(sortedTerms[i].first));
	}
}

// ===== EMBEDDING GENERATION (TF-IDF) =====

EmbeddingVector SemanticSearch::generateEmbedding(const QString &text) {
	EmbeddingVector embedding(_embeddingDimensions, 0.0f);

	QStringList tokens = tokenize(text);
	if (tokens.isEmpty()) {
		return embedding;
	}

	// Compute term frequencies
	QHash<QString, int> tf;
	for (const QString &token : tokens) {
		tf[token]++;
	}

	float maxTf = 1.0f;
	for (auto it = tf.begin(); it != tf.end(); ++it) {
		if (it.value() > maxTf) maxTf = it.value();
	}

	// Build TF-IDF vector using vocabulary indices
	for (auto it = tf.begin(); it != tf.end(); ++it) {
		const QString &term = it.key();
		auto vocabIt = _vocabulary.find(term);
		if (vocabIt != _vocabulary.end()) {
			int idx = vocabIt.value();
			if (idx < _embeddingDimensions) {
				float tfNorm = 0.5f + 0.5f * (static_cast<float>(it.value()) / maxTf);
				float idf = _idfScores.value(term, 1.0f);
				embedding[idx] = tfNorm * idf;
			}
		}
	}

	// L2 normalize
	float norm = 0.0f;
	for (int i = 0; i < embedding.size(); ++i) {
		norm += embedding[i] * embedding[i];
	}
	if (norm > 0.0f) {
		norm = std::sqrt(norm);
		for (int i = 0; i < embedding.size(); ++i) {
			embedding[i] /= norm;
		}
	}

	return embedding;
}

// ===== EMBEDDING STORAGE =====

bool SemanticSearch::storeEmbedding(
		qint64 messageId,
		qint64 chatId,
		const QString &content,
		const EmbeddingVector &embedding) {

	if (!_archiver) return false;
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return false;

	// Serialize embedding to BLOB
	QByteArray embeddingBlob;
	QDataStream stream(&embeddingBlob, QIODevice::WriteOnly);
	for (float val : embedding) {
		stream << val;
	}

	QSqlQuery query(db);
	query.prepare(
		"INSERT OR REPLACE INTO message_embeddings "
		"(message_id, chat_id, content, embedding, embedding_model, created_at) "
		"VALUES (:message_id, :chat_id, :content, :embedding, :model, :created_at)"
	);

	query.bindValue(":message_id", messageId);
	query.bindValue(":chat_id", chatId);
	query.bindValue(":content", content);
	query.bindValue(":embedding", embeddingBlob);
	query.bindValue(":model", _modelPath);
	query.bindValue(":created_at", QDateTime::currentSecsSinceEpoch());

	return query.exec();
}

// ===== INDEXING =====

bool SemanticSearch::indexMessage(qint64 messageId) {
	if (!_archiver || !_isInitialized) return false;
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return false;

	// Look up message content from messages table
	QSqlQuery q(db);
	q.prepare("SELECT chat_id, content FROM messages WHERE message_id = :mid LIMIT 1");
	q.bindValue(":mid", messageId);
	if (!q.exec() || !q.next()) {
		return false;
	}

	qint64 chatId = q.value(0).toLongLong();
	QString content = q.value(1).toString();
	if (content.trimmed().isEmpty()) {
		return false;
	}

	EmbeddingVector emb = generateEmbedding(content);
	return storeEmbedding(messageId, chatId, content, emb);
}

bool SemanticSearch::indexChat(qint64 chatId, int limit) {
	if (!_archiver || !_isInitialized) return false;
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return false;

	// Get messages for this chat that aren't yet indexed
	QString sql =
		"SELECT m.message_id, m.content FROM messages m "
		"LEFT JOIN message_embeddings e ON m.message_id = e.message_id AND m.chat_id = e.chat_id "
		"WHERE m.chat_id = :cid AND e.message_id IS NULL AND m.content != '' ";
	if (limit > 0) {
		sql += "ORDER BY m.message_id DESC LIMIT " + QString::number(limit);
	}

	QSqlQuery q(db);
	q.prepare(sql);
	q.bindValue(":cid", chatId);
	if (!q.exec()) return false;

	int indexed = 0;
	int total = 0;

	while (q.next()) {
		total++;
		qint64 msgId = q.value(0).toLongLong();
		QString content = q.value(1).toString();
		if (content.trimmed().isEmpty()) continue;

		EmbeddingVector emb = generateEmbedding(content);
		if (storeEmbedding(msgId, chatId, content, emb)) {
			indexed++;
		}

		if (total % 100 == 0) {
			Q_EMIT indexingProgress(total, total);  // total unknown, emit current
		}
	}

	Q_EMIT indexingCompleted(indexed);
	return true;
}

bool SemanticSearch::indexAllChats() {
	if (!_archiver || !_isInitialized) return false;
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return false;

	// Get all distinct chat IDs
	QSqlQuery q(db);
	q.exec("SELECT DISTINCT chat_id FROM messages");

	QVector<qint64> chatIds;
	while (q.next()) {
		chatIds.append(q.value(0).toLongLong());
	}

	for (qint64 chatId : chatIds) {
		indexChat(chatId, -1);
	}

	return true;
}

// ===== SEMANTIC SEARCH =====

QVector<SearchResult> SemanticSearch::searchSimilar(
		const QString &query,
		qint64 chatId,
		int limit,
		float minSimilarity) {

	QVector<SearchResult> results;
	if (!_archiver || !_isInitialized) return results;

	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return results;

	// Generate query embedding
	EmbeddingVector queryEmb = generateEmbedding(query);

	// Load all embeddings for the target chat (or all chats)
	auto candidates = loadAllEmbeddings(chatId);

	// Compute similarities
	QVector<QPair<float, int>> scored;
	scored.reserve(candidates.size());
	for (int i = 0; i < candidates.size(); ++i) {
		float sim = cosineSimilarity(queryEmb, candidates[i].second);
		if (sim >= minSimilarity) {
			scored.append({sim, i});
		}
	}

	// Sort by similarity descending
	std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
		return a.first > b.first;
	});

	// Build results
	int count = qMin(limit, scored.size());
	for (int i = 0; i < count; ++i) {
		int idx = scored[i].second;
		qint64 msgId = candidates[idx].first;

		// Look up message details
		QSqlQuery q(db);
		q.prepare(
			"SELECT e.chat_id, e.content, m.timestamp, m.sender_name "
			"FROM message_embeddings e "
			"LEFT JOIN messages m ON e.message_id = m.message_id AND e.chat_id = m.chat_id "
			"WHERE e.message_id = :mid LIMIT 1"
		);
		q.bindValue(":mid", msgId);
		if (q.exec() && q.next()) {
			SearchResult sr;
			sr.messageId = msgId;
			sr.chatId = q.value(0).toLongLong();
			sr.content = q.value(1).toString();
			sr.timestamp = q.value(2).toLongLong();
			sr.username = q.value(3).toString();
			sr.similarity = scored[i].first;
			results.append(sr);
		}
	}

	return results;
}

QVector<SearchResult> SemanticSearch::searchSimilarToMessage(
		qint64 messageId,
		int limit,
		float minSimilarity) {

	if (!_archiver || !_isInitialized) return QVector<SearchResult>();

	// Load the source message's embedding
	EmbeddingVector sourceEmb = loadEmbedding(messageId);
	if (sourceEmb.isEmpty()) {
		// Not indexed yet — try to index it first
		indexMessage(messageId);
		sourceEmb = loadEmbedding(messageId);
		if (sourceEmb.isEmpty()) {
			return QVector<SearchResult>();
		}
	}

	// Look up chatId for context
	QSqlDatabase db = _archiver->database();
	QSqlQuery q(db);
	q.prepare("SELECT chat_id, content FROM message_embeddings WHERE message_id = :mid LIMIT 1");
	q.bindValue(":mid", messageId);
	qint64 chatId = 0;
	QString content;
	if (q.exec() && q.next()) {
		chatId = q.value(0).toLongLong();
		content = q.value(1).toString();
	}

	// Search using the embedding's text (reuse searchSimilar logic)
	auto candidates = loadAllEmbeddings(chatId);

	QVector<QPair<float, int>> scored;
	for (int i = 0; i < candidates.size(); ++i) {
		if (candidates[i].first == messageId) continue;  // Skip self
		float sim = cosineSimilarity(sourceEmb, candidates[i].second);
		if (sim >= minSimilarity) {
			scored.append({sim, i});
		}
	}

	std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
		return a.first > b.first;
	});

	QVector<SearchResult> results;
	int count = qMin(limit, scored.size());
	for (int i = 0; i < count; ++i) {
		int idx = scored[i].second;
		qint64 msgId = candidates[idx].first;

		QSqlQuery rq(db);
		rq.prepare(
			"SELECT e.chat_id, e.content, m.timestamp, m.sender_name "
			"FROM message_embeddings e "
			"LEFT JOIN messages m ON e.message_id = m.message_id AND e.chat_id = m.chat_id "
			"WHERE e.message_id = :mid LIMIT 1"
		);
		rq.bindValue(":mid", msgId);
		if (rq.exec() && rq.next()) {
			SearchResult sr;
			sr.messageId = msgId;
			sr.chatId = rq.value(0).toLongLong();
			sr.content = rq.value(1).toString();
			sr.timestamp = rq.value(2).toLongLong();
			sr.username = rq.value(3).toString();
			sr.similarity = scored[i].first;
			results.append(sr);
		}
	}

	return results;
}

// ===== CLUSTERING (K-MEANS) =====

QVector<MessageCluster> SemanticSearch::clusterMessages(
		const QVector<qint64> &messageIds,
		int numClusters) {

	// Load embeddings for the specified messages
	QVector<QPair<qint64, EmbeddingVector>> data;
	for (qint64 msgId : messageIds) {
		EmbeddingVector emb = loadEmbedding(msgId);
		if (!emb.isEmpty()) {
			data.append({msgId, emb});
		}
	}

	if (data.size() < numClusters) {
		numClusters = qMax(1, data.size());
	}

	return kMeansClustering(data, numClusters);
}

QVector<MessageCluster> SemanticSearch::detectTopics(
		qint64 chatId,
		int numTopics,
		const QDateTime &start,
		const QDateTime &end) {

	if (!_archiver) return QVector<MessageCluster>();
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return QVector<MessageCluster>();

	// Get message IDs for this chat in the time range
	QString sql = "SELECT message_id FROM messages WHERE chat_id = :cid";
	if (start.isValid()) {
		sql += " AND timestamp >= " + QString::number(start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		sql += " AND timestamp <= " + QString::number(end.toSecsSinceEpoch());
	}
	sql += " ORDER BY message_id DESC LIMIT 1000";

	QSqlQuery q(db);
	q.prepare(sql);
	q.bindValue(":cid", chatId);
	if (!q.exec()) return QVector<MessageCluster>();

	QVector<qint64> messageIds;
	while (q.next()) {
		messageIds.append(q.value(0).toLongLong());
	}

	return clusterMessages(messageIds, numTopics);
}

QVector<MessageCluster> SemanticSearch::kMeansClustering(
		const QVector<QPair<qint64, EmbeddingVector>> &data,
		int k) {

	QVector<MessageCluster> clusters;
	if (data.isEmpty() || k <= 0) return clusters;

	int dim = _embeddingDimensions;
	int n = data.size();

	// Initialize centroids (pick first k points)
	QVector<EmbeddingVector> centroids;
	centroids.reserve(k);
	int step = qMax(1, n / k);
	for (int i = 0; i < k && i * step < n; ++i) {
		centroids.append(data[i * step].second);
	}
	// Fill remaining if needed
	while (centroids.size() < k) {
		centroids.append(data[0].second);
	}

	// Assignments
	QVector<int> assignments(n, 0);

	// Run K-means for 20 iterations
	for (int iter = 0; iter < 20; ++iter) {
		// Assign each point to nearest centroid
		bool changed = false;
		for (int i = 0; i < n; ++i) {
			float bestSim = -1.0f;
			int bestK = 0;
			for (int c = 0; c < k; ++c) {
				float sim = cosineSimilarity(data[i].second, centroids[c]);
				if (sim > bestSim) {
					bestSim = sim;
					bestK = c;
				}
			}
			if (assignments[i] != bestK) {
				assignments[i] = bestK;
				changed = true;
			}
		}

		if (!changed) break;

		// Recompute centroids
		for (int c = 0; c < k; ++c) {
			EmbeddingVector newCentroid(dim, 0.0f);
			int count = 0;
			for (int i = 0; i < n; ++i) {
				if (assignments[i] == c) {
					for (int d = 0; d < dim; ++d) {
						newCentroid[d] += data[i].second[d];
					}
					count++;
				}
			}
			if (count > 0) {
				for (int d = 0; d < dim; ++d) {
					newCentroid[d] /= count;
				}
			}
			centroids[c] = newCentroid;
		}
	}

	// Build cluster results
	for (int c = 0; c < k; ++c) {
		MessageCluster cluster;
		cluster.clusterId = c;
		cluster.messageCount = 0;

		// Collect message IDs and compute cohesion
		float cohesionSum = 0.0f;
		QHash<QString, int> termFreq;

		for (int i = 0; i < n; ++i) {
			if (assignments[i] == c) {
				cluster.messageIds.append(data[i].first);
				cluster.messageCount++;
				cohesionSum += cosineSimilarity(data[i].second, centroids[c]);

				// Collect terms for key terms extraction
				if (_archiver) {
					QSqlDatabase db = _archiver->database();
					QSqlQuery q(db);
					q.prepare("SELECT content FROM message_embeddings WHERE message_id = :mid LIMIT 1");
					q.bindValue(":mid", data[i].first);
					if (q.exec() && q.next()) {
						QStringList tokens = tokenize(q.value(0).toString());
						for (const QString &t : tokens) {
							termFreq[t]++;
						}
					}
				}
			}
		}

		cluster.cohesion = (cluster.messageCount > 0)
			? (cohesionSum / cluster.messageCount) : 0.0f;

		// Extract top 5 key terms
		QVector<QPair<int, QString>> sorted;
		for (auto it = termFreq.begin(); it != termFreq.end(); ++it) {
			sorted.append({it.value(), it.key()});
		}
		std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
			return a.first > b.first;
		});
		for (int i = 0; i < qMin(5, sorted.size()); ++i) {
			cluster.keyTerms.append(sorted[i].second);
		}
		cluster.topicLabel = cluster.keyTerms.isEmpty()
			? QString("Topic %1").arg(c)
			: cluster.keyTerms.first();

		clusters.append(cluster);
	}

	return clusters;
}

// ===== INTENT / ENTITY STATISTICS =====

QJsonObject SemanticSearch::getIntentDistribution(qint64 chatId) {
	QJsonObject result;
	if (!_archiver) return result;
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return result;

	QSqlQuery q(db);
	q.prepare("SELECT content FROM messages WHERE chat_id = :cid ORDER BY ROWID DESC LIMIT 500");
	q.bindValue(":cid", chatId);
	if (!q.exec()) return result;

	QHash<QString, int> intentCounts;
	int total = 0;
	while (q.next()) {
		QString content = q.value(0).toString();
		SearchIntent intent = classifyIntent(content);
		QString intentName;
		switch (intent) {
			case SearchIntent::Question: intentName = "question"; break;
			case SearchIntent::Answer: intentName = "answer"; break;
			case SearchIntent::Command: intentName = "command"; break;
			case SearchIntent::Greeting: intentName = "greeting"; break;
			case SearchIntent::Farewell: intentName = "farewell"; break;
			case SearchIntent::Agreement: intentName = "agreement"; break;
			case SearchIntent::Disagreement: intentName = "disagreement"; break;
			case SearchIntent::Statement: intentName = "statement"; break;
			default: intentName = "other"; break;
		}
		intentCounts[intentName]++;
		total++;
	}

	for (auto it = intentCounts.begin(); it != intentCounts.end(); ++it) {
		result[it.key()] = it.value();
	}
	result["total"] = total;

	return result;
}

QJsonObject SemanticSearch::getEntityStatistics(qint64 chatId) {
	QJsonObject result;
	if (!_archiver) return result;
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return result;

	QSqlQuery q(db);
	q.prepare("SELECT content FROM messages WHERE chat_id = :cid ORDER BY ROWID DESC LIMIT 500");
	q.bindValue(":cid", chatId);
	if (!q.exec()) return result;

	int mentions = 0, urls = 0, hashtags = 0, commands = 0;
	while (q.next()) {
		QString content = q.value(0).toString();
		auto entities = extractEntities(content);
		for (const auto &e : entities) {
			switch (e.type) {
				case EntityType::UserMention: mentions++; break;
				case EntityType::URL: urls++; break;
				case EntityType::Hashtag: hashtags++; break;
				case EntityType::BotCommand: commands++; break;
				default: break;
			}
		}
	}

	result["user_mentions"] = mentions;
	result["urls"] = urls;
	result["hashtags"] = hashtags;
	result["bot_commands"] = commands;
	result["total"] = mentions + urls + hashtags + commands;

	return result;
}

// ===== EXPORT =====

QJsonObject SemanticSearch::exportSearchResults(const QVector<SearchResult> &results) {
	QJsonObject obj;
	QJsonArray arr;
	for (const auto &r : results) {
		QJsonObject item;
		item["message_id"] = r.messageId;
		item["chat_id"] = r.chatId;
		item["content"] = r.content;
		item["timestamp"] = r.timestamp;
		item["username"] = r.username;
		item["similarity"] = static_cast<double>(r.similarity);
		arr.append(item);
	}
	obj["results"] = arr;
	obj["count"] = arr.size();
	return obj;
}

QJsonObject SemanticSearch::exportClusters(const QVector<MessageCluster> &clusters) {
	QJsonObject obj;
	QJsonArray arr;
	for (const auto &c : clusters) {
		QJsonObject item;
		item["cluster_id"] = c.clusterId;
		item["topic_label"] = c.topicLabel;
		item["message_count"] = c.messageCount;
		item["cohesion"] = static_cast<double>(c.cohesion);

		QJsonArray terms;
		for (const QString &t : c.keyTerms) {
			terms.append(t);
		}
		item["key_terms"] = terms;

		QJsonArray ids;
		for (qint64 id : c.messageIds) {
			ids.append(id);
		}
		item["message_ids"] = ids;

		arr.append(item);
	}
	obj["clusters"] = arr;
	obj["count"] = arr.size();
	return obj;
}

// ===== EMBEDDING LOAD HELPERS =====

EmbeddingVector SemanticSearch::loadEmbedding(qint64 messageId) const {
	if (!_archiver) return EmbeddingVector();
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return EmbeddingVector();

	QSqlQuery q(db);
	q.prepare("SELECT embedding FROM message_embeddings WHERE message_id = :mid LIMIT 1");
	q.bindValue(":mid", messageId);
	if (!q.exec() || !q.next()) {
		return EmbeddingVector();
	}

	QByteArray blob = q.value(0).toByteArray();
	EmbeddingVector embedding;
	QDataStream stream(blob);
	while (!stream.atEnd()) {
		float val;
		stream >> val;
		embedding.append(val);
	}

	return embedding;
}

QVector<QPair<qint64, EmbeddingVector>> SemanticSearch::loadAllEmbeddings(qint64 chatId) const {
	QVector<QPair<qint64, EmbeddingVector>> result;
	if (!_archiver) return result;
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return result;

	QSqlQuery q(db);
	if (chatId > 0) {
		q.prepare("SELECT message_id, embedding FROM message_embeddings WHERE chat_id = :cid");
		q.bindValue(":cid", chatId);
	} else {
		q.prepare("SELECT message_id, embedding FROM message_embeddings");
	}

	if (!q.exec()) return result;

	while (q.next()) {
		qint64 msgId = q.value(0).toLongLong();
		QByteArray blob = q.value(1).toByteArray();

		EmbeddingVector embedding;
		QDataStream stream(blob);
		while (!stream.atEnd()) {
			float val;
			stream >> val;
			embedding.append(val);
		}

		result.append({msgId, embedding});
	}

	return result;
}

// ===== INDEXED COUNT =====

int SemanticSearch::getIndexedMessageCount() const {
	if (!_archiver) return 0;
	QSqlDatabase db = _archiver->database();
	if (!db.isOpen()) return 0;

	QSqlQuery q(db);
	q.exec("SELECT COUNT(*) FROM message_embeddings");
	if (q.next()) {
		return q.value(0).toInt();
	}
	return 0;
}

// ===== INTENT CLASSIFICATION (heuristic-based) =====

SearchIntent SemanticSearch::classifyIntent(const QString &text) {
	QString lower = text.toLower().trimmed();

	if (isQuestion(lower)) return SearchIntent::Question;
	if (isCommand(lower)) return SearchIntent::Command;
	if (isGreeting(lower)) return SearchIntent::Greeting;
	if (isFarewell(lower)) return SearchIntent::Farewell;

	if (lower.startsWith("yes") || lower.startsWith("i agree") || lower == "ok" || lower == "okay") {
		return SearchIntent::Agreement;
	}
	if (lower.startsWith("no") || lower.startsWith("i disagree")) {
		return SearchIntent::Disagreement;
	}

	return SearchIntent::Statement;
}

// ===== ENTITY EXTRACTION =====

QVector<Entity> SemanticSearch::extractEntities(const QString &text) {
	QVector<Entity> entities;
	entities.append(extractUserMentions(text));
	entities.append(extractURLs(text));
	entities.append(extractHashtags(text));
	entities.append(extractBotCommands(text));
	return entities;
}

// ===== SIMILARITY =====

float SemanticSearch::cosineSimilarity(const EmbeddingVector &a, const EmbeddingVector &b) const {
	if (a.size() != b.size() || a.isEmpty()) {
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

float SemanticSearch::euclideanDistance(const EmbeddingVector &a, const EmbeddingVector &b) const {
	if (a.size() != b.size()) return 0.0f;
	float sum = 0.0f;
	for (int i = 0; i < a.size(); ++i) {
		float diff = a[i] - b[i];
		sum += diff * diff;
	}
	return std::sqrt(sum);
}

// ===== TEXT PREPROCESSING =====

QString SemanticSearch::preprocessText(const QString &text) const {
	QString cleaned = text.toLower().trimmed();
	// Remove URLs
	cleaned.replace(QRegularExpression(R"(https?://\S+)"), " ");
	// Remove non-alphanumeric except spaces
	cleaned.replace(QRegularExpression(R"([^\w\s])"), " ");
	// Collapse whitespace
	cleaned.replace(QRegularExpression(R"(\s+)"), " ");
	return cleaned.trimmed();
}

QStringList SemanticSearch::tokenize(const QString &text) const {
	QString cleaned = preprocessText(text);
	QStringList tokens = cleaned.split(' ', Qt::SkipEmptyParts);

	// Remove stop words and short tokens
	QStringList filtered;
	for (const QString &t : tokens) {
		if (t.length() >= 2 && !stopWords().contains(t)) {
			filtered.append(t);
		}
	}

	return filtered;
}

// ===== HELPER IMPLEMENTATIONS =====

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

// ===== ENTITY EXTRACTION HELPERS =====

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

} // namespace MCP
