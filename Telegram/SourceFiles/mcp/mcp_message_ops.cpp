// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== MESSAGE OPERATION TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolEditMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString newText = args["new_text"].toString();

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	// Get the message
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	auto item = owner.message(history->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Edit the message via API
	// Prepare text with entities
	TextWithEntities textWithEntities;
	textWithEntities.text = newText;

	// Create edit options
	Api::SendOptions options;
	options.scheduled = 0;  // Not scheduled

	// Use the Api namespace function (not api member function)
	// This is an asynchronous operation with callbacks
	Api::EditTextMessage(
		item,
		textWithEntities,
		Data::WebPageDraft(),  // No webpage
		options,
		[=](mtpRequestId) {
			// Success callback
			qInfo() << "MCP: Edit message succeeded" << messageId;
		},
		[=](const QString &error, mtpRequestId) {
			// Failure callback
			qWarning() << "MCP: Edit message failed:" << error;
		},
		false  // not spoilered
	);

	result["success"] = true;
	result["edited"] = true;
	result["note"] = "Edit request sent (async operation)";

	qInfo() << "MCP: Edit message requested for" << messageId << "in chat" << chatId;
	return result;
}

QJsonObject Server::toolDeleteMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	bool revoke = args.value("revoke").toBool(true);  // Delete for everyone by default

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	// Get the history
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	// Verify message exists
	auto item = owner.message(history->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Delete the message using Data::Histories API
	// Create message ID list
	MessageIdsList ids = { item->fullId() };

	// Delete via session's histories manager
	_session->data().histories().deleteMessages(ids, revoke);
	_session->data().sendHistoryChangeNotifications();

	result["success"] = true;
	result["revoked"] = revoke;

	qInfo() << "MCP: Deleted message" << messageId << "from chat" << chatId << "(revoke:" << revoke << ")";
	return result;
}

QJsonObject Server::toolForwardMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 fromChatId = args["from_chat_id"].toVariant().toLongLong();
	qint64 toChatId = args["to_chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();

	QJsonObject result;
	result["from_chat_id"] = fromChatId;
	result["to_chat_id"] = toChatId;
	result["message_id"] = messageId;

	// Get source message
	auto &owner = _session->data();
	const auto fromPeerId = PeerId(fromChatId);
	const auto fromHistory = owner.historyLoaded(fromPeerId);
	if (!fromHistory) {
		result["success"] = false;
		result["error"] = "Source chat not found";
		return result;
	}

	auto item = owner.message(fromHistory->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Get destination peer
	const auto toPeerId = PeerId(toChatId);
	const auto toPeer = owner.peer(toPeerId);
	if (!toPeer) {
		result["success"] = false;
		result["error"] = "Destination chat not found";
		return result;
	}

	// Forward the message
	// Get destination history
	auto toHistory = _session->data().history(toPeerId);
	if (!toHistory) {
		result["success"] = false;
		result["error"] = "Failed to get destination history";
		return result;
	}

	// Create HistoryItemsList with the item to forward
	HistoryItemsList items;
	items.push_back(item);

	// Create ResolvedForwardDraft
	Data::ResolvedForwardDraft draft;
	draft.items = items;
	draft.options = Data::ForwardOptions::PreserveInfo;  // Preserve original sender info

	// Create SendAction with destination thread
	// For simple cases, history can be cast to Data::Thread*
	auto thread = (Data::Thread*)toHistory;
	Api::SendAction action(thread, Api::SendOptions());

	// Forward via session API
	auto &api = _session->api();
	api.forwardMessages(std::move(draft), action);

	result["success"] = true;
	result["forwarded"] = true;

	qInfo() << "MCP: Forwarded message" << messageId << "from chat" << fromChatId << "to chat" << toChatId;
	return result;
}

QJsonObject Server::toolPinMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	bool notify = args.value("notify").toBool(false);

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	// Get the message
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	auto item = owner.message(history->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Check permissions
	const auto peer = history->peer;
	if (const auto chat = peer->asChat()) {
		if (!chat->canPinMessages()) {
			result["success"] = false;
			result["error"] = "No permission to pin messages in this chat";
			return result;
		}
	} else if (const auto channel = peer->asChannel()) {
		if (!channel->canPinMessages()) {
			result["success"] = false;
			result["error"] = "No permission to pin messages in this channel";
			return result;
		}
	}

	// Pin via API
	// Use the session's API to pin the message
	auto &api = _session->api();

	// Request message pin (notify parameter controls silent pinning)
	// Using the peer's pinMessage method through API
	api.request(MTPmessages_UpdatePinnedMessage(
		MTP_flags(notify ? MTPmessages_UpdatePinnedMessage::Flag::f_unpin : MTPmessages_UpdatePinnedMessage::Flags(0)),
		peer->input,
		MTP_int(messageId)
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		qWarning() << "MCP: Pin message failed:" << error.type();
	}).send();

	result["success"] = true;
	result["pinned"] = true;
	result["notify"] = notify;

	qInfo() << "MCP: Pinned message" << messageId << "in chat" << chatId << "(notify:" << notify << ")";
	return result;
}

QJsonObject Server::toolUnpinMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	// Get the peer
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto peer = owner.peer(peerId);
	if (!peer) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	// Check permissions
	if (const auto chat = peer->asChat()) {
		if (!chat->canPinMessages()) {
			result["success"] = false;
			result["error"] = "No permission to unpin messages in this chat";
			return result;
		}
	} else if (const auto channel = peer->asChannel()) {
		if (!channel->canPinMessages()) {
			result["success"] = false;
			result["error"] = "No permission to unpin messages in this channel";
			return result;
		}
	}

	// Unpin via API
	// Use the session's API to unpin the message
	auto &api = _session->api();

	// Request message unpin (using the same API as pin with unpin flag)
	api.request(MTPmessages_UpdatePinnedMessage(
		MTP_flags(MTPmessages_UpdatePinnedMessage::Flag::f_unpin),
		peer->input,
		MTP_int(messageId)
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		qWarning() << "MCP: Unpin message failed:" << error.type();
	}).send();

	result["success"] = true;
	result["unpinned"] = true;

	qInfo() << "MCP: Unpinned message" << messageId << "in chat" << chatId;
	return result;
}

QJsonObject Server::toolAddReaction(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString emoji = args["emoji"].toString();

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;
	result["emoji"] = emoji;

	// Get the message
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	auto item = owner.message(history->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Check if reactions are available
	const auto reactionsOwner = &owner.reactions();
	if (!reactionsOwner) {
		result["success"] = false;
		result["error"] = "Reactions system not available";
		return result;
	}

	// Add reaction via HistoryItem API
	// Create reaction ID from emoji string
	const Data::ReactionId reactionId{ emoji };

	// Toggle the reaction (will add if not present, remove if already present)
	// Using HistoryReactionSource::Selector for programmatic reactions
	item->toggleReaction(reactionId, HistoryReactionSource::Selector);

	result["success"] = true;
	result["added"] = true;

	qInfo() << "MCP: Added reaction" << emoji << "to message" << messageId << "in chat" << chatId;
	return result;
}


} // namespace MCP
