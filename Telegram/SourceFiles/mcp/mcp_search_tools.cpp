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

	if (_semanticSearch) {
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

	// Fallback: FTS5 full-text search on indexed messages
	QJsonObject result;
	result["query"] = query;

	QJsonArray matches;

	// Try FTS5 index first
	QSqlQuery ftsQuery(_db);
	QString fts = "SELECT chat_id, message_id, text, sender_name, timestamp, "
		"rank FROM message_fts WHERE message_fts MATCH ?";
	if (chatId > 0) {
		fts += " AND chat_id = '" + QString::number(chatId) + "'";
	}
	fts += " ORDER BY rank LIMIT " + QString::number(limit);

	ftsQuery.prepare(fts);
	// FTS5 match syntax: escape special chars, wrap in quotes for phrase search
	QString ftsQuery_str = query;
	ftsQuery_str.replace("\"", "");
	ftsQuery.addBindValue("\"" + ftsQuery_str + "\"");

	bool ftsWorked = false;
	if (ftsQuery.exec()) {
		while (ftsQuery.next()) {
			QJsonObject match;
			match["chat_id"] = ftsQuery.value(0).toString();
			match["message_id"] = ftsQuery.value(1).toString();
			match["content"] = ftsQuery.value(2).toString();
			match["sender"] = ftsQuery.value(3).toString();
			match["timestamp"] = ftsQuery.value(4).toString();
			match["rank"] = ftsQuery.value(5).toDouble();
			matches.append(match);
			ftsWorked = true;
		}
	}

	// If FTS didn't work or returned nothing, try LIKE search on messages table
	if (!ftsWorked || matches.isEmpty()) {
		QSqlQuery likeQuery(_db);
		QString sql = "SELECT chat_id, message_id, content, username, timestamp "
			"FROM messages WHERE content LIKE ?";
		if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
		sql += " ORDER BY timestamp DESC LIMIT " + QString::number(limit);

		likeQuery.prepare(sql);
		likeQuery.addBindValue("%" + query + "%");

		if (likeQuery.exec()) {
			while (likeQuery.next()) {
				QJsonObject match;
				match["chat_id"] = likeQuery.value(0).toString();
				match["message_id"] = likeQuery.value(1).toString();
				match["content"] = likeQuery.value(2).toString();
				match["sender"] = likeQuery.value(3).toString();
				match["timestamp"] = likeQuery.value(4).toString();
				matches.append(match);
			}
		}
		result["method"] = "like_search";
	} else {
		result["method"] = "fts5";
	}

	result["results"] = matches;
	result["count"] = matches.size();
	result["success"] = true;
	result["source"] = "local_db";

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
						const auto item = view->data();

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

	// Fall back to keyword frequency analysis
	// Use FTS index if available, otherwise fall back to messages table directly
	QSqlQuery wordQuery(_db);
	bool usingFts = false;
	if (indexedCount > 0) {
		wordQuery.prepare("SELECT text FROM message_fts WHERE chat_id = ? LIMIT ?");
		wordQuery.addBindValue(QString::number(chatId));
		wordQuery.addBindValue(messageLimit);
		usingFts = true;
	} else {
		// Directly read from messages table
		QString sql = "SELECT content FROM messages WHERE content != '' AND content IS NOT NULL";
		if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
		sql += " ORDER BY timestamp DESC LIMIT " + QString::number(messageLimit);
		wordQuery.prepare(sql);
	}

	QHash<QString, int> wordFreq;
	int messagesAnalyzed = 0;
	if (wordQuery.exec()) {
		while (wordQuery.next()) {
			messagesAnalyzed++;
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
		topic["frequency"] = sorted[t * wordsPerTopic].second;
		topics.append(topic);
	}

	result["indexed_messages"] = indexedCount;
	result["messages_analyzed"] = messagesAnalyzed;
	result["success"] = true;
	result["topics"] = topics;
	result["method"] = usingFts ? "keyword_frequency_fts" : "keyword_frequency_db";

	return result;
}

QJsonObject Server::toolClassifyIntent(const QJsonObject &args) {
	QString text = args["text"].toString();

	if (text.isEmpty()) {
		QJsonObject result;
		result["error"] = "Missing text parameter";
		result["success"] = false;
		return result;
	}

	// Rule-based intent classification (always runs as primary or verification)
	QString lower = text.trimmed().toLower();

	QString intentStr = "statement";
	double confidence = 0.6;
	QJsonArray signals;

	// Question detection
	if (lower.endsWith("?")
		|| lower.startsWith("what ") || lower.startsWith("who ")
		|| lower.startsWith("where ") || lower.startsWith("when ")
		|| lower.startsWith("why ") || lower.startsWith("how ")
		|| lower.startsWith("which ") || lower.startsWith("is ")
		|| lower.startsWith("are ") || lower.startsWith("can ")
		|| lower.startsWith("could ") || lower.startsWith("would ")
		|| lower.startsWith("do ") || lower.startsWith("does ")
		|| lower.startsWith("did ") || lower.startsWith("will ")) {
		intentStr = "question";
		confidence = 0.85;
		signals.append("interrogative_pattern");
	}
	// Command detection
	else if (lower.startsWith("please ") || lower.startsWith("send ")
		|| lower.startsWith("show ") || lower.startsWith("get ")
		|| lower.startsWith("find ") || lower.startsWith("search ")
		|| lower.startsWith("list ") || lower.startsWith("create ")
		|| lower.startsWith("delete ") || lower.startsWith("update ")
		|| lower.startsWith("set ") || lower.startsWith("stop ")
		|| lower.startsWith("start ") || lower.startsWith("run ")
		|| lower.startsWith("open ") || lower.startsWith("close ")
		|| lower.startsWith("help ") || lower.startsWith("/")) {
		intentStr = "command";
		confidence = 0.8;
		signals.append("imperative_pattern");
	}
	// Greeting detection
	else if (lower.startsWith("hi") || lower.startsWith("hello")
		|| lower.startsWith("hey") || lower.startsWith("good morning")
		|| lower.startsWith("good afternoon") || lower.startsWith("good evening")
		|| lower == "yo" || lower == "sup" || lower.startsWith("greetings")) {
		intentStr = "greeting";
		confidence = 0.9;
		signals.append("greeting_keyword");
	}
	// Farewell detection
	else if (lower.startsWith("bye") || lower.startsWith("goodbye")
		|| lower.startsWith("good night") || lower.startsWith("see you")
		|| lower.startsWith("take care") || lower == "later"
		|| lower.startsWith("cya") || lower.startsWith("gotta go")) {
		intentStr = "farewell";
		confidence = 0.9;
		signals.append("farewell_keyword");
	}
	// Agreement detection
	else if (lower == "yes" || lower == "yeah" || lower == "yep"
		|| lower == "sure" || lower == "ok" || lower == "okay"
		|| lower == "agreed" || lower == "exactly" || lower == "right"
		|| lower.contains("i agree") || lower.startsWith("sounds good")
		|| lower.startsWith("yes,") || lower.startsWith("yes ")
		|| lower.startsWith("yeah,") || lower.startsWith("yeah ")
		|| lower == "definitely" || lower == "absolutely") {
		intentStr = "agreement";
		confidence = 0.85;
		signals.append("agreement_keyword");
	}
	// Disagreement detection
	else if (lower == "no" || lower == "nope" || lower == "nah"
		|| lower.startsWith("i disagree") || lower.startsWith("i don't think")
		|| lower.startsWith("that's wrong") || lower.startsWith("not really")
		|| lower.startsWith("actually,") || lower == "wrong") {
		intentStr = "disagreement";
		confidence = 0.85;
		signals.append("disagreement_keyword");
	}

	// Check for exclamation (strong statement)
	if (lower.endsWith("!") && intentStr == "statement") {
		confidence = 0.7;
		signals.append("exclamation");
	}

	QJsonObject result;
	result["text"] = text;
	result["intent"] = intentStr;
	result["confidence"] = confidence;
	result["signals"] = signals;
	result["method"] = "rule_based";
	result["success"] = true;
	return result;
}

QJsonObject Server::toolExtractEntities(const QJsonObject &args) {
	QString text = args["text"].toString();

	if (text.isEmpty()) {
		QJsonObject result;
		result["error"] = "Missing text parameter";
		result["success"] = false;
		return result;
	}

	// Regex-based entity extraction (handles all types including email, date, monetary)
	QJsonArray entitiesArray;

	// @username mentions
	{
		QRegularExpression re("@([a-zA-Z][a-zA-Z0-9_]{4,31})");
		auto it = re.globalMatch(text);
		while (it.hasNext()) {
			auto match = it.next();
			QJsonObject e;
			e["type"] = "user_mention";
			e["text"] = match.captured(0);
			e["value"] = match.captured(1);
			e["offset"] = match.capturedStart(0);
			e["length"] = match.capturedLength(0);
			entitiesArray.append(e);
		}
	}

	// URLs
	{
		QRegularExpression re("https?://[^\\s<>\"']+|www\\.[^\\s<>\"']+");
		auto it = re.globalMatch(text);
		while (it.hasNext()) {
			auto match = it.next();
			QJsonObject e;
			e["type"] = "url";
			e["text"] = match.captured(0);
			e["offset"] = match.capturedStart(0);
			e["length"] = match.capturedLength(0);
			entitiesArray.append(e);
		}
	}

	// Email addresses
	{
		QRegularExpression re("[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}");
		auto it = re.globalMatch(text);
		while (it.hasNext()) {
			auto match = it.next();
			QJsonObject e;
			e["type"] = "email";
			e["text"] = match.captured(0);
			e["offset"] = match.capturedStart(0);
			e["length"] = match.capturedLength(0);
			entitiesArray.append(e);
		}
	}

	// Phone numbers (international format)
	{
		QRegularExpression re("\\+?[1-9]\\d{6,14}");
		auto it = re.globalMatch(text);
		while (it.hasNext()) {
			auto match = it.next();
			QJsonObject e;
			e["type"] = "phone_number";
			e["text"] = match.captured(0);
			e["offset"] = match.capturedStart(0);
			e["length"] = match.capturedLength(0);
			entitiesArray.append(e);
		}
	}

	// Hashtags
	{
		QRegularExpression re("#([a-zA-Z][a-zA-Z0-9_]+)");
		auto it = re.globalMatch(text);
		while (it.hasNext()) {
			auto match = it.next();
			QJsonObject e;
			e["type"] = "hashtag";
			e["text"] = match.captured(0);
			e["value"] = match.captured(1);
			e["offset"] = match.capturedStart(0);
			e["length"] = match.capturedLength(0);
			entitiesArray.append(e);
		}
	}

	// Bot commands
	{
		QRegularExpression re("/([a-zA-Z][a-zA-Z0-9_]{0,63})");
		auto it = re.globalMatch(text);
		while (it.hasNext()) {
			auto match = it.next();
			QJsonObject e;
			e["type"] = "bot_command";
			e["text"] = match.captured(0);
			e["value"] = match.captured(1);
			e["offset"] = match.capturedStart(0);
			e["length"] = match.capturedLength(0);
			entitiesArray.append(e);
		}
	}

	// TON/crypto addresses (EQ... or UQ...)
	{
		QRegularExpression re("(EQ|UQ)[A-Za-z0-9_-]{46}");
		auto it = re.globalMatch(text);
		while (it.hasNext()) {
			auto match = it.next();
			QJsonObject e;
			e["type"] = "crypto_address";
			e["text"] = match.captured(0);
			e["offset"] = match.capturedStart(0);
			e["length"] = match.capturedLength(0);
			entitiesArray.append(e);
		}
	}

	// Dates (YYYY-MM-DD, DD/MM/YYYY, etc.)
	{
		QRegularExpression re("\\d{4}-\\d{2}-\\d{2}|\\d{1,2}/\\d{1,2}/\\d{2,4}");
		auto it = re.globalMatch(text);
		while (it.hasNext()) {
			auto match = it.next();
			QJsonObject e;
			e["type"] = "date";
			e["text"] = match.captured(0);
			e["offset"] = match.capturedStart(0);
			e["length"] = match.capturedLength(0);
			entitiesArray.append(e);
		}
	}

	// Monetary amounts ($100, 50 USD, 1.5 TON, etc.)
	{
		QRegularExpression re("\\$[\\d,.]+|[\\d,.]+\\s*(USD|EUR|GBP|TON|BTC|ETH|RUB)\\b",
			QRegularExpression::CaseInsensitiveOption);
		auto it = re.globalMatch(text);
		while (it.hasNext()) {
			auto match = it.next();
			QJsonObject e;
			e["type"] = "monetary_amount";
			e["text"] = match.captured(0);
			e["offset"] = match.capturedStart(0);
			e["length"] = match.capturedLength(0);
			entitiesArray.append(e);
		}
	}

	QJsonObject result;
	result["text"] = text;
	result["entities"] = entitiesArray;
	result["count"] = entitiesArray.size();
	result["method"] = "regex";
	result["success"] = true;
	return result;
}


} // namespace MCP
