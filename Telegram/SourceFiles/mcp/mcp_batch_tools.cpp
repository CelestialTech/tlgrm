// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== BATCH OPERATION TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolBatchSend(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	QJsonArray chatIdsArray = args["chat_ids"].toArray();
	QString text = args["message"].toString();

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all chat IDs and send message to each
	for (const auto &chatIdVal : chatIdsArray) {
		qint64 chatId = chatIdVal.toVariant().toLongLong();

		// Create args for single send
		QJsonObject sendArgs;
		sendArgs["chat_id"] = chatId;
		sendArgs["text"] = text;

		// Call single send_message implementation
		QJsonObject sendResult = toolSendMessage(sendArgs);

		// Track success/failure
		if (sendResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject chatResult;
		chatResult["chat_id"] = chatId;
		chatResult["success"] = sendResult.value("success").toBool();
		if (sendResult.contains("error")) {
			chatResult["error"] = sendResult["error"];
		}
		results.append(chatResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["total_chats"] = chatIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["results"] = results;

	qInfo() << "MCP: Batch send to" << chatIdsArray.size() << "chats -" << successCount << "succeeded," << failureCount << "failed";

	return result;
}

QJsonObject Server::toolBatchDelete(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();
	bool revoke = args.value("revoke").toBool(true);

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all message IDs and delete each
	for (const auto &msgIdVal : messageIdsArray) {
		qint64 messageId = msgIdVal.toVariant().toLongLong();

		// Create args for single delete
		QJsonObject deleteArgs;
		deleteArgs["chat_id"] = chatId;
		deleteArgs["message_id"] = messageId;
		deleteArgs["revoke"] = revoke;

		// Call single delete_message implementation
		QJsonObject deleteResult = toolDeleteMessage(deleteArgs);

		// Track success/failure
		if (deleteResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject msgResult;
		msgResult["message_id"] = messageId;
		msgResult["success"] = deleteResult.value("success").toBool();
		if (deleteResult.contains("error")) {
			msgResult["error"] = deleteResult["error"];
		}
		results.append(msgResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["chat_id"] = chatId;
	result["total_messages"] = messageIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["revoke"] = revoke;
	result["results"] = results;

	qInfo() << "MCP: Batch delete" << messageIdsArray.size() << "messages from chat" << chatId << "-" << successCount << "succeeded," << failureCount << "failed";

	return result;
}

QJsonObject Server::toolBatchForward(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 fromChatId = args["from_chat_id"].toVariant().toLongLong();
	qint64 toChatId = args["to_chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all message IDs and forward each
	for (const auto &msgIdVal : messageIdsArray) {
		qint64 messageId = msgIdVal.toVariant().toLongLong();

		// Create args for single forward
		QJsonObject forwardArgs;
		forwardArgs["from_chat_id"] = fromChatId;
		forwardArgs["to_chat_id"] = toChatId;
		forwardArgs["message_id"] = messageId;

		// Call single forward_message implementation
		QJsonObject forwardResult = toolForwardMessage(forwardArgs);

		// Track success/failure
		if (forwardResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject msgResult;
		msgResult["message_id"] = messageId;
		msgResult["success"] = forwardResult.value("success").toBool();
		if (forwardResult.contains("error")) {
			msgResult["error"] = forwardResult["error"];
		}
		results.append(msgResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["from_chat_id"] = fromChatId;
	result["to_chat_id"] = toChatId;
	result["total_messages"] = messageIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["results"] = results;

	qInfo() << "MCP: Batch forward" << messageIdsArray.size() << "messages from chat" << fromChatId << "to chat" << toChatId << "-" << successCount << "succeeded," << failureCount << "failed";

	return result;
}

QJsonObject Server::toolBatchPin(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();
	bool notify = args.value("notify").toBool(false);

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all message IDs and pin each
	for (const auto &msgIdVal : messageIdsArray) {
		qint64 messageId = msgIdVal.toVariant().toLongLong();

		// Create args for single pin
		QJsonObject pinArgs;
		pinArgs["chat_id"] = chatId;
		pinArgs["message_id"] = messageId;
		pinArgs["notify"] = notify;

		// Call single pin_message implementation
		QJsonObject pinResult = toolPinMessage(pinArgs);

		// Track success/failure
		if (pinResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject msgResult;
		msgResult["message_id"] = messageId;
		msgResult["success"] = pinResult.value("success").toBool();
		if (pinResult.contains("error")) {
			msgResult["error"] = pinResult["error"];
		}
		results.append(msgResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["chat_id"] = chatId;
	result["total_messages"] = messageIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["notify"] = notify;
	result["results"] = results;

	qInfo() << "MCP: Batch pin" << messageIdsArray.size() << "messages in chat" << chatId << "-" << successCount << "succeeded," << failureCount << "failed";

	return result;
}

QJsonObject Server::toolBatchReaction(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();
	QString emoji = args["emoji"].toString();

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all message IDs and add reaction to each
	for (const auto &msgIdVal : messageIdsArray) {
		qint64 messageId = msgIdVal.toVariant().toLongLong();

		// Create args for single reaction
		QJsonObject reactionArgs;
		reactionArgs["chat_id"] = chatId;
		reactionArgs["message_id"] = messageId;
		reactionArgs["emoji"] = emoji;

		// Call single add_reaction implementation
		QJsonObject reactionResult = toolAddReaction(reactionArgs);

		// Track success/failure
		if (reactionResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject msgResult;
		msgResult["message_id"] = messageId;
		msgResult["success"] = reactionResult.value("success").toBool();
		if (reactionResult.contains("error")) {
			msgResult["error"] = reactionResult["error"];
		}
		results.append(msgResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["chat_id"] = chatId;
	result["emoji"] = emoji;
	result["total_messages"] = messageIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["results"] = results;

	qInfo() << "MCP: Batch reaction" << emoji << "on" << messageIdsArray.size() << "messages in chat" << chatId << "-" << successCount << "succeeded," << failureCount << "failed";

	return result;
}


} // namespace MCP
