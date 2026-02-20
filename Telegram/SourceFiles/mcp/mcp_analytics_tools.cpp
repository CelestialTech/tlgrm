// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== ANALYTICS TOOL IMPLEMENTATIONS =====
// Each tool delegates to _analytics when available, falls back to direct
// SQL queries against the archived_messages / messages tables otherwise.

QJsonObject Server::toolGetMessageStats(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString period = args.value("period").toString("all");

	if (_analytics) {
		auto stats = _analytics->getMessageStatistics(chatId, period);
		QJsonObject result = stats;
		result["chat_id"] = QString::number(chatId);
		return result;
	}

	// Fallback: compute stats from local DB
	QJsonObject result;
	result["chat_id"] = QString::number(chatId);
	result["period"] = period;

	// Determine time filter
	QString timeFilter;
	if (period == "day") {
		timeFilter = " AND timestamp >= strftime('%s','now','-1 day')";
	} else if (period == "week") {
		timeFilter = " AND timestamp >= strftime('%s','now','-7 days')";
	} else if (period == "month") {
		timeFilter = " AND timestamp >= strftime('%s','now','-30 days')";
	}

	// Total messages
	QSqlQuery query(_db);
	QString sql = "SELECT COUNT(*), COUNT(DISTINCT username) FROM messages WHERE 1=1";
	if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
	sql += timeFilter;
	query.exec(sql);
	if (query.next()) {
		result["total_messages"] = query.value(0).toInt();
		result["unique_senders"] = query.value(1).toInt();
	}

	// Messages per day average
	sql = "SELECT MIN(timestamp), MAX(timestamp) FROM messages WHERE 1=1";
	if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
	sql += timeFilter;
	query.exec(sql);
	if (query.next()) {
		qint64 minTs = query.value(0).toLongLong();
		qint64 maxTs = query.value(1).toLongLong();
		if (minTs > 0 && maxTs > minTs) {
			double days = static_cast<double>(maxTs - minTs) / 86400.0;
			if (days < 1.0) days = 1.0;
			result["messages_per_day"] = result["total_messages"].toInt() / days;
			result["time_span_days"] = days;
		}
	}

	// Average message length
	sql = "SELECT AVG(LENGTH(content)) FROM messages WHERE content != ''";
	if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
	sql += timeFilter;
	query.exec(sql);
	if (query.next()) {
		result["avg_message_length"] = query.value(0).toDouble();
	}

	result["success"] = true;
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetUserActivity(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();

	if (_analytics) {
		return _analytics->getUserActivity(userId, chatId);
	}

	QJsonObject result;
	result["user_id"] = QString::number(userId);

	QSqlQuery query(_db);
	QString sql = "SELECT COUNT(*), MIN(timestamp), MAX(timestamp), "
		"AVG(LENGTH(content)) FROM messages WHERE 1=1";

	// Match by user_id or username
	if (userId > 0) {
		sql += " AND (user_id = " + QString::number(userId) + ")";
	}
	if (chatId > 0) {
		sql += " AND chat_id = " + QString::number(chatId);
		result["chat_id"] = QString::number(chatId);
	}

	query.exec(sql);
	if (query.next()) {
		result["total_messages"] = query.value(0).toInt();
		auto firstTs = query.value(1).toLongLong();
		auto lastTs = query.value(2).toLongLong();
		if (firstTs > 0) {
			result["first_message"] = QDateTime::fromSecsSinceEpoch(firstTs).toString(Qt::ISODate);
		}
		if (lastTs > 0) {
			result["last_message"] = QDateTime::fromSecsSinceEpoch(lastTs).toString(Qt::ISODate);
		}
		result["avg_message_length"] = query.value(3).toDouble();
	}

	// Activity by hour
	sql = "SELECT CAST(strftime('%H', timestamp, 'unixepoch') AS INTEGER) as hour, COUNT(*) "
		"FROM messages WHERE 1=1";
	if (userId > 0) sql += " AND user_id = " + QString::number(userId);
	if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
	sql += " GROUP BY hour ORDER BY hour";

	query.exec(sql);
	QJsonObject hourlyActivity;
	while (query.next()) {
		hourlyActivity[QString::number(query.value(0).toInt())] = query.value(1).toInt();
	}
	if (!hourlyActivity.isEmpty()) {
		result["hourly_activity"] = hourlyActivity;
	}

	result["success"] = true;
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetChatActivity(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	if (_analytics) {
		return _analytics->getChatActivity(chatId);
	}

	QJsonObject result;
	result["chat_id"] = QString::number(chatId);

	QSqlQuery query(_db);
	QString filter = (chatId > 0)
		? " WHERE chat_id = " + QString::number(chatId)
		: QString();

	// Daily activity for last 30 days
	QString sql = "SELECT date(timestamp, 'unixepoch') as day, COUNT(*) "
		"FROM messages" + filter
		+ " AND timestamp >= strftime('%s','now','-30 days')"
		  " GROUP BY day ORDER BY day";
	query.exec(sql);

	QJsonArray dailyData;
	while (query.next()) {
		QJsonObject point;
		point["date"] = query.value(0).toString();
		point["count"] = query.value(1).toInt();
		dailyData.append(point);
	}
	result["daily_activity"] = dailyData;

	// Top senders
	sql = "SELECT username, COUNT(*) as cnt FROM messages" + filter
		+ " GROUP BY username ORDER BY cnt DESC LIMIT 10";
	query.exec(sql);

	QJsonArray topSenders;
	while (query.next()) {
		QJsonObject sender;
		sender["username"] = query.value(0).toString();
		sender["message_count"] = query.value(1).toInt();
		topSenders.append(sender);
	}
	result["top_senders"] = topSenders;

	// Message types breakdown (text vs media)
	sql = "SELECT "
		"SUM(CASE WHEN content != '' AND content IS NOT NULL THEN 1 ELSE 0 END) as text_count, "
		"COUNT(*) as total FROM messages" + filter;
	query.exec(sql);
	if (query.next()) {
		int textCount = query.value(0).toInt();
		int total = query.value(1).toInt();
		result["text_messages"] = textCount;
		result["total_messages"] = total;
		result["media_messages"] = total - textCount;
	}

	result["success"] = true;
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetTimeSeries(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString granularity = args.value("granularity").toString("daily");

	if (_analytics) {
		auto timeSeries = _analytics->getTimeSeries(chatId, granularity);
		QJsonObject result;
		result["chat_id"] = QString::number(chatId);
		result["granularity"] = granularity;
		result["data_points"] = timeSeries;
		result["count"] = timeSeries.size();
		return result;
	}

	QJsonObject result;
	result["chat_id"] = QString::number(chatId);
	result["granularity"] = granularity;

	QString dateFormat;
	if (granularity == "hourly") {
		dateFormat = "%Y-%m-%d %H:00";
	} else if (granularity == "weekly") {
		dateFormat = "%Y-W%W";
	} else if (granularity == "monthly") {
		dateFormat = "%Y-%m";
	} else {
		dateFormat = "%Y-%m-%d"; // daily
	}

	QSqlQuery query(_db);
	QString sql = QString(
		"SELECT strftime('%1', timestamp, 'unixepoch') as period, COUNT(*) "
		"FROM messages WHERE 1=1").arg(dateFormat);
	if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
	sql += " GROUP BY period ORDER BY period DESC LIMIT 100";

	query.exec(sql);

	QJsonArray dataPoints;
	while (query.next()) {
		QJsonObject point;
		point["period"] = query.value(0).toString();
		point["count"] = query.value(1).toInt();
		dataPoints.append(point);
	}

	result["data_points"] = dataPoints;
	result["count"] = dataPoints.size();
	result["success"] = true;
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetTopUsers(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(10);

	if (_analytics) {
		auto topUsers = _analytics->getTopUsers(chatId, limit);
		QJsonObject result;
		result["chat_id"] = QString::number(chatId);
		result["users"] = topUsers;
		result["count"] = topUsers.size();
		return result;
	}

	QJsonObject result;
	result["chat_id"] = QString::number(chatId);

	QSqlQuery query(_db);
	QString sql = "SELECT username, user_id, COUNT(*) as msg_count, "
		"MIN(timestamp) as first_msg, MAX(timestamp) as last_msg, "
		"AVG(LENGTH(content)) as avg_len "
		"FROM messages WHERE 1=1";
	if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
	sql += " GROUP BY COALESCE(NULLIF(username,''), user_id) ORDER BY msg_count DESC LIMIT "
		+ QString::number(limit);

	query.exec(sql);

	QJsonArray users;
	int rank = 1;
	while (query.next()) {
		QJsonObject user;
		user["rank"] = rank++;
		user["username"] = query.value(0).toString();
		user["user_id"] = query.value(1).toLongLong();
		user["message_count"] = query.value(2).toInt();
		auto firstTs = query.value(3).toLongLong();
		auto lastTs = query.value(4).toLongLong();
		if (firstTs > 0) {
			user["first_message"] = QDateTime::fromSecsSinceEpoch(firstTs).toString(Qt::ISODate);
		}
		if (lastTs > 0) {
			user["last_message"] = QDateTime::fromSecsSinceEpoch(lastTs).toString(Qt::ISODate);
		}
		user["avg_message_length"] = query.value(5).toDouble();
		users.append(user);
	}

	result["users"] = users;
	result["count"] = users.size();
	result["success"] = true;
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetTopWords(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(20);

	if (_analytics) {
		auto topWords = _analytics->getTopWords(chatId, limit);
		QJsonObject result;
		result["chat_id"] = QString::number(chatId);
		result["words"] = topWords;
		result["count"] = topWords.size();
		return result;
	}

	// Fallback: word frequency analysis from DB
	QJsonObject result;
	result["chat_id"] = QString::number(chatId);

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
		"very", "just", "also", "now", "here", "there", "then", "about",
		"up", "out", "if", "into", "through", "over", "after", "before"
	};

	QSqlQuery query(_db);
	QString sql = "SELECT content FROM messages WHERE content != '' AND content IS NOT NULL";
	if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
	sql += " ORDER BY timestamp DESC LIMIT 2000";

	query.exec(sql);

	QHash<QString, int> wordFreq;
	int totalMessages = 0;
	while (query.next()) {
		totalMessages++;
		QString text = query.value(0).toString().toLower();
		QStringList words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
		for (const QString &word : words) {
			// Strip non-alphanumeric edges
			QString clean = word;
			clean.remove(QRegularExpression("^[^a-zA-Z0-9]+"));
			clean.remove(QRegularExpression("[^a-zA-Z0-9]+$"));
			if (clean.length() >= 3 && !stopWords.contains(clean)) {
				wordFreq[clean]++;
			}
		}
	}

	QVector<QPair<QString, int>> sorted;
	sorted.reserve(wordFreq.size());
	for (auto it = wordFreq.constBegin(); it != wordFreq.constEnd(); ++it) {
		sorted.append({it.key(), it.value()});
	}
	std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
		return a.second > b.second;
	});

	QJsonArray words;
	for (int i = 0; i < limit && i < sorted.size(); ++i) {
		QJsonObject word;
		word["word"] = sorted[i].first;
		word["count"] = sorted[i].second;
		word["rank"] = i + 1;
		words.append(word);
	}

	result["words"] = words;
	result["count"] = words.size();
	result["messages_analyzed"] = totalMessages;
	result["success"] = true;
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolExportAnalytics(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString outputPath = args["output_path"].toString();
	QString format = args.value("format").toString("json");

	if (_analytics) {
		QString resultPath = _analytics->exportAnalytics(chatId, format, outputPath);
		QJsonObject result;
		result["success"] = !resultPath.isEmpty();
		result["chat_id"] = QString::number(chatId);
		result["output_path"] = resultPath;
		result["format"] = format;
		return result;
	}

	// Fallback: generate export from DB
	QJsonObject result;
	result["chat_id"] = QString::number(chatId);
	result["format"] = format;

	// Gather all analytics data
	QJsonObject analyticsData;
	analyticsData["message_stats"] = toolGetMessageStats(args);
	analyticsData["chat_activity"] = toolGetChatActivity(args);

	QJsonObject topUsersArgs;
	topUsersArgs["chat_id"] = args["chat_id"];
	topUsersArgs["limit"] = 20;
	analyticsData["top_users"] = toolGetTopUsers(topUsersArgs);

	QJsonObject topWordsArgs;
	topWordsArgs["chat_id"] = args["chat_id"];
	topWordsArgs["limit"] = 50;
	analyticsData["top_words"] = toolGetTopWords(topWordsArgs);

	analyticsData["exported_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

	// Write to file
	if (outputPath.isEmpty()) {
		outputPath = QDir::tempPath() + "/mcp_analytics_"
			+ QString::number(chatId) + "." + format;
	}

	QFile file(outputPath);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QJsonDocument doc(analyticsData);
		file.write(doc.toJson(QJsonDocument::Indented));
		file.close();

		result["success"] = true;
		result["output_path"] = outputPath;
		result["size_bytes"] = QFileInfo(outputPath).size();
	} else {
		result["success"] = false;
		result["error"] = "Failed to write file: " + file.errorString();
	}

	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetTrends(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString metric = args.value("metric").toString("messages");
	int daysBack = args.value("days_back").toInt(30);

	if (_analytics) {
		auto trends = _analytics->getTrends(chatId, metric, daysBack);
		QJsonObject result = trends;
		result["chat_id"] = QString::number(chatId);
		result["metric"] = metric;
		result["days_back"] = daysBack;
		return result;
	}

	QJsonObject result;
	result["chat_id"] = QString::number(chatId);
	result["metric"] = metric;
	result["days_back"] = daysBack;

	QSqlQuery query(_db);

	// Get daily counts for the period
	QString sql = QString(
		"SELECT date(timestamp, 'unixepoch') as day, COUNT(*) "
		"FROM messages WHERE timestamp >= strftime('%%s','now','-%1 days')").arg(daysBack);
	if (chatId > 0) sql += " AND chat_id = " + QString::number(chatId);
	sql += " GROUP BY day ORDER BY day";

	query.exec(sql);

	QJsonArray dailyPoints;
	int totalCount = 0;
	int dayCount = 0;
	int prevCount = -1;
	int increasingDays = 0;
	int decreasingDays = 0;
	while (query.next()) {
		QJsonObject point;
		point["date"] = query.value(0).toString();
		int count = query.value(1).toInt();
		point["count"] = count;
		dailyPoints.append(point);
		totalCount += count;
		dayCount++;
		if (prevCount >= 0) {
			if (count > prevCount) increasingDays++;
			else if (count < prevCount) decreasingDays++;
		}
		prevCount = count;
	}

	result["data_points"] = dailyPoints;
	result["total_count"] = totalCount;
	result["day_count"] = dayCount;

	if (dayCount > 0) {
		result["daily_average"] = totalCount / static_cast<double>(dayCount);
	}

	// Simple trend direction
	if (increasingDays > decreasingDays * 1.5) {
		result["trend"] = "increasing";
	} else if (decreasingDays > increasingDays * 1.5) {
		result["trend"] = "decreasing";
	} else {
		result["trend"] = "stable";
	}
	result["increasing_days"] = increasingDays;
	result["decreasing_days"] = decreasingDays;

	result["success"] = true;
	result["source"] = "local_db";
	return result;
}


} // namespace MCP
