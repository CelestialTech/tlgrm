// MCP v6.5.1 Feature Tools implementation.
// This file is part of Telegram Desktop MCP integration.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"
#include "data/data_chat_filters.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "api/api_text_entities.h"

#include <QtCore/QEventLoop>

namespace MCP {

// ============================================================
// V6.5.1 FEATURE TOOLS IMPLEMENTATION (10 tools)
// ============================================================

// ---------- request_message_summary ----------
QJsonObject Server::toolRequestMessageSummary(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int messageId = args.value("message_id").toInt();

	if (chatId == 0 || messageId == 0) {
		QJsonObject error;
		error["error"] = "chat_id and message_id are required";
		return error;
	}

	QString language = args.value("language").toString();

	// Resolve the peer
	PeerId peerId(chatId);
	const auto peer = _session->data().peerLoaded(peerId);
	if (!peer) {
		QJsonObject error;
		error["error"] = QString("Chat %1 not found").arg(chatId);
		return error;
	}

	// Build MTP flags
	using Flag = MTPmessages_SummarizeText::Flag;
	const auto flags = language.isEmpty()
		? Flag(0)
		: Flag::f_to_lang;

	// Send async request with blocking wait
	QJsonObject result;
	bool done = false;
	QEventLoop loop;

	_session->api().request(MTPmessages_SummarizeText(
		MTP_flags(flags),
		peer->input(),
		MTP_int(messageId),
		language.isEmpty() ? MTPstring() : MTP_string(language)
	)).done([&](const MTPTextWithEntities &response) {
		const auto &data = response.data();
		result["success"] = true;
		result["summary"] = qs(data.vtext());
		result["chat_id"] = chatId;
		result["message_id"] = messageId;
		if (!language.isEmpty()) {
			result["language"] = language;
		}
		done = true;
		loop.quit();
	}).fail([&](const MTP::Error &error) {
		result["success"] = false;
		result["error"] = error.type();
		if (error.type() == u"SUMMARY_FLOOD_PREMIUM"_q) {
			result["premium_required"] = true;
			result["error_description"] = "Rate limit reached. Premium subscription required for more summaries.";
		}
		done = true;
		loop.quit();
	}).send();

	// Wait with timeout
	QTimer::singleShot(15000, &loop, &QEventLoop::quit);
	if (!done) {
		loop.exec();
	}

	if (!done) {
		result["success"] = false;
		result["error"] = "Request timed out after 15 seconds";
	}

	return result;
}

// ---------- list_folders ----------
QJsonObject Server::toolListFolders(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	const auto &filters = _session->data().chatsFilters().list();
	QJsonArray folders;
	for (const auto &filter : filters) {
		if (filter.id() == 0) continue;
		QJsonObject f;
		f["id"] = filter.id();
		f["title"] = filter.title().text.text;
		f["emoji"] = filter.iconEmoji();
		f["always_count"] = (int)filter.always().size();
		f["never_count"] = (int)filter.never().size();
		f["pinned_count"] = (int)filter.pinned().size();

		QJsonArray flags;
		using Flag = Data::ChatFilter::Flag;
		if (filter.flags() & Flag::Contacts) flags.append("contacts");
		if (filter.flags() & Flag::NonContacts) flags.append("non_contacts");
		if (filter.flags() & Flag::Groups) flags.append("groups");
		if (filter.flags() & Flag::Channels) flags.append("channels");
		if (filter.flags() & Flag::Bots) flags.append("bots");
		if (filter.flags() & Flag::NoMuted) flags.append("exclude_muted");
		if (filter.flags() & Flag::NoRead) flags.append("exclude_read");
		if (filter.flags() & Flag::NoArchived) flags.append("exclude_archived");
		f["flags"] = flags;

		folders.append(f);
	}

	QJsonObject result;
	result["success"] = true;
	result["folders"] = folders;
	result["count"] = folders.size();
	return result;
}

// ---------- create_folder ----------
QJsonObject Server::toolCreateFolder(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	QString title = args.value("title").toString();
	if (title.isEmpty()) {
		QJsonObject error;
		error["error"] = "title is required";
		return error;
	}

	// Generate a new filter ID (max existing + 2, skipping 0 and 1)
	const auto &filters = _session->data().chatsFilters().list();
	FilterId newId = 2;
	for (const auto &filter : filters) {
		if (filter.id() >= newId) {
			newId = filter.id() + 1;
		}
	}

	// Build flags from parameters
	using Flag = Data::ChatFilter::Flag;
	using Flags = Data::ChatFilter::Flags;
	auto filterFlags = Flags(0);
	if (args.value("include_contacts").toBool()) filterFlags |= Flag::Contacts;
	if (args.value("include_non_contacts").toBool()) filterFlags |= Flag::NonContacts;
	if (args.value("include_groups").toBool()) filterFlags |= Flag::Groups;
	if (args.value("include_channels").toBool()) filterFlags |= Flag::Channels;
	if (args.value("include_bots").toBool()) filterFlags |= Flag::Bots;
	if (args.value("exclude_muted").toBool()) filterFlags |= Flag::NoMuted;
	if (args.value("exclude_read").toBool()) filterFlags |= Flag::NoRead;
	if (args.value("exclude_archived").toBool()) filterFlags |= Flag::NoArchived;

	QString iconEmoji = args.value("icon_emoji").toString();

	// Resolve include/exclude chat IDs to History objects
	base::flat_set<not_null<History*>> always;
	base::flat_set<not_null<History*>> never;
	std::vector<not_null<History*>> pinned;

	if (args.contains("include_chat_ids")) {
		const auto ids = args.value("include_chat_ids").toArray();
		for (const auto &v : ids) {
			PeerId pid(v.toVariant().toLongLong());
			if (const auto history = _session->data().historyLoaded(pid)) {
				always.emplace(history);
			}
		}
	}
	if (args.contains("exclude_chat_ids")) {
		const auto ids = args.value("exclude_chat_ids").toArray();
		for (const auto &v : ids) {
			PeerId pid(v.toVariant().toLongLong());
			if (const auto history = _session->data().historyLoaded(pid)) {
				never.emplace(history);
			}
		}
	}

	// Create the ChatFilter
	Data::ChatFilterTitle filterTitle;
	filterTitle.text = TextWithEntities{ title };

	auto filter = Data::ChatFilter(
		newId,
		filterTitle,
		iconEmoji,
		std::nullopt, // colorIndex
		filterFlags,
		std::move(always),
		std::move(pinned),
		std::move(never));

	// Send to server
	_session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(newId),
		filter.tl()
	)).done([session = _session](const MTPBool &) {
		session->data().chatsFilters().reload();
	}).send();

	// Also update local state
	_session->data().chatsFilters().set(std::move(filter));

	QJsonObject result;
	result["success"] = true;
	result["folder_id"] = newId;
	result["title"] = title;
	result["status"] = "Folder created";
	return result;
}

// ---------- update_folder ----------
QJsonObject Server::toolUpdateFolder(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	int folderId = args.value("folder_id").toInt();
	if (folderId == 0) {
		QJsonObject error;
		error["error"] = "folder_id is required";
		return error;
	}

	// Find existing filter
	const auto &filters = _session->data().chatsFilters().list();
	const Data::ChatFilter *existing = nullptr;
	for (const auto &filter : filters) {
		if (filter.id() == folderId) {
			existing = &filter;
			break;
		}
	}

	if (!existing) {
		QJsonObject error;
		error["error"] = QString("Folder with id %1 not found").arg(folderId);
		return error;
	}

	// Build updated filter - start from existing
	auto updated = *existing;

	if (args.contains("title")) {
		Data::ChatFilterTitle newTitle;
		newTitle.text = TextWithEntities{ args.value("title").toString() };
		updated = updated.withTitle(newTitle);
	}

	// Build new flags if any flag params provided
	using Flag = Data::ChatFilter::Flag;
	auto newFlags = existing->flags();
	bool flagsChanged = false;

	const auto applyFlag = [&](const char *key, Flag flag) {
		if (args.contains(key)) {
			flagsChanged = true;
			if (args.value(key).toBool()) {
				newFlags |= flag;
			} else {
				newFlags &= ~flag;
			}
		}
	};
	applyFlag("include_contacts", Flag::Contacts);
	applyFlag("include_non_contacts", Flag::NonContacts);
	applyFlag("include_groups", Flag::Groups);
	applyFlag("include_channels", Flag::Channels);
	applyFlag("include_bots", Flag::Bots);
	applyFlag("exclude_muted", Flag::NoMuted);
	applyFlag("exclude_read", Flag::NoRead);
	applyFlag("exclude_archived", Flag::NoArchived);

	// Reconstruct filter if flags changed
	if (flagsChanged || args.contains("icon_emoji")) {
		QString iconEmoji = args.contains("icon_emoji")
			? args.value("icon_emoji").toString()
			: existing->iconEmoji();

		updated = Data::ChatFilter(
			folderId,
			args.contains("title")
				? Data::ChatFilterTitle{ TextWithEntities{ args.value("title").toString() } }
				: existing->title(),
			iconEmoji,
			existing->colorIndex(),
			flagsChanged ? newFlags : existing->flags(),
			existing->always(),
			existing->pinned(),
			existing->never());
	}

	// Send to server
	_session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(folderId),
		updated.tl()
	)).send();

	// Update local state
	_session->data().chatsFilters().set(std::move(updated));

	QJsonObject result;
	result["success"] = true;
	result["folder_id"] = folderId;
	result["status"] = "Folder updated";
	return result;
}

// ---------- delete_folder ----------
QJsonObject Server::toolDeleteFolder(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	int folderId = args.value("folder_id").toInt();
	if (folderId == 0) {
		QJsonObject error;
		error["error"] = "folder_id is required";
		return error;
	}

	// Verify folder exists
	const auto &filters = _session->data().chatsFilters().list();
	bool found = false;
	QString folderTitle;
	for (const auto &filter : filters) {
		if (filter.id() == folderId) {
			found = true;
			folderTitle = filter.title().text.text;
			break;
		}
	}

	if (!found) {
		QJsonObject error;
		error["error"] = QString("Folder with id %1 not found").arg(folderId);
		return error;
	}

	// Remove locally and from server
	_session->data().chatsFilters().remove(folderId);

	QJsonObject result;
	result["success"] = true;
	result["folder_id"] = folderId;
	result["deleted_title"] = folderTitle;
	result["status"] = "Folder deleted";
	return result;
}

// ---------- reorder_folders ----------
QJsonObject Server::toolReorderFolders(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	QJsonArray folderIdsJson = args.value("folder_ids").toArray();
	if (folderIdsJson.isEmpty()) {
		QJsonObject error;
		error["error"] = "folder_ids array is required and must not be empty";
		return error;
	}

	// Parse and validate IDs
	std::vector<FilterId> order;
	order.reserve(folderIdsJson.size());
	for (const auto &v : folderIdsJson) {
		order.push_back(v.toInt());
	}

	// Verify all IDs exist
	const auto &filters = _session->data().chatsFilters().list();
	for (const auto id : order) {
		bool found = false;
		for (const auto &filter : filters) {
			if (filter.id() == id) {
				found = true;
				break;
			}
		}
		if (!found) {
			QJsonObject error;
			error["error"] = QString("Folder with id %1 not found").arg(id);
			return error;
		}
	}

	// Apply the new order
	_session->data().chatsFilters().saveOrder(order);

	QJsonObject result;
	result["success"] = true;
	result["status"] = "Folders reordered";

	// Return new order
	QJsonArray newOrder;
	for (const auto id : order) {
		newOrder.append(id);
	}
	result["new_order"] = newOrder;
	return result;
}

// ---------- transfer_group_ownership ----------
QJsonObject Server::toolTransferGroupOwnership(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	qint64 newOwnerId = args.value("new_owner_id").toVariant().toLongLong();

	if (chatId == 0 || newOwnerId == 0) {
		QJsonObject error;
		error["error"] = "chat_id and new_owner_id are required";
		return error;
	}

	QJsonObject result;
	result["error"] = "Not implemented - requires 2FA password input which cannot be provided via MCP";
	QJsonObject info;
	info["api_call"] = "MTPchannels_EditCreator";
	info["description"] = "Transfers ownership of a group or channel to another user. "
		"This requires Two-Factor Authentication (2FA) password confirmation "
		"which must be provided interactively through the GUI.";
	info["parameters_received"] = QJsonObject{
		{"chat_id", chatId},
		{"new_owner_id", newOwnerId}
	};
	info["security_warning"] = "Ownership transfer is IRREVERSIBLE. "
		"Please use Settings > Chat Info > Transfer Ownership in the GUI.";
	result["info"] = info;
	return result;
}

// ---------- craft_star_gift ----------
QJsonObject Server::toolCraftStarGift(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	QJsonArray giftIdsJson = args.value("gift_ids").toArray();
	if (giftIdsJson.isEmpty()) {
		QJsonObject error;
		error["error"] = "gift_ids array is required and must not be empty";
		return error;
	}

	// Build InputSavedStarGift vector from message IDs
	auto inputs = QVector<MTPInputSavedStarGift>();
	QJsonArray parsedIds;
	for (const auto &v : giftIdsJson) {
		int msgId = v.toInt();
		if (msgId > 0) {
			inputs.push_back(MTP_inputSavedStarGiftUser(MTP_int(msgId)));
			parsedIds.append(msgId);
		} else if (v.isString()) {
			// Slug-based identifier
			inputs.push_back(MTP_inputSavedStarGiftSlug(MTP_string(v.toString())));
			parsedIds.append(v.toString());
		}
	}

	if (inputs.isEmpty()) {
		QJsonObject error;
		error["error"] = "No valid gift IDs provided. Use message IDs (integers) or gift slugs (strings).";
		return error;
	}

	// Send craft request with blocking wait
	QJsonObject result;
	bool done = false;
	QEventLoop loop;

	_session->api().request(MTPpayments_CraftStarGift(
		MTP_vector<MTPInputSavedStarGift>(inputs)
	)).done([&](const MTPUpdates &updates) {
		_session->api().applyUpdates(updates);
		result["success"] = true;
		result["status"] = "Gift crafted successfully";
		result["gift_ids_used"] = parsedIds;
		done = true;
		loop.quit();
	}).fail([&](const MTP::Error &error) {
		const auto type = error.type();
		result["success"] = false;
		result["error"] = type;
		const auto waitPrefix = u"STARGIFT_CRAFT_TOO_EARLY_"_q;
		if (type.startsWith(waitPrefix)) {
			int seconds = type.mid(waitPrefix.size()).toInt();
			result["wait_seconds"] = seconds;
			result["error_description"] = QString("Crafting cooldown: wait %1 seconds").arg(seconds);
		}
		done = true;
		loop.quit();
	}).send();

	QTimer::singleShot(15000, &loop, &QEventLoop::quit);
	if (!done) {
		loop.exec();
	}

	if (!done) {
		result["success"] = false;
		result["error"] = "Request timed out after 15 seconds";
	}

	return result;
}

// ---------- get_craft_options ----------
QJsonObject Server::toolGetCraftOptions(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	// Fetch user's saved gifts that are eligible for crafting
	QJsonObject result;
	bool done = false;
	QEventLoop loop;

	const auto user = _session->user();
	_session->api().request(MTPpayments_GetSavedStarGifts(
		MTP_flags(0),
		user->input(),
		MTP_int(0), // collection_id
		MTP_string(), // offset
		MTP_int(50) // limit
	)).done([&](const MTPpayments_SavedStarGifts &response) {
		const auto &data = response.data();
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());

		QJsonArray gifts;
		for (const auto &gift : data.vgifts().v) {
			gift.match([&](const MTPDsavedStarGift &d) {
				QJsonObject g;
				g["date"] = d.vdate().v;
				g["unsaved"] = d.is_unsaved();
				g["pinned"] = d.is_pinned_to_top();
				g["upgradable"] = d.is_can_upgrade();
				g["convert_stars"] = (qint64)d.vconvert_stars().value_or_empty();
				g["upgrade_stars"] = (qint64)d.vupgrade_stars().value_or_empty();
				g["message_id"] = d.vmsg_id().value_or_empty();
				g["saved_id"] = (qint64)d.vsaved_id().value_or_empty();

				// Gift info
				d.vgift().match([&](const MTPDstarGift &sg) {
					g["gift_id"] = (qint64)sg.vid().v;
					g["stars"] = (qint64)sg.vstars().v;
					if (const auto limited = sg.vavailability_total()) {
						g["limited_total"] = limited->v;
					}
				}, [&](const MTPDstarGiftUnique &ug) {
					g["unique"] = true;
					g["unique_id"] = (qint64)ug.vid().v;
					g["title"] = qs(ug.vtitle());
					g["slug"] = qs(ug.vslug());
					g["number"] = ug.vnum().v;
					g["craft_chance_permille"] = ug.vcraft_chance_permille().value_or_empty();
				});

				gifts.append(g);
			});
		}

		result["success"] = true;
		result["gifts"] = gifts;
		result["total_count"] = data.vcount().v;
		if (const auto next = data.vnext_offset()) {
			result["next_offset"] = qs(*next);
		}
		done = true;
		loop.quit();
	}).fail([&](const MTP::Error &error) {
		result["success"] = false;
		result["error"] = error.type();
		done = true;
		loop.quit();
	}).send();

	QTimer::singleShot(15000, &loop, &QEventLoop::quit);
	if (!done) {
		loop.exec();
	}

	if (!done) {
		result["success"] = false;
		result["error"] = "Request timed out after 15 seconds";
	}

	return result;
}

// ---------- export_topic ----------
QJsonObject Server::toolExportTopic(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int topicId = args.value("topic_id").toInt();

	if (chatId == 0 || topicId == 0) {
		QJsonObject error;
		error["error"] = "chat_id and topic_id are required";
		return error;
	}

	int limit = args.value("limit").toInt(100);
	if (limit > 500) limit = 500;
	QString format = args.value("format").toString("json");

	if (format != "json" && format != "text") {
		QJsonObject error;
		error["error"] = "format must be 'json' or 'text'";
		return error;
	}

	// Resolve the peer
	PeerId peerId(chatId);
	const auto peer = _session->data().peerLoaded(peerId);
	if (!peer) {
		QJsonObject error;
		error["error"] = QString("Chat %1 not found").arg(chatId);
		return error;
	}

	const auto channel = peer->asChannel();
	if (!channel) {
		QJsonObject error;
		error["error"] = "Chat is not a channel/supergroup";
		return error;
	}

	if (!channel->isForum()) {
		QJsonObject error;
		error["error"] = "Chat does not have forum topics enabled";
		return error;
	}

	// Fetch topic messages via MTPmessages_GetReplies
	QJsonObject result;
	bool done = false;
	QEventLoop loop;

	_session->api().request(MTPmessages_GetReplies(
		peer->input(),
		MTP_int(topicId),
		MTP_int(0), // offset_id
		MTP_int(0), // offset_date
		MTP_int(0), // add_offset
		MTP_int(limit),
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done([&](const MTPmessages_Messages &response) {
		response.match([&](const MTPDmessages_messages &d) {
			_session->data().processUsers(d.vusers());
			_session->data().processChats(d.vchats());
			processTopicMessages(d.vmessages().v, format, result);
		}, [&](const MTPDmessages_messagesSlice &d) {
			_session->data().processUsers(d.vusers());
			_session->data().processChats(d.vchats());
			result["total_count"] = d.vcount().v;
			processTopicMessages(d.vmessages().v, format, result);
		}, [&](const MTPDmessages_channelMessages &d) {
			_session->data().processUsers(d.vusers());
			_session->data().processChats(d.vchats());
			result["total_count"] = d.vcount().v;
			processTopicMessages(d.vmessages().v, format, result);
		}, [&](const MTPDmessages_messagesNotModified &d) {
			result["total_count"] = d.vcount().v;
			result["not_modified"] = true;
		});

		result["success"] = true;
		result["chat_id"] = chatId;
		result["topic_id"] = topicId;
		result["format"] = format;
		done = true;
		loop.quit();
	}).fail([&](const MTP::Error &error) {
		result["success"] = false;
		result["error"] = error.type();
		done = true;
		loop.quit();
	}).send();

	QTimer::singleShot(15000, &loop, &QEventLoop::quit);
	if (!done) {
		loop.exec();
	}

	if (!done) {
		result["success"] = false;
		result["error"] = "Request timed out after 15 seconds";
	}

	return result;
}

// Helper for export_topic: process message list into result
void Server::processTopicMessages(
		const QVector<MTPMessage> &messages,
		const QString &format,
		QJsonObject &result) {
	if (format == "json") {
		QJsonArray msgs;
		for (const auto &msg : messages) {
			msg.match([&](const MTPDmessage &d) {
				QJsonObject m;
				m["id"] = d.vid().v;
				m["date"] = d.vdate().v;
				m["text"] = qs(d.vmessage());
				if (const auto fromId = d.vfrom_id()) {
					fromId->match([&](const MTPDpeerUser &u) {
						m["from_user_id"] = (qint64)u.vuser_id().v;
					}, [&](const MTPDpeerChat &c) {
						m["from_chat_id"] = (qint64)c.vchat_id().v;
					}, [&](const MTPDpeerChannel &c) {
						m["from_channel_id"] = (qint64)c.vchannel_id().v;
					});
				}
				if (const auto replyTo = d.vreply_to()) {
					replyTo->match([&](const MTPDmessageReplyHeader &r) {
						if (const auto replyId = r.vreply_to_msg_id()) {
							m["reply_to_message_id"] = replyId->v;
						}
					}, [&](const MTPDmessageReplyStoryHeader &) {
						// story reply, skip
					});
				}
				m["out"] = d.is_out();
				msgs.append(m);
			}, [&](const MTPDmessageService &d) {
				QJsonObject m;
				m["id"] = d.vid().v;
				m["date"] = d.vdate().v;
				m["service"] = true;
				msgs.append(m);
			}, [&](const MTPDmessageEmpty &d) {
				// skip empty messages
			});
		}
		result["messages"] = msgs;
		result["message_count"] = msgs.size();
	} else {
		// text format
		QStringList lines;
		for (const auto &msg : messages) {
			msg.match([&](const MTPDmessage &d) {
				const auto date = QDateTime::fromSecsSinceEpoch(d.vdate().v);
				const auto dateStr = date.toString("yyyy-MM-dd HH:mm:ss");
				QString fromStr = "Unknown";
				if (const auto fromId = d.vfrom_id()) {
					fromId->match([&](const MTPDpeerUser &u) {
						if (const auto user = _session->data().userLoaded(UserId(u.vuser_id().v))) {
							fromStr = user->name();
						} else {
							fromStr = QString("User#%1").arg(u.vuser_id().v);
						}
					}, [&](const auto &) {});
				}
				const auto text = qs(d.vmessage());
				lines.append(QString("[%1] %2: %3").arg(dateStr, fromStr, text));
			}, [&](const MTPDmessageService &d) {
				const auto date = QDateTime::fromSecsSinceEpoch(d.vdate().v);
				lines.append(QString("[%1] [service message]").arg(
					date.toString("yyyy-MM-dd HH:mm:ss")));
			}, [&](const MTPDmessageEmpty &) {});
		}
		result["text"] = lines.join("\n");
		result["message_count"] = lines.size();
	}
}

} // namespace MCP
