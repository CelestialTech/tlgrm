// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== CORE TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolListChats(const QJsonObject &args) {
	Q_UNUSED(args);

	// Check cache first
	if (_cache) {
		QJsonObject cached;
		if (_cache->get(_cache->chatListKey(), cached)) {
			// Cache hit - return immediately
			cached["source"] = cached["source"].toString() + " (cached)";
			return cached;
		}
	}

	QJsonArray chats;

	// Try live data first if session is available
	if (_session) {
		auto chatsList = _session->data().chatsList();  // Main folder chat list
		if (chatsList) {
			auto indexed = chatsList->indexed();
			if (indexed) {
				for (const auto &row : *indexed) {
					if (!row) continue;
					auto thread = row->thread();
					if (!thread) continue;
					auto peer = thread->peer();
					if (!peer) continue;

					QJsonObject chat;
					chat["id"] = QString::number(peer->id.value);
					chat["name"] = peer->name();
					chat["username"] = peer->username();
					chat["source"] = "live";

					// Determine chat type
					if (peer->isUser()) {
						chat["type"] = "user";
						auto user = peer->asUser();
						if (user && user->isBot()) {
							chat["is_bot"] = true;
						}
					} else if (peer->isChat()) {
						chat["type"] = "group";
					} else if (peer->isChannel()) {
						auto channel = peer->asChannel();
						if (channel) {
							if (channel->isBroadcast()) {
								chat["type"] = "channel";
							} else if (channel->isMegagroup()) {
								chat["type"] = "supergroup";
							} else {
								chat["type"] = "channel";
							}
						} else {
							chat["type"] = "channel";
						}
					} else {
						chat["type"] = "unknown";
					}

					chats.append(chat);
				}

				QJsonObject result;
				result["chats"] = chats;
				result["count"] = chats.size();
				result["source"] = "live_telegram_data";

				// Cache the result
				if (_cache) {
					_cache->put(_cache->chatListKey(), result, 60);  // Cache for 60 seconds
				}

				return result;
			}
		}
		qWarning() << "MCP: Failed to access live chat data, falling back to archive";
	}

	// Fallback to archived data
	if (_archiver) {
		chats = _archiver->listArchivedChats();
	}

	QJsonObject result;
	result["chats"] = chats;
	result["count"] = chats.size();
	result["source"] = _archiver ? "archived_data" : "no_data_available";

	// Cache the archived result too
	if (_cache) {
		_cache->put(_cache->chatListKey(), result, 300);  // Cache for 5 minutes
	}

	return result;
}

QJsonObject Server::toolGetChatInfo(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	QJsonObject chatInfo;

	// Try live data first if session is available
	if (_session) {
		// Convert chat_id to PeerId
		PeerId peerId(chatId);

		// Get the peer data
		auto peer = _session->data().peer(peerId);
		if (!peer) {
			qWarning() << "MCP: No peer found for chat" << chatId;
			chatInfo["error"] = "Chat not found";
			chatInfo["chat_id"] = QString::number(chatId);
			return chatInfo;
		}

		// Basic information
		chatInfo["id"] = QString::number(peer->id.value);
		chatInfo["name"] = peer->name();

		// Determine chat type
		if (peer->isUser()) {
			chatInfo["type"] = "user";
			auto user = peer->asUser();
			if (user && user->isBot()) {
				chatInfo["is_bot"] = true;
			}
		} else if (peer->isChat()) {
			chatInfo["type"] = "group";
			auto chat = peer->asChat();
			if (chat) {
				chatInfo["member_count"] = chat->count;
				chatInfo["is_creator"] = chat->amCreator();
			}
		} else if (peer->isChannel()) {
			auto channel = peer->asChannel();
			if (channel) {
				if (channel->isMegagroup()) {
					chatInfo["type"] = "supergroup";
				} else {
					chatInfo["type"] = "channel";
				}
				chatInfo["member_count"] = channel->membersCount();
				chatInfo["is_broadcast"] = channel->isBroadcast();
				chatInfo["is_megagroup"] = channel->isMegagroup();
				chatInfo["is_creator"] = channel->amCreator();
			}
		}

		// Optional fields
		if (!peer->username().isEmpty()) {
			chatInfo["username"] = peer->username();
		}

		// Status fields
		chatInfo["is_verified"] = peer->isVerified();
		chatInfo["is_scam"] = peer->isScam();
		chatInfo["is_fake"] = peer->isFake();

		// About/description
		if (!peer->about().isEmpty()) {
			chatInfo["about"] = peer->about();
		}

		// Get message count from history
		auto history = _session->data().history(peerId);
		if (history) {
			int messageCount = 0;
			for (const auto &block : history->blocks) {
				if (block) {
					messageCount += block->messages.size();
				}
			}
			chatInfo["loaded_message_count"] = messageCount;
		}

		chatInfo["source"] = "live_telegram_data";

		qInfo() << "MCP: Retrieved info for chat" << chatId;
		return chatInfo;
	}

	// Fallback to archived data
	if (!_archiver) {
		chatInfo["chat_id"] = QString::number(chatId);
		chatInfo["error"] = "Chat info not available (archiver not initialized)";
		chatInfo["source"] = "error";
		return chatInfo;
	}
	chatInfo = _archiver->getChatInfo(chatId);
	if (chatInfo.isEmpty() || !chatInfo.contains("id")) {
		chatInfo["chat_id"] = QString::number(chatId);
		chatInfo["error"] = "Chat info not available (session not active)";
		chatInfo["source"] = "error";
	} else {
		chatInfo["source"] = "archived_data";
	}

	return chatInfo;
}

QJsonObject Server::toolReadMessages(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(50);
	qint64 beforeTimestamp = args.value("before_timestamp").toVariant().toLongLong();

	QJsonArray messages;

	// Try live data first if session is available
	if (_session) {
		// Convert chat_id to PeerId
		PeerId peerId(chatId);

		// Get the history for this peer
		auto history = _session->data().history(peerId);
		if (!history) {
			qWarning() << "MCP: No history found for peer" << chatId;
		} else {
			// Iterate through blocks and messages (newest first)
			int collected = 0;
			for (auto blockIt = history->blocks.rbegin();
			     blockIt != history->blocks.rend() && collected < limit;
			     ++blockIt) {
				const auto &block = *blockIt;
				if (!block) continue;

				// Iterate through messages in this block (newest first)
				for (auto msgIt = block->messages.rbegin();
				     msgIt != block->messages.rend() && collected < limit;
				     ++msgIt) {
					const auto &element = *msgIt;
					if (!element) continue;
					auto item = element->data();
					if (!item) continue;

					// Skip if message is after beforeTimestamp filter
					if (beforeTimestamp > 0 && item->date() >= beforeTimestamp) {
						continue;
					}

					// Extract message data
					QJsonObject msg;
					msg["message_id"] = QString::number(item->id.bare);
					msg["date"] = static_cast<qint64>(item->date());

					// Get message text
					const auto &text = item->originalText();
					msg["text"] = text.text;

					// Get sender information
					auto from = item->from();
					if (from) {
						QJsonObject fromUser;
						fromUser["id"] = QString::number(from->id.value);
						fromUser["name"] = from->name();
						if (!from->username().isEmpty()) {
							fromUser["username"] = from->username();
						}
						msg["from_user"] = fromUser;
					}

					// Add optional fields
					if (item->out()) {
						msg["is_outgoing"] = true;
					}
					if (item->isPinned()) {
						msg["is_pinned"] = true;
					}

					// Add reply information if present
					if (item->replyToId()) {
						QJsonObject reply;
						reply["message_id"] = QString::number(item->replyToId().bare);
						msg["reply_to"] = reply;
					}

					messages.append(msg);
					collected++;
				}
			}

			// Return live data result
			QJsonObject result;
			result["messages"] = messages;
			result["count"] = messages.size();
			result["chat_id"] = chatId;
			result["source"] = "live_telegram_data";

			qInfo() << "MCP: Read" << messages.size() << "live messages from chat" << chatId;
			return result;
		}
	}

	// Fallback to archived data
	if (_archiver) {
		messages = _archiver->getMessages(chatId, limit, beforeTimestamp);
	}

	QJsonObject result;
	result["messages"] = messages;
	result["count"] = messages.size();
	result["chat_id"] = chatId;
	result["source"] = _archiver ? "archived_data" : "no_data_available";

	return result;
}

QJsonObject Server::toolSendMessage(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString text = args["text"].toString();

	QJsonObject result;

	// Check if session is available
	if (!_session) {
		result["success"] = false;
		result["error"] = "Session not available";
		result["chat_id"] = chatId;
		return result;
	}

	// Convert chat_id to PeerId
	PeerId peerId(chatId);

	// Get the history for this peer
	auto history = _session->data().history(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		result["chat_id"] = chatId;
		return result;
	}

	// Create SendAction (history is a Data::Thread)
	Api::SendAction action(history);

	// Create MessageToSend
	Api::MessageToSend message(action);
	message.textWithTags = TextWithTags{ text };

	// Send the message via API
	_session->api().sendMessage(std::move(message));

	// Return success
	result["success"] = true;
	result["chat_id"] = chatId;
	result["text"] = text;
	result["status"] = "Message queued for sending";

	qInfo() << "MCP: Queued message send to chat" << chatId;
	return result;
}

QJsonObject Server::toolSearchMessages(const QJsonObject &args) {
	QString query = args["query"].toString();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(50);

	QJsonArray results;

	// Try live search first if session is available
	if (_session && chatId != 0) {
		PeerId peerId(chatId);
		auto history = _session->data().history(peerId);

		if (history) {
			QString lowerQuery = query.toLower();
			int found = 0;

			// Search through loaded messages
			for (auto blockIt = history->blocks.rbegin();
			     blockIt != history->blocks.rend() && found < limit;
			     ++blockIt) {
				const auto &block = *blockIt;
				if (!block) continue;

				for (auto msgIt = block->messages.rbegin();
				     msgIt != block->messages.rend() && found < limit;
				     ++msgIt) {
					const auto &element = *msgIt;
					if (!element) continue;
					auto item = element->data();
					if (!item) continue;

					// Get message text and check if it contains query
					const auto &text = item->originalText();
					if (text.text.toLower().contains(lowerQuery)) {
						QJsonObject msg;
						msg["message_id"] = QString::number(item->id.bare);
						msg["date"] = static_cast<qint64>(item->date());
						msg["text"] = text.text;

						// Get sender info
						auto from = item->from();
						if (from) {
							QJsonObject fromUser;
							fromUser["id"] = QString::number(from->id.value);
							fromUser["name"] = from->name();
							if (!from->username().isEmpty()) {
								fromUser["username"] = from->username();
							}
							msg["from_user"] = fromUser;
						}

						msg["source"] = "live";
						results.append(msg);
						found++;
					}
				}
			}

			if (found > 0) {
				QJsonObject result;
				result["results"] = results;
				result["count"] = results.size();
				result["query"] = query;
				result["chat_id"] = chatId;
				result["source"] = "live_search";

				qInfo() << "MCP: Found" << found << "messages in live search for:" << query;
				return result;
			}
		}
	}

	// Fallback to archived data search (more comprehensive, uses FTS)
	if (_archiver) {
		results = _archiver->searchMessages(chatId, query, limit);
	}

	QJsonObject result;
	result["results"] = results;
	result["count"] = results.size();
	result["query"] = query;
	if (chatId != 0) {
		result["chat_id"] = chatId;
	}
	result["source"] = _archiver ? "archived_search" : "no_archive_available";

	return result;
}

QJsonObject Server::toolGetUserInfo(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();

	QJsonObject userInfo;

	// Try live data first if session is available
	if (_session) {
		// Convert user_id to UserId and then PeerId
		UserId uid(userId);
		PeerId peerId = peerFromUser(uid);

		// Get the peer data first
		auto peer = _session->data().peer(peerId);
		if (!peer) {
			qWarning() << "MCP: Peer not found for" << userId;
			userInfo["error"] = "User not found";
			userInfo["user_id"] = QString::number(userId);
			return userInfo;
		}

		// Get the user data
		auto user = peer->asUser();
		if (!user) {
			qWarning() << "MCP: Peer" << userId << "is not a user";
			userInfo["error"] = "Specified ID is not a user";
			userInfo["user_id"] = QString::number(userId);
			return userInfo;
		}

		// Extract user information
		userInfo["id"] = QString::number(user->id.value);
		userInfo["name"] = user->name();

		// Optional fields
		if (!user->username().isEmpty()) {
			userInfo["username"] = user->username();
		}
		if (!user->firstName.isEmpty()) {
			userInfo["first_name"] = user->firstName;
		}
		if (!user->lastName.isEmpty()) {
			userInfo["last_name"] = user->lastName;
		}
		if (!user->phone().isEmpty()) {
			userInfo["phone"] = user->phone();
		}

		// Boolean fields
		userInfo["is_bot"] = user->isBot();
		userInfo["is_self"] = user->isSelf();
		userInfo["is_contact"] = user->isContact();
		userInfo["is_premium"] = user->isPremium();
		userInfo["is_verified"] = user->isVerified();
		userInfo["is_scam"] = user->isScam();
		userInfo["is_fake"] = user->isFake();

		// User online status
		if (user->isServiceUser()) {
			userInfo["status"] = "service";
		} else if (user->isSelf()) {
			userInfo["status"] = "self";
		} else {
			// Last seen info requires privacy settings to allow visibility.
			// The raw timestamp is not directly exposed via the public API
			// without subscribing to status updates.
			userInfo["status"] = "unknown";
		}

		// About/bio if available
		if (!user->about().isEmpty()) {
			userInfo["about"] = user->about();
		}

		userInfo["source"] = "live_telegram_data";

		qInfo() << "MCP: Retrieved info for user" << userId;
		return userInfo;
	}

	// Fallback response if session not available
	userInfo["user_id"] = QString::number(userId);
	userInfo["error"] = "User info not available (session not active)";
	userInfo["source"] = "error";

	return userInfo;
}


} // namespace MCP
