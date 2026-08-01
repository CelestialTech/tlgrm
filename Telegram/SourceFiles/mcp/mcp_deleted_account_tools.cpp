// This file is part of Telegram Desktop MCP integration.
// Deleted account archiving tools.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {

QJsonObject Server::toolListDeletedAccounts(const QJsonObject &args) {
	if (!_gradualArchiver) {
		_gradualArchiver = std::make_unique<GradualArchiver>(this);
		if (_session) {
			_gradualArchiver->setMainSession(_session);
			_gradualArchiver->setDataSession(&_session->data());
		}
		if (_archiver) {
			_gradualArchiver->setArchiver(_archiver.get());
		}
	}

	auto deleted = _gradualArchiver->scanDeletedAccounts();

	QJsonArray chatsArray;
	for (const auto &chat : deleted) {
		QJsonObject obj;
		obj["peer_id"] = QString::number(chat.peerId);
		obj["name"] = chat.name;
		obj["cached_message_count"] = chat.messageCount;
		if (chat.firstMessageDate > 0) {
			obj["first_message_date"] = QDateTime::fromSecsSinceEpoch(
				chat.firstMessageDate).toString(Qt::ISODate);
		}
		if (chat.lastMessageDate > 0) {
			obj["last_message_date"] = QDateTime::fromSecsSinceEpoch(
				chat.lastMessageDate).toString(Qt::ISODate);
		}

		// Add last 5 message previews for identification
		QJsonArray messagesArray;
		if (_session) {
			PeerId peerId(chat.peerId);
			if (auto history = _session->data().historyLoaded(peerId)) {
				int count = 0;
				for (auto blockIt = history->blocks.rbegin();
					 blockIt != history->blocks.rend() && count < 5;
					 ++blockIt) {
					const auto &block = *blockIt;
					if (!block) continue;
					for (auto msgIt = block->messages.rbegin();
						 msgIt != block->messages.rend() && count < 5;
						 ++msgIt) {
						const auto &elem = *msgIt;
						auto item = elem->data();
						if (!item || item->isService()) continue;

						QJsonObject msgObj;
						msgObj["date"] = QDateTime::fromSecsSinceEpoch(
							item->date()).toString(Qt::ISODate);

						QString text = item->originalText().text;
						if (text.length() > 200) {
							text = text.left(200) + "...";
						}
						msgObj["text"] = text;

						if (const auto from = item->from()) {
							msgObj["from"] = from->name();
						}

						messagesArray.append(msgObj);
						count++;
					}
				}
			}
		}
		obj["last_messages"] = messagesArray;

		chatsArray.append(obj);
	}

	QJsonObject result;
	result["success"] = true;
	result["count"] = chatsArray.size();
	result["deleted_accounts"] = chatsArray;
	return result;
}

QJsonObject Server::toolArchiveDeletedAccounts(const QJsonObject &args) {
	if (!_gradualArchiver) {
		_gradualArchiver = std::make_unique<GradualArchiver>(this);
		if (_session) {
			_gradualArchiver->setMainSession(_session);
			_gradualArchiver->setDataSession(&_session->data());
		}
		if (_archiver) {
			_gradualArchiver->setArchiver(_archiver.get());
		}
	}

	GradualArchiveConfig config;
	config.forwardMode = true;
	config.respectActiveHours = false; // Don't pause for active hours in archive mode

	if (args.contains("group_title")) {
		config.groupTitle = args.value("group_title").toString();
	}
	if (args.contains("target_group_id")) {
		config.forwardTargetGroupId = args.value("target_group_id").toVariant().toLongLong();
	}
	if (args.contains("min_delay_ms")) {
		config.minDelayMs = args.value("min_delay_ms").toInt();
	}
	if (args.contains("max_delay_ms")) {
		config.maxDelayMs = args.value("max_delay_ms").toInt();
	}
	if (args.contains("date_format")) {
		config.dateHeaderFormat = args.value("date_format").toString();
	}
	if (args.contains("add_date_headers")) {
		config.addDateHeaders = args.value("add_date_headers").toBool();
	}
	if (args.contains("add_chat_separators")) {
		config.addChatSeparators = args.value("add_chat_separators").toBool();
	}
	if (args.contains("peer_id")) {
		// Single peer ID
		qint64 id = args.value("peer_id").toVariant().toLongLong();
		if (id > 0) {
			config.specificPeerIds.append(id);
		}
	}
	if (args.contains("peer_ids")) {
		// Array of peer IDs
		QJsonArray ids = args.value("peer_ids").toArray();
		for (const auto &v : ids) {
			qint64 id = v.toVariant().toLongLong();
			if (id > 0) {
				config.specificPeerIds.append(id);
			}
		}
	}

	bool started = _gradualArchiver->startDeletedAccountArchive(config);

	QJsonObject result;
	result["success"] = started;
	if (started) {
		result["message"] = "Deleted account archiving started with human simulation";
	} else {
		result["error"] = "Failed to start - another operation may be in progress or no deleted accounts found";
	}
	return result;
}

QJsonObject Server::toolGetDeletedArchiveStatus(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		QJsonObject result;
		result["success"] = true;
		result["state"] = "idle";
		result["message"] = "No deleted account archive in progress";
		return result;
	}

	return _gradualArchiver->statusJson();
}

QJsonObject Server::toolPauseDeletedArchive(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "No archive in progress";
		return result;
	}

	_gradualArchiver->pause();

	QJsonObject result;
	result["success"] = true;
	result["message"] = "Deleted account archive paused";
	result["status"] = _gradualArchiver->statusJson();
	return result;
}

QJsonObject Server::toolResumeDeletedArchive(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "No archive to resume";
		return result;
	}

	// Disable active hours restriction so resume always works
	auto config = _gradualArchiver->config();
	config.respectActiveHours = false;
	_gradualArchiver->setConfig(config);

	_gradualArchiver->resume();

	QJsonObject result;
	result["success"] = true;
	result["message"] = "Deleted account archive resumed";
	result["status"] = _gradualArchiver->statusJson();
	return result;
}

QJsonObject Server::toolCancelDeletedArchive(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "No archive to cancel";
		return result;
	}

	_gradualArchiver->cancel();

	QJsonObject result;
	result["success"] = true;
	result["message"] = "Deleted account archive cancelled";
	return result;
}

QJsonObject Server::toolListDeletedChannels(const QJsonObject &args) {
	if (!_session) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "No active session";
		return result;
	}

	QJsonArray channelsArray;

	auto scanList = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &row : *list->indexed()) {
			if (!row || !row->thread()) continue;
			auto peer = row->thread()->peer();
			if (!peer) continue;

			bool isDeleted = false;
			QString type;

			if (peer->isChannel()) {
				auto channel = peer->asChannel();
				if (channel->isForbidden()) {
					isDeleted = true;
					type = channel->isMegagroup() ? "supergroup" : "channel";
				}
			} else if (peer->isChat()) {
				auto chat = peer->asChat();
				if (chat->isForbidden() || chat->isDeactivated()) {
					isDeleted = true;
					type = "group";
				}
			}

			if (!isDeleted) continue;

			QJsonObject obj;
			obj["peer_id"] = QString::number(peer->id.value);
			obj["name"] = peer->name();
			obj["type"] = type;

			// Get last 5 messages as summary
			QJsonArray messagesArray;
			if (auto history = _session->data().historyLoaded(peer->id)) {
				int count = 0;
				// Iterate blocks in reverse to get newest messages first
				for (auto blockIt = history->blocks.rbegin();
					 blockIt != history->blocks.rend() && count < 5;
					 ++blockIt) {
					const auto &block = *blockIt;
					if (!block) continue;
					for (auto msgIt = block->messages.rbegin();
						 msgIt != block->messages.rend() && count < 5;
						 ++msgIt) {
						const auto &elem = *msgIt;
						auto item = elem->data();
						if (!item || item->isService()) continue;

						QJsonObject msgObj;
						msgObj["id"] = QString::number(item->id.bare);
						msgObj["date"] = QDateTime::fromSecsSinceEpoch(
							item->date()).toString(Qt::ISODate);

						QString text = item->originalText().text;
						if (text.length() > 200) {
							text = text.left(200) + "...";
						}
						msgObj["text"] = text;

						if (const auto from = item->from()) {
							msgObj["from"] = from->name();
						}

						if (item->media()) {
							if (item->media()->document()) {
								msgObj["has_media"] = "document";
							} else if (item->media()->photo()) {
								msgObj["has_media"] = "photo";
							}
						}

						messagesArray.append(msgObj);
						count++;
					}
				}
			}

			obj["last_messages"] = messagesArray;
			obj["message_preview_count"] = messagesArray.size();
			channelsArray.append(obj);
		}
	};

	scanList(_session->data().chatsList());
	if (auto folder = _session->data().folderLoaded(1)) {
		scanList(folder->chatsList());
	}

	QJsonObject result;
	result["success"] = true;
	result["count"] = channelsArray.size();
	result["deleted_channels"] = channelsArray;
	return result;
}

} // namespace MCP
