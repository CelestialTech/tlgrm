// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== CORE TOOL IMPLEMENTATIONS =====

PeerData *Server::resolvePeer(qint64 chatId) const {
	return chatId ? resolvePeer(PeerId(chatId)) : nullptr;
}

PeerData *Server::resolvePeer(PeerId id) const {
	if (!_session || !id.value) {
		return nullptr;
	}
	// Session::peer() aborts on an id that names no peer kind, so the shape
	// has to be checked before the lookup, not after it.
	if (!peerIsUser(id) && !peerIsChat(id) && !peerIsChannel(id)) {
		return nullptr;
	}
	return _session->data().peerLoaded(id);
}

History *Server::resolveHistory(qint64 chatId) const {
	const auto peer = resolvePeer(chatId);
	return peer ? _session->data().history(peer).get() : nullptr;
}

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

// Creates a channel or supergroup and returns its chat_id.
//
// Exists mainly so tests have somewhere disposable to act: roughly half the
// tool surface mutates or destroys, and none of it could be exercised against
// a real account. Paired with delete_channel, a test run can build its own
// fixtures and remove them again.
QJsonObject Server::toolCreateChannel(const QJsonObject &args) {
	const auto title = args["title"].toString().trimmed();
	const auto about = args.value("about").toString();
	const auto megagroup = args.value("megagroup").toBool(false);

	if (!_session) {
		return toolError("No active session");
	}
	if (title.isEmpty()) {
		return toolError("title is required");
	}

	using Flag = MTPchannels_CreateChannel::Flag;
	const auto flags = megagroup ? Flag::f_megagroup : Flag::f_broadcast;
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPchannels_CreateChannel(
			MTP_flags(flags),
			MTP_string(title),
			MTP_string(about),
			MTPInputGeoPoint(),
			MTPstring(),
			MTP_int(0)
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			const auto chats = [&]() -> const QVector<MTPChat>* {
				switch (result.type()) {
				case mtpc_updates: return &result.c_updates().vchats().v;
				case mtpc_updatesCombined:
					return &result.c_updatesCombined().vchats().v;
				}
				return nullptr;
			}();
			QJsonObject value;
			if (!chats || chats->isEmpty()) {
				value = toolError("Channel created but Telegram returned no "
					"chat to identify it");
				done(value);
				return;
			}
			const auto peer = _session->data().processChats(
				MTP_vector<MTPChat>(*chats));
			value["success"] = true;
			value["chat_id"] = qint64(peer->id.value);
			value["title"] = title;
			value["megagroup"] = megagroup;
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("channels.createChannel failed: " + error.type());
		}).send();
	}, 30000);
}

// Deletes a channel or supergroup outright. Only its creator can, which is
// the guard that matters for a tool this destructive -- Telegram refuses
// otherwise rather than this having to check.
QJsonObject Server::toolDeleteChannel(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	if (!_session) {
		return toolError("No active session");
	}
	const auto peer = resolvePeer(chatId);
	const auto channel = peer ? peer->asChannel() : nullptr;
	if (!channel) {
		return toolError("chat_id must name a channel or supergroup this "
			"client has loaded");
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPchannels_DeleteChannel(
			channel->inputChannel()
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			QJsonObject value;
			value["success"] = true;
			value["chat_id"] = chatId;
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("channels.deleteChannel failed: " + error.type());
		}).send();
	}, 30000);
}

// Sets or clears a channel's public username, which is what makes it
// reachable as t.me/<name> and resolvable by ResolveChannel. create_channel
// alone leaves a channel private, and the update feed has to be public for
// the updater to find it without being a member.
QJsonObject Server::toolSetChannelUsername(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto username = args["username"].toString().trimmed();
	if (!_session) {
		return toolError("No active session");
	}
	const auto peer = resolvePeer(chatId);
	const auto channel = peer ? peer->asChannel() : nullptr;
	if (!channel) {
		return toolError("chat_id must name a channel or supergroup");
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPchannels_UpdateUsername(
			channel->inputChannel(),
			MTP_string(username)
		)).done([=](const MTPBool &result) {
			QJsonObject value;
			value["success"] = mtpIsTrue(result);
			value["chat_id"] = chatId;
			value["username"] = username;
			if (!username.isEmpty()) {
				value["link"] = "https://t.me/" + username;
			}
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("channels.updateUsername failed: " + error.type());
		}).send();
	}, 30000);
}

// Whether a username is free for this channel to take. Worth asking before
// updateUsername so the caller gets "taken" rather than a generic failure.
QJsonObject Server::toolCheckChannelUsername(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto username = args["username"].toString().trimmed();
	if (!_session) {
		return toolError("No active session");
	}
	const auto peer = resolvePeer(chatId);
	const auto channel = peer ? peer->asChannel() : nullptr;
	if (!channel) {
		return toolError("chat_id must name a channel or supergroup");
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPchannels_CheckUsername(
			channel->inputChannel(),
			MTP_string(username)
		)).done([=](const MTPBool &result) {
			QJsonObject value;
			value["success"] = true;
			value["username"] = username;
			value["available"] = mtpIsTrue(result);
			done(value);
		}).fail([=](const MTP::Error &error) {
			QJsonObject value;
			value["success"] = true;
			value["username"] = username;
			value["available"] = false;
			value["reason"] = error.type();
			done(value);
		}).send();
	});
}

QJsonObject Server::toolGetChatInfo(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	QJsonObject chatInfo;

	// Try live data first if session is available
	if (_session) {
		// Convert chat_id to PeerId
		PeerId peerId(chatId);

		// Get the peer data
		auto peer = resolvePeer(chatId);
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
	if (chatId == 0) {
		return toolError("chat_id is required and must be non-zero");
	}
	int limit = clampLimit(args.value("limit").toInt(50));
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

	if (chatId == 0) {
		return toolError("chat_id is required and must be non-zero");
	}
	if (text.isEmpty()) {
		return toolError("text is required and must not be empty");
	}

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

// Sends a local file to a chat as a document, preserving its bytes and its
// filename. Used by the release pipeline to publish signed update packages to
// the update feed channel: MTProto's update path resolves a package through
// documentAttributeFilename, which SendMediaType::File is the only send mode
// that guarantees (Photo re-encodes, and anything image-shaped would be
// recompressed). The caller does not need to know how the file is prepared,
// chunked or uploaded, nor that uploads are asynchronous — the return
// acknowledges queueing, not delivery.
QJsonObject Server::toolSendDocument(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto path = args["file_path"].toString();
	const auto caption = args.value("caption").toString();

	if (chatId == 0) {
		return toolError("chat_id is required and must be non-zero");
	}
	if (path.isEmpty()) {
		return toolError("file_path is required and must not be empty");
	}
	const auto info = QFileInfo(path);
	if (!info.exists() || !info.isFile()) {
		return toolError("file_path is not an existing file: " + path);
	}
	if (!_session) {
		return toolError("Session not available");
	}

	const auto history = _session->data().history(PeerId(chatId));
	if (!history) {
		return toolError("Chat not found");
	}

	const auto premium = _session->user()->isPremium();
	auto list = Storage::PrepareMediaList(
		QStringList(info.absoluteFilePath()),
		st::sendMediaPreviewSize,
		premium);
	if (list.error != Ui::PreparedList::Error::None) {
		// TooLargeFile is the one a caller can act on: the limit is 2 GB for
		// premium accounts and 4 GB otherwise, so report it distinctly.
		const auto reason = (list.error == Ui::PreparedList::Error::TooLargeFile)
			? QString("file exceeds this account's upload limit")
			: QString("could not prepare file (error %1)"
				).arg(int(list.error));
		return toolError(reason + ": " + list.errorData);
	}
	if (list.files.empty()) {
		return toolError("File prepared to an empty list: " + path);
	}
	if (!caption.isEmpty()) {
		list.files.back().caption.text = caption;
	}

	_session->api().sendFiles(
		std::move(list),
		SendMediaType::File,
		nullptr, // not an album
		Api::SendAction(history));

	QJsonObject result;
	result["success"] = true;
	result["chat_id"] = chatId;
	result["file_path"] = info.absoluteFilePath();
	result["file_name"] = info.fileName();
	result["size"] = qint64(info.size());
	if (!caption.isEmpty()) {
		result["caption"] = caption;
	}
	// Deliberately not "sent": sendFiles() queues an asynchronous upload, so
	// no message id exists yet. Callers that need the post id (the update
	// feed does) must read it back from the chat once the upload completes.
	result["status"] = "Document queued for upload";

	qInfo() << "MCP: Queued document" << info.fileName()
		<< "(" << info.size() << "bytes ) to chat" << chatId;
	return result;
}

QJsonObject Server::toolSearchMessages(const QJsonObject &args) {
	QString query = args["query"].toString();
	if (query.isEmpty()) {
		return toolError("query is required and must not be empty");
	}
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = clampLimit(args.value("limit").toInt(50));

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
		auto peer = resolvePeer(peerId);
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
