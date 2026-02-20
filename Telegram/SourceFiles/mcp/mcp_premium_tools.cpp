// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== PREMIUM EQUIVALENT FEATURES IMPLEMENTATION =====

// Voice Transcription Tools
QJsonObject Server::toolTranscribeVoiceMessage(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString language = args.value("language").toString("auto");

	// Initialize voice transcription if needed
	if (!_voiceTranscription) {
		_voiceTranscription.reset(new VoiceTranscription(this));
		_voiceTranscription->start(&_db);
	}

	// Check if we already have a transcription stored
	if (_voiceTranscription->hasTranscription(messageId)) {
		auto stored = _voiceTranscription->getStoredTranscription(messageId);
		if (stored.success) {
			result["success"] = true;
			result["chat_id"] = chatId;
			result["message_id"] = messageId;
			result["text"] = stored.text;
			result["language"] = stored.language;
			result["confidence"] = stored.confidence;
			result["duration_seconds"] = stored.durationSeconds;
			result["status"] = "completed";
			result["cached"] = true;
			return result;
		}
	}

	// Try to find the voice message file path from the session
	QString audioPath;
	if (_session) {
		auto item = _session->data().message(PeerId(chatId), MsgId(messageId));
		if (item && item->media()) {
			auto doc = item->media()->document();
			if (doc) {
				auto location = doc->location(true);
				if (!location.isEmpty()) {
					audioPath = location.name();
				}
			}
		}
	}

	if (audioPath.isEmpty()) {
		result["success"] = false;
		result["error"] = "Voice message file not found in local cache. The file may need to be downloaded first.";
		result["chat_id"] = chatId;
		result["message_id"] = messageId;
		return result;
	}

	// Run transcription
	if (!language.isEmpty() && language != "auto") {
		_voiceTranscription->setLanguage(language);
	}

	auto transcriptionResult = _voiceTranscription->transcribe(audioPath);

	// Store result
	if (transcriptionResult.success) {
		_voiceTranscription->storeTranscription(messageId, chatId, transcriptionResult);
	}

	result["success"] = transcriptionResult.success;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;
	result["text"] = transcriptionResult.text;
	result["language"] = transcriptionResult.language;
	result["confidence"] = transcriptionResult.confidence;
	result["duration_seconds"] = transcriptionResult.durationSeconds;
	result["model"] = transcriptionResult.modelUsed;
	result["provider"] = transcriptionResult.provider;
	result["status"] = transcriptionResult.success ? "completed" : "failed";

	if (!transcriptionResult.error.isEmpty()) {
		result["error"] = transcriptionResult.error;
	}

	return result;
}

QJsonObject Server::toolGetVoiceTranscription(const QJsonObject &args) {
	QJsonObject result;
	qint64 messageId = args["message_id"].toVariant().toLongLong();

	if (!_voiceTranscription) {
		_voiceTranscription.reset(new VoiceTranscription(this));
		_voiceTranscription->start(&_db);
	}

	auto stored = _voiceTranscription->getStoredTranscription(messageId);

	result["success"] = stored.success;
	result["message_id"] = messageId;

	if (stored.success) {
		result["text"] = stored.text;
		result["language"] = stored.language;
		result["confidence"] = stored.confidence;
		result["duration_seconds"] = stored.durationSeconds;
		result["model"] = stored.modelUsed;
		result["transcribed_at"] = stored.transcribedAt.toString(Qt::ISODate);
		result["status"] = "completed";
	} else {
		result["status"] = "not_found";
		result["error"] = "No transcription found for this message. Use transcribe_voice_message first.";
	}

	return result;
}

// Translation Tools
QJsonObject Server::toolTranslateMessage(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString targetLanguage = args["target_language"].toString();
	QString sourceLanguage = args.value("source_language").toString("auto");

	if (targetLanguage.isEmpty()) {
		result["error"] = "Missing target_language parameter";
		result["success"] = false;
		return result;
	}

	// Check translation cache first
	QSqlQuery query(_db);
	query.prepare("SELECT translated_text, detected_language FROM translation_cache "
				  "WHERE chat_id = ? AND message_id = ? AND target_language = ?");
	query.addBindValue(chatId);
	query.addBindValue(messageId);
	query.addBindValue(targetLanguage);

	if (query.exec() && query.next()) {
		result["success"] = true;
		result["translated_text"] = query.value(0).toString();
		result["detected_language"] = query.value(1).toString();
		result["target_language"] = targetLanguage;
		result["cached"] = true;
		return result;
	}

	// Get original message text
	QString originalText;
	if (_session) {
		auto &owner = _session->data();
		auto item = owner.message(PeerId(chatId), MsgId(messageId));
		if (item) {
			originalText = item->originalText().text;
		}
	}

	if (originalText.isEmpty()) {
		result["error"] = "Message not found or has no text";
		result["success"] = false;
		return result;
	}

	// Translate using local LLM if available
	if (_localLLM && _localLLM->isRunning()) {
		auto completion = _localLLM->translate(
			originalText,
			targetLanguage,
			(sourceLanguage == "auto") ? QString() : sourceLanguage);

		if (completion.success) {
			// Store translated text in cache
			QSqlQuery insertQuery(_db);
			insertQuery.prepare(R"(
				INSERT OR REPLACE INTO translation_cache
				(chat_id, message_id, original_text, translated_text, source_language, target_language, created_at)
				VALUES (?, ?, ?, ?, ?, ?, ?)
			)");
			insertQuery.addBindValue(chatId);
			insertQuery.addBindValue(messageId);
			insertQuery.addBindValue(originalText);
			insertQuery.addBindValue(completion.text);
			insertQuery.addBindValue(sourceLanguage);
			insertQuery.addBindValue(targetLanguage);
			insertQuery.addBindValue(QDateTime::currentSecsSinceEpoch());
			insertQuery.exec();

			result["success"] = true;
			result["original_text"] = originalText;
			result["translated_text"] = completion.text;
			result["target_language"] = targetLanguage;
			result["source_language"] = sourceLanguage;
			result["model"] = completion.model;
			result["duration_seconds"] = completion.durationSeconds;
			result["cached"] = false;
			return result;
		}

		// LLM failed - fall through to pending state
		result["llm_error"] = completion.error;
	}

	// No LLM or LLM failed - store original for later
	QSqlQuery insertQuery(_db);
	insertQuery.prepare(R"(
		INSERT OR IGNORE INTO translation_cache
		(chat_id, message_id, original_text, source_language, target_language, created_at)
		VALUES (?, ?, ?, ?, ?, ?)
	)");
	insertQuery.addBindValue(chatId);
	insertQuery.addBindValue(messageId);
	insertQuery.addBindValue(originalText);
	insertQuery.addBindValue(sourceLanguage);
	insertQuery.addBindValue(targetLanguage);
	insertQuery.addBindValue(QDateTime::currentSecsSinceEpoch());
	insertQuery.exec();

	result["success"] = true;
	result["original_text"] = originalText;
	result["target_language"] = targetLanguage;
	result["source_language"] = sourceLanguage;
	result["status"] = "translation_pending";
	result["note"] = "No local LLM available for translation. "
		"Install Ollama (ollama.com) and pull a model to enable translation.";

	return result;
}

QJsonObject Server::toolGetTranslationHistory(const QJsonObject &args) {
	QJsonObject result;
	int limit = args.value("limit").toInt(50);
	QString targetLanguage = args.value("target_language").toString();

	QSqlQuery query(_db);
	QString sql = "SELECT chat_id, message_id, original_text, translated_text, "
				  "source_language, target_language, created_at "
				  "FROM translation_cache ";

	if (!targetLanguage.isEmpty()) {
		sql += "WHERE target_language = ? ";
	}
	sql += "ORDER BY created_at DESC LIMIT ?";

	query.prepare(sql);
	if (!targetLanguage.isEmpty()) {
		query.addBindValue(targetLanguage);
	}
	query.addBindValue(limit);

	QJsonArray translations;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject translation;
			translation["chat_id"] = query.value(0).toLongLong();
			translation["message_id"] = query.value(1).toLongLong();
			translation["original_text"] = query.value(2).toString();
			translation["translated_text"] = query.value(3).toString();
			translation["source_language"] = query.value(4).toString();
			translation["target_language"] = query.value(5).toString();
			translation["created_at"] = query.value(6).toString();
			translations.append(translation);
		}
	}

	result["success"] = true;
	result["translations"] = translations;
	result["count"] = translations.size();

	return result;
}

// Message Tags Tools
QJsonObject Server::toolAddMessageTag(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString tagName = args["tag"].toString();
	QString color = args.value("color").toString("#3390ec");

	if (tagName.isEmpty()) {
		result["error"] = "Missing tag parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO message_tags (chat_id, message_id, tag_name, color, created_at) "
				  "VALUES (?, ?, ?, ?, datetime('now'))");
	query.addBindValue(chatId);
	query.addBindValue(messageId);
	query.addBindValue(tagName);
	query.addBindValue(color);

	if (query.exec()) {
		result["success"] = true;
		result["chat_id"] = chatId;
		result["message_id"] = messageId;
		result["tag"] = tagName;
		result["color"] = color;
	} else {
		result["success"] = false;
		result["error"] = "Failed to add tag: " + query.lastError().text();
	}

	return result;
}

QJsonObject Server::toolGetMessageTags(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	qint64 messageId = args.value("message_id").toVariant().toLongLong();

	QSqlQuery query(_db);
	QString sql = "SELECT DISTINCT tag_name, color, COUNT(*) as usage_count "
				  "FROM message_tags ";

	QStringList conditions;
	if (chatId > 0) conditions << "chat_id = ?";
	if (messageId > 0) conditions << "message_id = ?";

	if (!conditions.isEmpty()) {
		sql += "WHERE " + conditions.join(" AND ") + " ";
	}
	sql += "GROUP BY tag_name, color ORDER BY usage_count DESC";

	query.prepare(sql);
	if (chatId > 0) query.addBindValue(chatId);
	if (messageId > 0) query.addBindValue(messageId);

	QJsonArray tags;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject tag;
			tag["name"] = query.value(0).toString();
			tag["color"] = query.value(1).toString();
			tag["usage_count"] = query.value(2).toInt();
			tags.append(tag);
		}
	}

	result["success"] = true;
	result["tags"] = tags;
	result["count"] = tags.size();

	return result;
}

QJsonObject Server::toolRemoveMessageTag(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString tagName = args["tag"].toString();

	QSqlQuery query(_db);
	query.prepare("DELETE FROM message_tags WHERE chat_id = ? AND message_id = ? AND tag_name = ?");
	query.addBindValue(chatId);
	query.addBindValue(messageId);
	query.addBindValue(tagName);

	if (query.exec()) {
		result["success"] = true;
		result["removed"] = query.numRowsAffected() > 0;
		result["chat_id"] = chatId;
		result["message_id"] = messageId;
		result["tag"] = tagName;
	} else {
		result["success"] = false;
		result["error"] = "Failed to remove tag: " + query.lastError().text();
	}

	return result;
}

QJsonObject Server::toolSearchByTag(const QJsonObject &args) {
	QJsonObject result;
	QString tagName = args["tag"].toString();
	int limit = args.value("limit").toInt(50);

	if (tagName.isEmpty()) {
		result["error"] = "Missing tag parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("SELECT chat_id, message_id, created_at FROM message_tags "
				  "WHERE tag_name = ? ORDER BY created_at DESC LIMIT ?");
	query.addBindValue(tagName);
	query.addBindValue(limit);

	QJsonArray messages;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject msg;
			msg["chat_id"] = query.value(0).toLongLong();
			msg["message_id"] = query.value(1).toLongLong();
			msg["tagged_at"] = query.value(2).toString();
			messages.append(msg);
		}
	}

	result["success"] = true;
	result["tag"] = tagName;
	result["messages"] = messages;
	result["count"] = messages.size();

	return result;
}

QJsonObject Server::toolGetTagSuggestions(const QJsonObject &args) {
	QJsonObject result;
	QString messageText = args.value("text").toString();
	int limit = args.value("limit").toInt(5);

	// Get most commonly used tags as suggestions
	QSqlQuery query(_db);
	query.prepare("SELECT tag_name, COUNT(*) as count FROM message_tags "
				  "GROUP BY tag_name ORDER BY count DESC LIMIT ?");
	query.addBindValue(limit);

	QJsonArray suggestions;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject suggestion;
			suggestion["tag"] = query.value(0).toString();
			suggestion["usage_count"] = query.value(1).toInt();
			suggestions.append(suggestion);
		}
	}

	result["success"] = true;
	result["suggestions"] = suggestions;

	return result;
}

// Ad Filtering Tools
QJsonObject Server::toolConfigureAdFilter(const QJsonObject &args) {
	QJsonObject result;
	bool enabled = args.value("enabled").toBool(true);
	QJsonArray keywords = args.value("keywords").toArray();
	QJsonArray excludeChats = args.value("exclude_chats").toArray();

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO ad_filter_config (id, enabled, keywords, exclude_chats, updated_at) "
				  "VALUES (1, ?, ?, ?, datetime('now'))");
	query.addBindValue(enabled);
	query.addBindValue(QJsonDocument(keywords).toJson(QJsonDocument::Compact));
	query.addBindValue(QJsonDocument(excludeChats).toJson(QJsonDocument::Compact));

	if (query.exec()) {
		result["success"] = true;
		result["enabled"] = enabled;
		result["keywords_count"] = keywords.size();
		result["exclude_chats_count"] = excludeChats.size();
	} else {
		result["success"] = false;
		result["error"] = "Failed to save ad filter config";
	}

	return result;
}

QJsonObject Server::toolGetAdFilterStats(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery configQuery(_db);
	configQuery.prepare("SELECT enabled, keywords, exclude_chats, ads_blocked, last_blocked_at "
						"FROM ad_filter_config WHERE id = 1");

	if (configQuery.exec() && configQuery.next()) {
		result["enabled"] = configQuery.value(0).toBool();
		result["keywords"] = QJsonDocument::fromJson(configQuery.value(1).toByteArray()).array();
		result["exclude_chats"] = QJsonDocument::fromJson(configQuery.value(2).toByteArray()).array();
		result["ads_blocked"] = configQuery.value(3).toInt();
		result["last_blocked_at"] = configQuery.value(4).toString();
		result["success"] = true;
	} else {
		result["enabled"] = false;
		result["ads_blocked"] = 0;
		result["success"] = true;
		result["note"] = "No ad filter configuration found";
	}

	return result;
}

// Chat Rules Tools
QJsonObject Server::toolSetChatRules(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString ruleName = args["rule_name"].toString();
	QString ruleType = args["rule_type"].toString();
	QJsonObject conditions = args["conditions"].toObject();
	QJsonObject actions = args["actions"].toObject();

	if (ruleName.isEmpty() || ruleType.isEmpty()) {
		result["error"] = "Missing rule_name or rule_type";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO chat_rules (chat_id, rule_name, rule_type, conditions, actions, enabled, created_at) "
				  "VALUES (?, ?, ?, ?, ?, 1, datetime('now'))");
	query.addBindValue(chatId);
	query.addBindValue(ruleName);
	query.addBindValue(ruleType);
	query.addBindValue(QJsonDocument(conditions).toJson(QJsonDocument::Compact));
	query.addBindValue(QJsonDocument(actions).toJson(QJsonDocument::Compact));

	if (query.exec()) {
		result["success"] = true;
		result["chat_id"] = chatId;
		result["rule_name"] = ruleName;
		result["rule_type"] = ruleType;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save chat rule: " + query.lastError().text();
	}

	return result;
}

QJsonObject Server::toolGetChatRules(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();

	QSqlQuery query(_db);
	QString sql = "SELECT rule_name, rule_type, conditions, actions, enabled, created_at "
				  "FROM chat_rules ";
	if (chatId > 0) {
		sql += "WHERE chat_id = ? ";
	}
	sql += "ORDER BY created_at DESC";

	query.prepare(sql);
	if (chatId > 0) {
		query.addBindValue(chatId);
	}

	QJsonArray rules;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject rule;
			rule["rule_name"] = query.value(0).toString();
			rule["rule_type"] = query.value(1).toString();
			rule["conditions"] = QJsonDocument::fromJson(query.value(2).toByteArray()).object();
			rule["actions"] = QJsonDocument::fromJson(query.value(3).toByteArray()).object();
			rule["enabled"] = query.value(4).toBool();
			rule["created_at"] = query.value(5).toString();
			rules.append(rule);
		}
	}

	result["success"] = true;
	result["rules"] = rules;
	result["count"] = rules.size();

	return result;
}

QJsonObject Server::toolTestChatRules(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString testMessage = args["test_message"].toString();

	if (testMessage.isEmpty()) {
		result["error"] = "Missing test_message parameter";
		result["success"] = false;
		return result;
	}

	// Get rules for this chat
	QSqlQuery query(_db);
	query.prepare("SELECT rule_name, rule_type, conditions, actions FROM chat_rules "
				  "WHERE (chat_id = ? OR chat_id = 0) AND enabled = 1");
	query.addBindValue(chatId);

	QJsonArray matchedRules;
	if (query.exec()) {
		while (query.next()) {
			QString ruleName = query.value(0).toString();
			QString ruleType = query.value(1).toString();
			QJsonObject conditions = QJsonDocument::fromJson(query.value(2).toByteArray()).object();
			QJsonObject actions = QJsonDocument::fromJson(query.value(3).toByteArray()).object();

			// Simple keyword matching for testing
			bool matches = false;
			if (conditions.contains("keywords")) {
				QJsonArray keywords = conditions["keywords"].toArray();
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
				matched["rule_type"] = ruleType;
				matched["actions"] = actions;
				matchedRules.append(matched);
			}
		}
	}

	result["success"] = true;
	result["test_message"] = testMessage;
	result["matched_rules"] = matchedRules;
	result["would_trigger"] = matchedRules.size() > 0;

	return result;
}

// Tasks Tools
QJsonObject Server::toolCreateTaskFromMessage(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString title = args.value("title").toString();
	QString dueDate = args.value("due_date").toString();
	int priority = args.value("priority").toInt(2);  // 1=high, 2=medium, 3=low

	// Get message text if title not provided
	if (title.isEmpty() && _session) {
		auto &owner = _session->data();
		auto item = owner.message(PeerId(chatId), MsgId(messageId));
		if (item) {
			title = item->originalText().text.left(100);
		}
	}

	if (title.isEmpty()) {
		result["error"] = "Could not determine task title";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT INTO tasks (chat_id, message_id, title, status, priority, due_date, created_at) "
				  "VALUES (?, ?, ?, 'pending', ?, ?, datetime('now'))");
	query.addBindValue(chatId);
	query.addBindValue(messageId);
	query.addBindValue(title);
	query.addBindValue(priority);
	query.addBindValue(dueDate.isEmpty() ? QVariant() : dueDate);

	if (query.exec()) {
		result["success"] = true;
		result["task_id"] = query.lastInsertId().toLongLong();
		result["title"] = title;
		result["status"] = "pending";
		result["priority"] = priority;
		if (!dueDate.isEmpty()) {
			result["due_date"] = dueDate;
		}
	} else {
		result["success"] = false;
		result["error"] = "Failed to create task: " + query.lastError().text();
	}

	return result;
}

QJsonObject Server::toolListTasks(const QJsonObject &args) {
	QJsonObject result;
	QString status = args.value("status").toString();
	int limit = args.value("limit").toInt(50);

	QSqlQuery query(_db);
	QString sql = "SELECT id, chat_id, message_id, title, status, priority, due_date, created_at, completed_at "
				  "FROM tasks ";
	if (!status.isEmpty()) {
		sql += "WHERE status = ? ";
	}
	sql += "ORDER BY priority ASC, due_date ASC NULLS LAST LIMIT ?";

	query.prepare(sql);
	if (!status.isEmpty()) {
		query.addBindValue(status);
	}
	query.addBindValue(limit);

	QJsonArray tasks;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject task;
			task["id"] = query.value(0).toLongLong();
			task["chat_id"] = query.value(1).toLongLong();
			task["message_id"] = query.value(2).toLongLong();
			task["title"] = query.value(3).toString();
			task["status"] = query.value(4).toString();
			task["priority"] = query.value(5).toInt();
			if (!query.value(6).isNull()) {
				task["due_date"] = query.value(6).toString();
			}
			task["created_at"] = query.value(7).toString();
			if (!query.value(8).isNull()) {
				task["completed_at"] = query.value(8).toString();
			}
			tasks.append(task);
		}
	}

	result["success"] = true;
	result["tasks"] = tasks;
	result["count"] = tasks.size();

	return result;
}

QJsonObject Server::toolUpdateTask(const QJsonObject &args) {
	QJsonObject result;
	qint64 taskId = args["task_id"].toVariant().toLongLong();
	QString status = args.value("status").toString();
	QString title = args.value("title").toString();
	int priority = args.value("priority").toInt(-1);

	QStringList updates;
	QVariantList values;

	if (!status.isEmpty()) {
		updates << "status = ?";
		values << status;
		if (status == "completed") {
			updates << "completed_at = datetime('now')";
		}
	}
	if (!title.isEmpty()) {
		updates << "title = ?";
		values << title;
	}
	if (priority >= 1 && priority <= 3) {
		updates << "priority = ?";
		values << priority;
	}

	if (updates.isEmpty()) {
		result["error"] = "No update fields provided";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("UPDATE tasks SET " + updates.join(", ") + " WHERE id = ?");
	for (const auto &val : values) {
		query.addBindValue(val);
	}
	query.addBindValue(taskId);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["task_id"] = taskId;
		result["updated"] = true;
	} else {
		result["success"] = false;
		result["error"] = "Task not found or update failed";
	}

	return result;
}


} // namespace MCP
