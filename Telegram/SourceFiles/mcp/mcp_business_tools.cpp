// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"
#include "data/data_types.h"
#include "media/audio/media_audio.h"

namespace MCP {
// ===== BUSINESS EQUIVALENT FEATURES IMPLEMENTATION =====

// Quick Replies Tools
QJsonObject Server::toolCreateQuickReply(const QJsonObject &args) {
	QJsonObject result;
	QString shortcut = args["shortcut"].toString();
	QString text = args["text"].toString();
	QString category = args.value("category").toString("general");

	if (shortcut.isEmpty() || text.isEmpty()) {
		result["error"] = "Missing shortcut or text parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT INTO quick_replies (shortcut, text, category, usage_count, created_at) "
				  "VALUES (?, ?, ?, 0, datetime('now'))");
	query.addBindValue(shortcut);
	query.addBindValue(text);
	query.addBindValue(category);

	if (query.exec()) {
		result["success"] = true;
		result["id"] = query.lastInsertId().toLongLong();
		result["shortcut"] = shortcut;
		result["text"] = text;
		result["category"] = category;
	} else {
		result["success"] = false;
		result["error"] = "Failed to create quick reply: " + query.lastError().text();
	}

	return result;
}

QJsonObject Server::toolListQuickReplies(const QJsonObject &args) {
	QJsonObject result;
	QString category = args.value("category").toString();
	int limit = args.value("limit").toInt(50);

	QSqlQuery query(_db);
	QString sql = "SELECT id, shortcut, text, category, usage_count, created_at FROM quick_replies ";
	if (!category.isEmpty()) {
		sql += "WHERE category = ? ";
	}
	sql += "ORDER BY usage_count DESC LIMIT ?";

	query.prepare(sql);
	if (!category.isEmpty()) {
		query.addBindValue(category);
	}
	query.addBindValue(limit);

	QJsonArray replies;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject reply;
			reply["id"] = query.value(0).toLongLong();
			reply["shortcut"] = query.value(1).toString();
			reply["text"] = query.value(2).toString();
			reply["category"] = query.value(3).toString();
			reply["usage_count"] = query.value(4).toInt();
			reply["created_at"] = query.value(5).toString();
			replies.append(reply);
		}
	}

	result["success"] = true;
	result["quick_replies"] = replies;
	result["count"] = replies.size();

	return result;
}

QJsonObject Server::toolUpdateQuickReply(const QJsonObject &args) {
	QJsonObject result;
	qint64 id = args["id"].toVariant().toLongLong();
	QString shortcut = args.value("shortcut").toString();
	QString text = args.value("text").toString();
	QString category = args.value("category").toString();

	QStringList updates;
	QVariantList values;

	if (!shortcut.isEmpty()) { updates << "shortcut = ?"; values << shortcut; }
	if (!text.isEmpty()) { updates << "text = ?"; values << text; }
	if (!category.isEmpty()) { updates << "category = ?"; values << category; }

	if (updates.isEmpty()) {
		result["error"] = "No update fields provided";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("UPDATE quick_replies SET " + updates.join(", ") + " WHERE id = ?");
	for (const auto &val : values) {
		query.addBindValue(val);
	}
	query.addBindValue(id);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["id"] = id;
	} else {
		result["success"] = false;
		result["error"] = "Quick reply not found";
	}

	return result;
}

QJsonObject Server::toolDeleteQuickReply(const QJsonObject &args) {
	QJsonObject result;
	qint64 id = args["id"].toVariant().toLongLong();

	QSqlQuery query(_db);
	query.prepare("DELETE FROM quick_replies WHERE id = ?");
	query.addBindValue(id);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["deleted"] = true;
	} else {
		result["success"] = false;
		result["error"] = "Quick reply not found";
	}

	return result;
}

QJsonObject Server::toolUseQuickReply(const QJsonObject &args) {
	QJsonObject result;
	QString shortcut = args["shortcut"].toString();
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	if (shortcut.isEmpty()) {
		result["error"] = "Missing shortcut parameter";
		result["success"] = false;
		return result;
	}

	// Get quick reply text
	QSqlQuery query(_db);
	query.prepare("SELECT id, text FROM quick_replies WHERE shortcut = ?");
	query.addBindValue(shortcut);

	if (!query.exec() || !query.next()) {
		result["error"] = "Quick reply not found: " + shortcut;
		result["success"] = false;
		return result;
	}

	qint64 replyId = query.value(0).toLongLong();
	QString text = query.value(1).toString();

	// Increment usage count
	QSqlQuery updateQuery(_db);
	updateQuery.prepare("UPDATE quick_replies SET usage_count = usage_count + 1 WHERE id = ?");
	updateQuery.addBindValue(replyId);
	updateQuery.exec();

	// Send the message if chat_id provided
	if (chatId > 0 && _session) {
		QJsonObject sendArgs;
		sendArgs["chat_id"] = chatId;
		sendArgs["text"] = text;
		QJsonObject sendResult = toolSendMessage(sendArgs);

		result["success"] = sendResult["success"].toBool();
		result["text"] = text;
		result["chat_id"] = chatId;
		result["message_sent"] = sendResult["success"].toBool();
	} else {
		result["success"] = true;
		result["text"] = text;
		result["note"] = "No chat_id provided, returning text only";
	}

	return result;
}

// Greeting Message Tools
QJsonObject Server::toolSetGreetingMessage(const QJsonObject &args) {
	QJsonObject result;
	QString message = args["message"].toString();
	bool enabled = args.value("enabled").toBool(true);
	QJsonArray triggerChats = args.value("trigger_chats").toArray();
	int delaySeconds = args.value("delay_seconds").toInt(0);

	if (message.isEmpty()) {
		result["error"] = "Missing message parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO greeting_config (id, enabled, message, trigger_chats, delay_seconds, updated_at) "
				  "VALUES (1, ?, ?, ?, ?, datetime('now'))");
	query.addBindValue(enabled);
	query.addBindValue(message);
	query.addBindValue(QJsonDocument(triggerChats).toJson(QJsonDocument::Compact));
	query.addBindValue(delaySeconds);

	if (query.exec()) {
		result["success"] = true;
		result["enabled"] = enabled;
		result["message"] = message;
		result["delay_seconds"] = delaySeconds;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save greeting config";
	}

	return result;
}

QJsonObject Server::toolGetGreetingMessage(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT enabled, message, trigger_chats, delay_seconds, greetings_sent, updated_at "
				  "FROM greeting_config WHERE id = 1");

	if (query.exec() && query.next()) {
		result["enabled"] = query.value(0).toBool();
		result["message"] = query.value(1).toString();
		result["trigger_chats"] = QJsonDocument::fromJson(query.value(2).toByteArray()).array();
		result["delay_seconds"] = query.value(3).toInt();
		result["greetings_sent"] = query.value(4).toInt();
		result["updated_at"] = query.value(5).toString();
		result["success"] = true;
	} else {
		result["enabled"] = false;
		result["success"] = true;
		result["note"] = "No greeting message configured";
	}

	return result;
}

QJsonObject Server::toolDisableGreeting(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("UPDATE greeting_config SET enabled = 0 WHERE id = 1");

	if (query.exec()) {
		result["success"] = true;
		result["disabled"] = true;
	} else {
		result["success"] = false;
		result["error"] = "Failed to disable greeting";
	}

	return result;
}

QJsonObject Server::toolTestGreeting(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	// Get greeting config
	QSqlQuery query(_db);
	query.prepare("SELECT message FROM greeting_config WHERE id = 1 AND enabled = 1");

	if (!query.exec() || !query.next()) {
		result["success"] = false;
		result["error"] = "No active greeting message configured";
		return result;
	}

	QString message = query.value(0).toString();

	// Actually send the greeting if chat_id provided and session available
	if (chatId > 0 && _session) {
		QJsonObject sendArgs;
		sendArgs["chat_id"] = chatId;
		sendArgs["text"] = message;
		QJsonObject sendResult = toolSendMessage(sendArgs);

		if (sendResult["success"].toBool()) {
			// Increment greetings_sent counter
			QSqlQuery updateQuery(_db);
			updateQuery.prepare("UPDATE greeting_config SET greetings_sent = greetings_sent + 1 WHERE id = 1");
			updateQuery.exec();

			result["success"] = true;
			result["message"] = message;
			result["chat_id"] = chatId;
			result["message_sent"] = true;
		} else {
			result["success"] = false;
			result["error"] = sendResult["error"].toString();
			result["message"] = message;
			result["chat_id"] = chatId;
		}
	} else if (chatId == 0) {
		result["success"] = true;
		result["message"] = message;
		result["note"] = "No chat_id provided - returning greeting text only";
	} else {
		result["success"] = false;
		result["error"] = "No active session available to send message";
		result["message"] = message;
	}

	return result;
}

// Away Message Tools
QJsonObject Server::toolSetAwayMessage(const QJsonObject &args) {
	QJsonObject result;
	QString message = args["message"].toString();
	bool enabled = args.value("enabled").toBool(true);
	QString startTime = args.value("start_time").toString();
	QString endTime = args.value("end_time").toString();

	if (message.isEmpty()) {
		result["error"] = "Missing message parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO away_config (id, enabled, message, start_time, end_time, updated_at) "
				  "VALUES (1, ?, ?, ?, ?, datetime('now'))");
	query.addBindValue(enabled);
	query.addBindValue(message);
	query.addBindValue(startTime.isEmpty() ? QVariant() : startTime);
	query.addBindValue(endTime.isEmpty() ? QVariant() : endTime);

	if (query.exec()) {
		result["success"] = true;
		result["enabled"] = enabled;
		result["message"] = message;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save away config";
	}

	return result;
}

QJsonObject Server::toolGetAwayMessage(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT enabled, message, start_time, end_time, away_sent, updated_at "
				  "FROM away_config WHERE id = 1");

	if (query.exec() && query.next()) {
		result["enabled"] = query.value(0).toBool();
		result["message"] = query.value(1).toString();
		if (!query.value(2).isNull()) result["start_time"] = query.value(2).toString();
		if (!query.value(3).isNull()) result["end_time"] = query.value(3).toString();
		result["away_sent"] = query.value(4).toInt();
		result["updated_at"] = query.value(5).toString();
		result["success"] = true;
	} else {
		result["enabled"] = false;
		result["success"] = true;
		result["note"] = "No away message configured";
	}

	return result;
}

QJsonObject Server::toolDisableAway(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("UPDATE away_config SET enabled = 0 WHERE id = 1");

	if (query.exec()) {
		result["success"] = true;
		result["disabled"] = true;
	} else {
		result["success"] = false;
		result["error"] = "Failed to disable away message";
	}

	return result;
}

QJsonObject Server::toolTestAway(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	QSqlQuery query(_db);
	query.prepare("SELECT message, start_time, end_time FROM away_config WHERE id = 1 AND enabled = 1");

	if (!query.exec() || !query.next()) {
		result["success"] = false;
		result["error"] = "No active away message configured";
		return result;
	}

	QString message = query.value(0).toString();

	// Actually send the away message if chat_id provided and session available
	if (chatId > 0 && _session) {
		QJsonObject sendArgs;
		sendArgs["chat_id"] = chatId;
		sendArgs["text"] = message;
		QJsonObject sendResult = toolSendMessage(sendArgs);

		if (sendResult["success"].toBool()) {
			// Increment away_sent counter
			QSqlQuery updateQuery(_db);
			updateQuery.prepare("UPDATE away_config SET away_sent = away_sent + 1 WHERE id = 1");
			updateQuery.exec();

			result["success"] = true;
			result["message"] = message;
			result["chat_id"] = chatId;
			result["message_sent"] = true;
		} else {
			result["success"] = false;
			result["error"] = sendResult["error"].toString();
			result["message"] = message;
			result["chat_id"] = chatId;
		}
	} else if (chatId == 0) {
		result["success"] = true;
		result["message"] = message;
		result["note"] = "No chat_id provided - returning away message text only";
	} else {
		result["success"] = false;
		result["error"] = "No active session available to send message";
		result["message"] = message;
	}

	return result;
}

// Business Hours Tools
QJsonObject Server::toolSetBusinessHours(const QJsonObject &args) {
	QJsonObject result;
	QJsonObject schedule = args["schedule"].toObject();
	QString timezone = args.value("timezone").toString("UTC");

	if (schedule.isEmpty()) {
		result["error"] = "Missing schedule parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO business_hours (id, enabled, schedule, timezone, updated_at) "
				  "VALUES (1, 1, ?, ?, datetime('now'))");
	query.addBindValue(QJsonDocument(schedule).toJson(QJsonDocument::Compact));
	query.addBindValue(timezone);

	if (query.exec()) {
		result["success"] = true;
		result["schedule"] = schedule;
		result["timezone"] = timezone;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save business hours";
	}

	return result;
}

QJsonObject Server::toolGetBusinessHours(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT enabled, schedule, timezone, updated_at FROM business_hours WHERE id = 1");

	if (query.exec() && query.next()) {
		result["enabled"] = query.value(0).toBool();
		result["schedule"] = QJsonDocument::fromJson(query.value(1).toByteArray()).object();
		result["timezone"] = query.value(2).toString();
		result["updated_at"] = query.value(3).toString();
		result["success"] = true;
	} else {
		result["success"] = true;
		result["note"] = "No business hours configured";
	}

	return result;
}

QJsonObject Server::toolCheckBusinessStatus(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT enabled, schedule, timezone FROM business_hours WHERE id = 1");

	if (!query.exec() || !query.next()) {
		result["is_open"] = true;  // Default to open if not configured
		result["success"] = true;
		result["note"] = "No business hours configured - defaulting to open";
		return result;
	}

	bool enabled = query.value(0).toBool();
	if (!enabled) {
		result["is_open"] = true;
		result["success"] = true;
		result["note"] = "Business hours disabled - always open";
		return result;
	}

	QJsonObject schedule = QJsonDocument::fromJson(query.value(1).toByteArray()).object();
	QString timezone = query.value(2).toString();

	// Get current day and time
	QDateTime now = QDateTime::currentDateTimeUtc();
	QString dayOfWeek = now.toString("dddd").toLower();

	bool isOpen = false;
	if (schedule.contains(dayOfWeek)) {
		QJsonObject daySchedule = schedule[dayOfWeek].toObject();
		QString openTime = daySchedule["open"].toString();
		QString closeTime = daySchedule["close"].toString();

		// Simple time check (could be more sophisticated)
		QString currentTime = now.toString("HH:mm");
		isOpen = (currentTime >= openTime && currentTime < closeTime);
	}

	result["is_open"] = isOpen;
	result["current_time"] = now.toString(Qt::ISODate);
	result["day_of_week"] = dayOfWeek;
	result["timezone"] = timezone;
	result["success"] = true;

	return result;
}

QJsonObject Server::toolGetNextAvailableSlot(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT enabled, schedule, timezone FROM business_hours WHERE id = 1");

	if (!query.exec() || !query.next()) {
		result["success"] = true;
		result["next_available"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
		result["note"] = "No business hours configured - available now";
		return result;
	}

	bool enabled = query.value(0).toBool();
	if (!enabled) {
		result["success"] = true;
		result["next_available"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
		result["note"] = "Business hours disabled - available now";
		return result;
	}

	QJsonObject schedule = QJsonDocument::fromJson(query.value(1).toByteArray()).object();
	QString timezone = query.value(2).toString();

	QDateTime now = QDateTime::currentDateTimeUtc();
	static const QStringList dayNames = {
		"", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"
	};

	// Search up to 7 days ahead for next open slot
	for (int dayOffset = 0; dayOffset < 7; ++dayOffset) {
		QDateTime candidate = now.addDays(dayOffset);
		int dow = candidate.date().dayOfWeek(); // 1=Monday .. 7=Sunday
		QString dayName = dayNames.value(dow);

		if (!schedule.contains(dayName)) continue;

		QJsonObject daySchedule = schedule[dayName].toObject();
		QString openTime = daySchedule["open"].toString();
		QString closeTime = daySchedule["close"].toString();
		if (openTime.isEmpty() || closeTime.isEmpty()) continue;

		QTime open = QTime::fromString(openTime, "HH:mm");
		QTime close = QTime::fromString(closeTime, "HH:mm");
		if (!open.isValid() || !close.isValid()) continue;

		if (dayOffset == 0) {
			// Today — check if we're still within open hours
			QTime currentTime = now.time();
			if (currentTime >= open && currentTime < close) {
				result["success"] = true;
				result["next_available"] = now.toString(Qt::ISODate);
				result["note"] = "Currently open";
				result["timezone"] = timezone;
				return result;
			}
			if (currentTime < open) {
				// Not open yet today
				QDateTime slot(candidate.date(), open, Qt::UTC);
				result["success"] = true;
				result["next_available"] = slot.toString(Qt::ISODate);
				result["day"] = dayName;
				result["timezone"] = timezone;
				return result;
			}
			// Past close — continue to next day
		} else {
			// Future day — return opening time
			QDateTime slot(candidate.date(), open, Qt::UTC);
			result["success"] = true;
			result["next_available"] = slot.toString(Qt::ISODate);
			result["day"] = dayName;
			result["timezone"] = timezone;
			return result;
		}
	}

	// No open slots found in the next 7 days
	result["success"] = true;
	result["next_available"] = QString();
	result["note"] = "No available slots found in the next 7 days";

	return result;
}

// AI Chatbot Tools
QJsonObject Server::toolConfigureChatbot(const QJsonObject &args) {
	QJsonObject result;
	QString name = args["name"].toString();
	QString personality = args.value("personality").toString("helpful");
	QJsonArray triggerKeywords = args.value("trigger_keywords").toArray();
	QString responseStyle = args.value("response_style").toString("concise");

	if (name.isEmpty()) {
		result["error"] = "Missing name parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO chatbot_config (id, enabled, name, personality, trigger_keywords, response_style, updated_at) "
				  "VALUES (1, 1, ?, ?, ?, ?, datetime('now'))");
	query.addBindValue(name);
	query.addBindValue(personality);
	query.addBindValue(QJsonDocument(triggerKeywords).toJson(QJsonDocument::Compact));
	query.addBindValue(responseStyle);

	if (query.exec()) {
		result["success"] = true;
		result["name"] = name;
		result["personality"] = personality;
		result["response_style"] = responseStyle;
	} else {
		result["success"] = false;
		result["error"] = "Failed to configure chatbot";
	}

	return result;
}

QJsonObject Server::toolGetChatbotConfig(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT enabled, name, personality, trigger_keywords, response_style, messages_handled "
				  "FROM chatbot_config WHERE id = 1");

	if (query.exec() && query.next()) {
		result["enabled"] = query.value(0).toBool();
		result["name"] = query.value(1).toString();
		result["personality"] = query.value(2).toString();
		result["trigger_keywords"] = QJsonDocument::fromJson(query.value(3).toByteArray()).array();
		result["response_style"] = query.value(4).toString();
		result["messages_handled"] = query.value(5).toInt();
		result["success"] = true;
	} else {
		result["success"] = true;
		result["note"] = "No chatbot configured";
	}

	return result;
}

QJsonObject Server::toolTrainChatbot(const QJsonObject &args) {
	QJsonObject result;
	QJsonArray trainingData = args["training_data"].toArray();

	if (trainingData.isEmpty()) {
		result["error"] = "Missing or empty training_data";
		result["success"] = false;
		return result;
	}

	// Store training data in chatbot_training table
	_db.transaction();
	QSqlQuery insertQuery(_db);
	insertQuery.prepare(
		"INSERT INTO chatbot_training (input, output, category) "
		"VALUES (?, ?, ?)");

	QString category = args.value("category").toString("general");
	int added = 0;
	for (const auto &item : trainingData) {
		QJsonObject dataItem = item.toObject();
		QString input = dataItem["input"].toString();
		QString output = dataItem["output"].toString();

		if (!input.isEmpty() && !output.isEmpty()) {
			insertQuery.addBindValue(input);
			insertQuery.addBindValue(output);
			insertQuery.addBindValue(dataItem.value("category").toString(category));
			insertQuery.exec();
			insertQuery.finish();
			added++;
		}
	}
	_db.commit();

	// If local LLM is available, verify it can use the training data
	bool llmAvailable = _localLLM && _localLLM->isRunning();
	QString llmModel;
	if (llmAvailable) {
		llmModel = _localLLM->model();
		// Optionally test with a sample from the training data
		if (added > 0 && args.value("test_after_train").toBool(false)) {
			// Get the chatbot personality for context
			QSqlQuery configQuery(_db);
			configQuery.prepare("SELECT personality, response_style FROM chatbot_config WHERE id = 1");
			if (configQuery.exec() && configQuery.next()) {
				QString personality = configQuery.value(0).toString();
				QString responseStyle = configQuery.value(1).toString();

				// Use first training example as test
				QJsonObject firstItem = trainingData.first().toObject();
				QString testInput = firstItem["input"].toString();

				auto completion = _localLLM->chatbot(testInput, personality, responseStyle, trainingData);
				if (completion.success) {
					result["test_input"] = testInput;
					result["test_response"] = completion.text;
					result["test_model"] = completion.model;
				}
			}
		}
	}

	result["success"] = true;
	result["training_samples_added"] = added;
	result["llm_available"] = llmAvailable;
	if (llmAvailable) {
		result["llm_model"] = llmModel;
		result["status"] = "trained";
		result["note"] = QString("Training data stored. Local LLM (%1) will use these as few-shot examples for chatbot responses.").arg(llmModel);
	} else {
		result["status"] = "data_stored";
		result["note"] = "Training data persisted. Install Ollama (ollama.com) and pull a model to enable AI chatbot.";
	}

	return result;
}

QJsonObject Server::toolTestChatbot(const QJsonObject &args) {
	QJsonObject result;
	QString testInput = args["input"].toString();

	if (testInput.isEmpty()) {
		result["error"] = "Missing input parameter";
		result["success"] = false;
		return result;
	}

	// Get chatbot config
	QSqlQuery query(_db);
	query.prepare("SELECT personality, response_style FROM chatbot_config WHERE id = 1 AND enabled = 1");

	if (!query.exec() || !query.next()) {
		result["error"] = "No active chatbot configured";
		result["success"] = false;
		return result;
	}

	QString personality = query.value(0).toString();
	QString responseStyle = query.value(1).toString();

	// Check if local LLM is available
	if (!_localLLM || !_localLLM->isRunning()) {
		result["success"] = false;
		result["error"] = "No local LLM available. Install Ollama (ollama.com) and pull a model (e.g. 'ollama pull llama3.1:8b').";
		return result;
	}

	if (!_localLLM->healthCheck()) {
		result["success"] = false;
		result["error"] = "Local LLM backend is not responding. Ensure Ollama is running ('ollama serve').";
		return result;
	}

	// Fetch training examples for few-shot context
	QJsonArray trainingExamples;
	QSqlQuery trainingQuery(_db);
	trainingQuery.prepare(
		"SELECT input, output FROM chatbot_training "
		"ORDER BY RANDOM() LIMIT 10"
	);
	if (trainingQuery.exec()) {
		while (trainingQuery.next()) {
			QJsonObject ex;
			ex["input"] = trainingQuery.value(0).toString();
			ex["output"] = trainingQuery.value(1).toString();
			trainingExamples.append(ex);
		}
	}

	// Get conversation history for context
	QString botName = "default";
	QSqlQuery nameQuery(_db);
	nameQuery.prepare("SELECT name FROM chatbot_config WHERE id = 1");
	if (nameQuery.exec() && nameQuery.next()) {
		botName = nameQuery.value(0).toString();
	}

	// Call local LLM
	auto completion = _localLLM->chatbot(
		testInput, personality, responseStyle, trainingExamples);

	if (!completion.success) {
		result["success"] = false;
		result["error"] = QString("LLM generation failed: %1").arg(completion.error);
		return result;
	}

	// Store conversation for future context
	_localLLM->storeConversation(botName, testInput, completion.text);

	// Increment messages_handled counter
	QSqlQuery updateQuery(_db);
	updateQuery.prepare("UPDATE chatbot_config SET messages_handled = messages_handled + 1 WHERE id = 1");
	updateQuery.exec();

	result["success"] = true;
	result["input"] = testInput;
	result["response"] = completion.text;
	result["personality"] = personality;
	result["response_style"] = responseStyle;
	result["model"] = completion.model;
	result["prompt_tokens"] = completion.promptTokens;
	result["completion_tokens"] = completion.completionTokens;
	result["duration_seconds"] = completion.durationSeconds;
	result["training_examples_used"] = trainingExamples.size();

	return result;
}

QJsonObject Server::toolGetChatbotAnalytics(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT messages_handled FROM chatbot_config WHERE id = 1");

	if (query.exec() && query.next()) {
		result["messages_handled"] = query.value(0).toInt();
		result["success"] = true;
	} else {
		result["messages_handled"] = 0;
		result["success"] = true;
	}

	return result;
}

// Text to Speech Tools
QJsonObject Server::toolTextToSpeech(const QJsonObject &args) {
	QJsonObject result;
	QString text = args["text"].toString();
	QString voice = args.value("voice").toString();
	double speed = args.value("speed").toDouble(1.0);
	double pitch = args.value("pitch").toDouble(1.0);

	if (text.isEmpty()) {
		result["error"] = "Missing text parameter";
		result["success"] = false;
		return result;
	}

	if (!_textToSpeech || !_textToSpeech->isRunning()) {
		result["error"] = "TTS service not initialized";
		result["success"] = false;
		return result;
	}

	// Resolve voice from persona DB if it's a name (not a file path)
	QString voiceId = voice;
	if (!voice.isEmpty() && !QFile::exists(voice)) {
		QSqlQuery query(_db);
		query.prepare("SELECT voice_id, provider, speed, pitch FROM voice_persona WHERE name = ?");
		query.addBindValue(voice);
		if (query.exec() && query.next()) {
			voiceId = query.value(0).toString();
			QString providerStr = query.value(1).toString();
			if (providerStr == "piper") {
				_textToSpeech->setProvider(TTSProvider::PiperTTS);
			} else if (providerStr == "espeak" || providerStr == "espeak-ng") {
				_textToSpeech->setProvider(TTSProvider::EspeakNG);
			} else if (providerStr == "coqui") {
				_textToSpeech->setProvider(TTSProvider::CoquiPython);
			}
			if (speed == 1.0) {
				speed = query.value(2).toDouble(1.0);
			}
			if (pitch == 1.0) {
				pitch = query.value(3).toDouble(1.0);
			}
		}
	}

	auto synthesis = _textToSpeech->synthesize(text, voiceId, speed, pitch);

	if (!synthesis.success) {
		result["success"] = false;
		result["error"] = synthesis.error;
		return result;
	}

	result["success"] = true;
	result["text"] = text;
	result["voice"] = voice;
	result["speed"] = speed;
	result["provider"] = synthesis.provider;
	result["voice_used"] = synthesis.voiceUsed;
	result["duration_seconds"] = synthesis.durationSeconds;
	result["audio_size_bytes"] = synthesis.audioData.size();
	result["output_path"] = synthesis.outputPath;
	result["format"] = "ogg_opus";
	result["status"] = "generated";

	return result;
}

QJsonObject Server::toolConfigureVoicePersona(const QJsonObject &args) {
	QJsonObject result;
	QString name = args["name"].toString();
	QString voiceId = args["voice_id"].toString();
	double pitch = args.value("pitch").toDouble(1.0);
	double speed = args.value("speed").toDouble(1.0);

	if (name.isEmpty()) {
		result["error"] = "Missing name parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO voice_persona (name, voice_id, pitch, speed, created_at) "
				  "VALUES (?, ?, ?, ?, datetime('now'))");
	query.addBindValue(name);
	query.addBindValue(voiceId);
	query.addBindValue(pitch);
	query.addBindValue(speed);

	if (query.exec()) {
		result["success"] = true;
		result["name"] = name;
		result["voice_id"] = voiceId;
		result["pitch"] = pitch;
		result["speed"] = speed;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save voice persona";
	}

	return result;
}

QJsonObject Server::toolListVoicePersonas(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT name, voice_id, pitch, speed, created_at FROM voice_persona");

	QJsonArray personas;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject persona;
			persona["name"] = query.value(0).toString();
			persona["voice_id"] = query.value(1).toString();
			persona["pitch"] = query.value(2).toDouble();
			persona["speed"] = query.value(3).toDouble();
			persona["created_at"] = query.value(4).toString();
			personas.append(persona);
		}
	}

	result["success"] = true;
	result["personas"] = personas;
	result["count"] = personas.size();

	return result;
}

QJsonObject Server::toolSendVoiceReply(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString text = args["text"].toString();
	QString persona = args.value("persona").toString();

	if (chatId == 0 || text.isEmpty()) {
		result["error"] = "Missing chat_id or text";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	if (!_textToSpeech || !_textToSpeech->isRunning()) {
		result["error"] = "TTS service not initialized";
		result["success"] = false;
		return result;
	}

	// Look up persona settings
	QString voiceId;
	double speed = 1.0;
	double pitch = 1.0;

	if (!persona.isEmpty()) {
		QSqlQuery query(_db);
		query.prepare("SELECT voice_id, provider, speed, pitch, sample_path "
					  "FROM voice_persona WHERE name = ?");
		query.addBindValue(persona);

		if (query.exec() && query.next()) {
			voiceId = query.value(0).toString();
			QString providerStr = query.value(1).toString();
			speed = query.value(2).toDouble(1.0);
			pitch = query.value(3).toDouble(1.0);
			QString samplePath = query.value(4).toString();

			if (providerStr == "piper") {
				_textToSpeech->setProvider(TTSProvider::PiperTTS);
			} else if (providerStr == "espeak" || providerStr == "espeak-ng") {
				_textToSpeech->setProvider(TTSProvider::EspeakNG);
			} else if (providerStr == "coqui") {
				_textToSpeech->setProvider(TTSProvider::CoquiPython);
				if (!samplePath.isEmpty()) {
					voiceId = samplePath;
				}
			}
		}
	}

	// Generate audio
	auto synthesis = _textToSpeech->synthesize(text, voiceId, speed, pitch);

	if (!synthesis.success) {
		result["success"] = false;
		result["error"] = "TTS synthesis failed: " + synthesis.error;
		return result;
	}

	// Look up chat history for sending
	PeerId peerId(chatId);
	auto history = _session->data().history(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	// Compute waveform for chat visualization
	VoiceWaveform waveform;
	if (!synthesis.outputPath.isEmpty()) {
		Core::FileLocation loc(synthesis.outputPath);
		waveform = audioCountWaveform(loc, QByteArray());
	}
	if (waveform.isEmpty()) {
		// Generate synthetic flat waveform as fallback
		int waveformSize = qMax(1, static_cast<int>(synthesis.durationSeconds * 10));
		waveform.resize(waveformSize);
		for (int i = 0; i < waveformSize; ++i) {
			waveform[i] = 20;
		}
	}

	// Duration in milliseconds
	crl::time durationMs = static_cast<crl::time>(
		synthesis.durationSeconds * 1000.0);

	// Send as voice message via Telegram API
	Api::SendAction action(history);
	_session->api().sendVoiceMessage(
		synthesis.audioData,
		waveform,
		durationMs,
		false,
		action);

	result["success"] = true;
	result["chat_id"] = chatId;
	result["text"] = text;
	result["persona"] = persona;
	result["provider"] = synthesis.provider;
	result["duration_seconds"] = synthesis.durationSeconds;
	result["status"] = "sent";

	return result;
}

// Text to Video Tools
QJsonObject Server::toolTextToVideo(const QJsonObject &args) {
	QJsonObject result;
	QString text = args["text"].toString();
	QString preset = args.value("preset").toString("default");

	if (text.isEmpty()) {
		result["error"] = "Missing text parameter";
		result["success"] = false;
		return result;
	}

	result["success"] = true;
	result["text"] = text;
	result["preset"] = preset;
	result["status"] = "video_generation_service_required";
	result["note"] = "Video circle generation requires external API integration";

	return result;
}

QJsonObject Server::toolSendVideoReply(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString text = args["text"].toString();
	QString preset = args.value("preset").toString("default");

	if (chatId == 0 || text.isEmpty()) {
		result["error"] = "Missing chat_id or text";
		result["success"] = false;
		return result;
	}

	// Check if avatar source exists
	QSqlQuery query(_db);
	query.prepare("SELECT source_path FROM video_avatar WHERE name = ?");
	query.addBindValue(preset);

	if (query.exec() && query.next()) {
		result["avatar_source"] = query.value(0).toString();
	}

	result["success"] = true;
	result["chat_id"] = chatId;
	result["text"] = text;
	result["preset"] = preset;
	result["status"] = "pending_generation";
	result["note"] = "Video circle generation requires external rendering service";

	return result;
}

QJsonObject Server::toolUploadAvatarSource(const QJsonObject &args) {
	QJsonObject result;
	QString name = args["name"].toString();
	QString filePath = args["file_path"].toString();

	if (name.isEmpty() || filePath.isEmpty()) {
		result["error"] = "Missing name or file_path parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO video_avatar (name, source_path, created_at) "
				  "VALUES (?, ?, datetime('now'))");
	query.addBindValue(name);
	query.addBindValue(filePath);

	if (query.exec()) {
		result["success"] = true;
		result["name"] = name;
		result["file_path"] = filePath;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save avatar source";
	}

	return result;
}

QJsonObject Server::toolListAvatarPresets(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT name, source_path, created_at FROM video_avatar");

	QJsonArray presets;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject preset;
			preset["name"] = query.value(0).toString();
			preset["source_path"] = query.value(1).toString();
			preset["created_at"] = query.value(2).toString();
			presets.append(preset);
		}
	}

	result["success"] = true;
	result["presets"] = presets;
	result["count"] = presets.size();

	return result;
}

// Auto-Reply Rules Tools
QJsonObject Server::toolCreateAutoReplyRule(const QJsonObject &args) {
	QJsonObject result;
	QString name = args["name"].toString();
	QJsonObject triggers = args["triggers"].toObject();
	QString response = args["response"].toString();
	int priority = args.value("priority").toInt(5);

	if (name.isEmpty() || response.isEmpty()) {
		result["error"] = "Missing name or response parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT INTO chat_rules (chat_id, rule_name, rule_type, conditions, actions, enabled, priority, created_at) "
				  "VALUES (0, ?, 'auto_reply', ?, ?, 1, ?, datetime('now'))");
	query.addBindValue(name);
	query.addBindValue(QJsonDocument(triggers).toJson(QJsonDocument::Compact));
	QJsonObject actions;
	actions["response"] = response;
	query.addBindValue(QJsonDocument(actions).toJson(QJsonDocument::Compact));
	query.addBindValue(priority);

	if (query.exec()) {
		result["success"] = true;
		result["id"] = query.lastInsertId().toLongLong();
		result["name"] = name;
	} else {
		result["success"] = false;
		result["error"] = "Failed to create auto-reply rule";
	}

	return result;
}

QJsonObject Server::toolListAutoReplyRules(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT id, rule_name, conditions, actions, enabled, priority, times_triggered "
				  "FROM chat_rules WHERE rule_type = 'auto_reply' ORDER BY priority");

	QJsonArray rules;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject rule;
			rule["id"] = query.value(0).toLongLong();
			rule["name"] = query.value(1).toString();
			rule["triggers"] = QJsonDocument::fromJson(query.value(2).toByteArray()).object();
			rule["actions"] = QJsonDocument::fromJson(query.value(3).toByteArray()).object();
			rule["enabled"] = query.value(4).toBool();
			rule["priority"] = query.value(5).toInt();
			rule["times_triggered"] = query.value(6).toInt();
			rules.append(rule);
		}
	}

	result["success"] = true;
	result["rules"] = rules;
	result["count"] = rules.size();

	return result;
}

QJsonObject Server::toolUpdateAutoReplyRule(const QJsonObject &args) {
	QJsonObject result;
	qint64 ruleId = args["rule_id"].toVariant().toLongLong();
	QString name = args.value("name").toString();
	QJsonObject triggers = args.value("triggers").toObject();
	QString response = args.value("response").toString();
	bool enabled = args.value("enabled").toBool(true);

	QStringList updates;
	QVariantList values;

	if (!name.isEmpty()) { updates << "rule_name = ?"; values << name; }
	if (!triggers.isEmpty()) {
		updates << "conditions = ?";
		values << QJsonDocument(triggers).toJson(QJsonDocument::Compact);
	}
	if (!response.isEmpty()) {
		QJsonObject actions;
		actions["response"] = response;
		updates << "actions = ?";
		values << QJsonDocument(actions).toJson(QJsonDocument::Compact);
	}
	updates << "enabled = ?";
	values << enabled;

	if (updates.isEmpty()) {
		result["error"] = "No update fields provided";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("UPDATE chat_rules SET " + updates.join(", ") + " WHERE id = ? AND rule_type = 'auto_reply'");
	for (const auto &val : values) {
		query.addBindValue(val);
	}
	query.addBindValue(ruleId);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["rule_id"] = ruleId;
	} else {
		result["success"] = false;
		result["error"] = "Rule not found or update failed";
	}

	return result;
}

QJsonObject Server::toolDeleteAutoReplyRule(const QJsonObject &args) {
	QJsonObject result;
	qint64 ruleId = args["rule_id"].toVariant().toLongLong();

	QSqlQuery query(_db);
	query.prepare("DELETE FROM chat_rules WHERE id = ? AND rule_type = 'auto_reply'");
	query.addBindValue(ruleId);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["deleted"] = true;
	} else {
		result["success"] = false;
		result["error"] = "Rule not found";
	}

	return result;
}

QJsonObject Server::toolTestAutoReplyRule(const QJsonObject &args) {
	QJsonObject result;
	QString testMessage = args["message"].toString();

	if (testMessage.isEmpty()) {
		result["error"] = "Missing message parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("SELECT rule_name, conditions, actions FROM chat_rules "
				  "WHERE rule_type = 'auto_reply' AND enabled = 1 ORDER BY priority");

	QJsonArray matchedRules;
	if (query.exec()) {
		while (query.next()) {
			QString ruleName = query.value(0).toString();
			QJsonObject triggers = QJsonDocument::fromJson(query.value(1).toByteArray()).object();
			QJsonObject actions = QJsonDocument::fromJson(query.value(2).toByteArray()).object();

			// Check keyword triggers
			bool matches = false;
			if (triggers.contains("keywords")) {
				QJsonArray keywords = triggers["keywords"].toArray();
				for (const auto &kw : keywords) {
					if (testMessage.contains(kw.toString(), Qt::CaseInsensitive)) {
						matches = true;
						break;
					}
				}
			}

			if (matches) {
				QJsonObject matched;
				matched["rule_name"] = ruleName;
				matched["response"] = actions["response"].toString();
				matchedRules.append(matched);
			}
		}
	}

	result["success"] = true;
	result["test_message"] = testMessage;
	result["matched_rules"] = matchedRules;
	result["would_reply"] = matchedRules.size() > 0;

	return result;
}

QJsonObject Server::toolGetAutoReplyStats(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT COUNT(*), SUM(times_triggered) FROM chat_rules WHERE rule_type = 'auto_reply'");

	if (query.exec() && query.next()) {
		result["total_rules"] = query.value(0).toInt();
		result["total_triggered"] = query.value(1).toInt();
		result["success"] = true;
	} else {
		result["total_rules"] = 0;
		result["total_triggered"] = 0;
		result["success"] = true;
	}

	return result;
}


} // namespace MCP
