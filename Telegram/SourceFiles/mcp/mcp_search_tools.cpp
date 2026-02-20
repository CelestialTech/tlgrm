// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== SEMANTIC SEARCH TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolSemanticSearch(const QJsonObject &args) {
	QString query = args["query"].toString();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(10);
	float minSimilarity = args.value("min_similarity").toDouble(0.7);

	if (!_semanticSearch) {
		QJsonObject result;
		result["error"] = "Semantic search not available";
		result["query"] = query;
		return result;
	}

	auto results = _semanticSearch->searchSimilar(query, chatId, limit, minSimilarity);

	QJsonArray matches;
	for (const auto &result : results) {
		QJsonObject match;
		match["message_id"] = result.messageId;
		match["chat_id"] = result.chatId;
		match["content"] = result.content;
		match["similarity"] = result.similarity;
		matches.append(match);
	}

	QJsonObject result;
	result["query"] = query;
	result["results"] = matches;
	result["count"] = matches.size();

	return result;
}

QJsonObject Server::toolIndexMessages(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(1000);
	bool rebuild = args.value("rebuild").toBool(false);

	QJsonObject result;
	result["chat_id"] = chatId;
	result["requested_limit"] = limit;

	// Create FTS table if not exists
	QSqlQuery createFts(_db);
	bool tableCreated = createFts.exec(
		"CREATE VIRTUAL TABLE IF NOT EXISTS message_fts USING fts5("
		"chat_id, message_id, text, sender_name, timestamp, "
		"content='', contentless_delete=1"
		")");

	if (!tableCreated) {
		result["success"] = false;
		result["error"] = QString("Failed to create FTS table: %1").arg(createFts.lastError().text());
		return result;
	}

	// If rebuild, clear existing index for this chat
	if (rebuild) {
		QSqlQuery clearQuery(_db);
		clearQuery.prepare("DELETE FROM message_fts WHERE chat_id = ?");
		clearQuery.addBindValue(QString::number(chatId));
		clearQuery.exec();
	}

	int indexed = 0;

	// Try to index from history blocks (live session data)
	if (_session && chatId > 0) {
		auto peer = _session->data().peer(PeerId(chatId));
		if (peer) {
			auto history = _session->data().history(peer);
			if (history) {
				_db.transaction();
				QSqlQuery insertQuery(_db);
				insertQuery.prepare(
					"INSERT OR IGNORE INTO message_fts(chat_id, message_id, text, sender_name, timestamp) "
					"VALUES(?, ?, ?, ?, ?)");

				for (const auto &block : history->blocks) {
					for (const auto &view : block->messages) {
						if (indexed >= limit) break;
						auto *item = view->data();
						if (!item) continue;

						QString text = item->originalText().text;
						if (text.isEmpty()) continue;

						auto from = item->from();
						QString senderName = from ? from->name() : QString();

						insertQuery.addBindValue(QString::number(chatId));
						insertQuery.addBindValue(QString::number(item->id.bare));
						insertQuery.addBindValue(text);
						insertQuery.addBindValue(senderName);
						insertQuery.addBindValue(QString::number(item->date()));
						insertQuery.exec();
						insertQuery.finish();

						indexed++;
					}
					if (indexed >= limit) break;
				}
				_db.commit();
			}
		}
	}

	// Also index from archived messages in the database
	if (indexed < limit) {
		QSqlQuery archiveQuery(_db);
		if (chatId > 0) {
			archiveQuery.prepare(
				"SELECT message_id, content, username, timestamp FROM messages "
				"WHERE chat_id = :chat_id ORDER BY timestamp DESC LIMIT :limit");
			archiveQuery.bindValue(":chat_id", chatId);
		} else {
			archiveQuery.prepare(
				"SELECT message_id, content, username, timestamp, chat_id FROM messages "
				"ORDER BY timestamp DESC LIMIT :limit");
		}
		archiveQuery.bindValue(":limit", limit - indexed);

		if (archiveQuery.exec()) {
			_db.transaction();
			QSqlQuery insertQuery(_db);
			insertQuery.prepare(
				"INSERT OR IGNORE INTO message_fts(chat_id, message_id, text, sender_name, timestamp) "
				"VALUES(?, ?, ?, ?, ?)");

			while (archiveQuery.next()) {
				QString text = archiveQuery.value("content").toString();
				if (text.isEmpty()) continue;

				QString cid = (chatId > 0)
					? QString::number(chatId)
					: archiveQuery.value("chat_id").toString();

				insertQuery.addBindValue(cid);
				insertQuery.addBindValue(archiveQuery.value("message_id").toString());
				insertQuery.addBindValue(text);
				insertQuery.addBindValue(archiveQuery.value("username").toString());
				insertQuery.addBindValue(archiveQuery.value("timestamp").toString());
				insertQuery.exec();
				insertQuery.finish();

				indexed++;
			}
			_db.commit();
		}
	}

	// Also trigger semantic search indexing if available
	if (_semanticSearch && chatId > 0) {
		_semanticSearch->indexChat(chatId, limit);
	}

	result["success"] = true;
	result["table_ready"] = tableCreated;
	result["indexed_count"] = indexed;
	result["method"] = "sqlite_fts5";

	return result;
}

QJsonObject Server::toolDetectTopics(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int numTopics = args.value("num_topics").toInt(5);
	int messageLimit = args.value("message_limit").toInt(500);

	QJsonObject result;
	result["chat_id"] = chatId;
	result["requested_topics"] = numTopics;

	// Common stop words to filter out
	static const QSet<QString> stopWords = {
		"the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for",
		"of", "with", "by", "from", "is", "are", "was", "were", "be", "been",
		"being", "have", "has", "had", "do", "does", "did", "will", "would",
		"could", "should", "may", "might", "must", "shall", "can", "need",
		"this", "that", "these", "those", "it", "its", "i", "you", "he", "she",
		"we", "they", "me", "him", "her", "us", "them", "my", "your", "his",
		"our", "their", "what", "which", "who", "whom", "when", "where", "why",
		"how", "all", "each", "every", "both", "few", "more", "most", "other",
		"some", "such", "no", "not", "only", "same", "so", "than", "too",
		"very", "just", "also", "now", "here", "there", "then", "about"
	};

	// Note: Full message iteration through history->blocks() requires complex API
	// integration. Topic detection will use the FTS index when available.

	// Check if FTS table exists and has data for this chat
	QSqlQuery checkQuery(_db);
	checkQuery.prepare("SELECT COUNT(*) FROM message_fts WHERE chat_id = ?");
	checkQuery.addBindValue(QString::number(chatId));

	int indexedCount = 0;
	if (checkQuery.exec() && checkQuery.next()) {
		indexedCount = checkQuery.value(0).toInt();
	}

	QJsonArray topics;

	// Try semantic search clustering first (best quality)
	if (_semanticSearch && _semanticSearch->isReady()) {
		auto clusters = _semanticSearch->detectTopics(chatId, numTopics);
		if (!clusters.isEmpty()) {
			QJsonObject exportedClusters = _semanticSearch->exportClusters(clusters);
			result["success"] = true;
			result["topics"] = exportedClusters["clusters"].toArray();
			result["method"] = "semantic_clustering";
			result["indexed_messages"] = indexedCount;
			return result;
		}
	}

	// Fall back to keyword frequency analysis from FTS index
	if (indexedCount > 0) {
		// Extract top words from indexed messages
		QSqlQuery wordQuery(_db);
		wordQuery.prepare("SELECT text FROM message_fts WHERE chat_id = ? LIMIT ?");
		wordQuery.addBindValue(QString::number(chatId));
		wordQuery.addBindValue(messageLimit);

		QHash<QString, int> wordFreq;
		if (wordQuery.exec()) {
			while (wordQuery.next()) {
				QString text = wordQuery.value(0).toString().toLower();
				QStringList words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
				for (const QString &word : words) {
					if (word.length() >= 4 && !stopWords.contains(word)) {
						wordFreq[word]++;
					}
				}
			}
		}

		// Sort by frequency and group into topics
		QVector<QPair<QString, int>> sorted;
		for (auto it = wordFreq.constBegin(); it != wordFreq.constEnd(); ++it) {
			sorted.append({it.key(), it.value()});
		}
		std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
			return a.second > b.second;
		});

		// Take top N*3 words and group into N topics
		int wordsPerTopic = 3;
		for (int t = 0; t < numTopics && t * wordsPerTopic < sorted.size(); ++t) {
			QJsonObject topic;
			topic["topic_id"] = t;

			QJsonArray keyTerms;
			for (int w = 0; w < wordsPerTopic && (t * wordsPerTopic + w) < sorted.size(); ++w) {
				int idx = t * wordsPerTopic + w;
				keyTerms.append(sorted[idx].first);
			}
			topic["key_terms"] = keyTerms;
			topic["label"] = sorted[t * wordsPerTopic].first;
			topics.append(topic);
		}

		result["indexed_messages"] = indexedCount;
	} else {
		result["indexed_messages"] = 0;
		result["note"] = "No indexed messages for this chat. Use index_messages first.";
	}

	result["success"] = true;
	result["topics"] = topics;
	result["method"] = "keyword_frequency";

	return result;
}

QJsonObject Server::toolClassifyIntent(const QJsonObject &args) {
	QString text = args["text"].toString();

	if (!_semanticSearch) {
		QJsonObject result;
		result["error"] = "Semantic search not available";
		result["text"] = text;
		return result;
	}

	SearchIntent intent = _semanticSearch->classifyIntent(text);

	QString intentStr;
	switch (intent) {
		case SearchIntent::Question: intentStr = "question"; break;
		case SearchIntent::Answer: intentStr = "answer"; break;
		case SearchIntent::Statement: intentStr = "statement"; break;
		case SearchIntent::Command: intentStr = "command"; break;
		case SearchIntent::Greeting: intentStr = "greeting"; break;
		case SearchIntent::Farewell: intentStr = "farewell"; break;
		case SearchIntent::Agreement: intentStr = "agreement"; break;
		case SearchIntent::Disagreement: intentStr = "disagreement"; break;
		default: intentStr = "other"; break;
	}

	QJsonObject result;
	result["text"] = text;
	result["intent"] = intentStr;

	return result;
}

QJsonObject Server::toolExtractEntities(const QJsonObject &args) {
	QString text = args["text"].toString();

	if (!_semanticSearch) {
		QJsonObject result;
		result["error"] = "Semantic search not available";
		result["text"] = text;
		return result;
	}

	auto entities = _semanticSearch->extractEntities(text);

	QJsonArray entitiesArray;
	for (const auto &entity : entities) {
		QJsonObject e;

		QString typeStr;
		switch (entity.type) {
			case EntityType::UserMention: typeStr = "user_mention"; break;
			case EntityType::ChatMention: typeStr = "chat_mention"; break;
			case EntityType::URL: typeStr = "url"; break;
			case EntityType::Email: typeStr = "email"; break;
			case EntityType::PhoneNumber: typeStr = "phone_number"; break;
			case EntityType::Hashtag: typeStr = "hashtag"; break;
			case EntityType::BotCommand: typeStr = "bot_command"; break;
			case EntityType::CustomEmoji: typeStr = "custom_emoji"; break;
			default: typeStr = "unknown"; break;
		}

		e["type"] = typeStr;
		e["text"] = entity.text;
		e["offset"] = entity.offset;
		e["length"] = entity.length;
		entitiesArray.append(e);
	}

	QJsonObject result;
	result["text"] = text;
	result["entities"] = entitiesArray;
	result["count"] = entitiesArray.size();

	return result;
}


} // namespace MCP
