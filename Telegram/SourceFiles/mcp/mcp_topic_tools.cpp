// Forum topic tools for the Telegram Desktop MCP integration.
//
// A forum splits one supergroup into topics, each with its own unread state.
// list_chats reports the group as a single row with one unread count, which
// hides where the unread messages actually are; these tools report the
// per-topic view the forum UI shows.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

#include "data/data_forum.h"
#include "data/data_forum_topic.h"

namespace MCP {

QJsonObject Server::toolListTopics(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(chatId);
	if (!peer) {
		return toolError(QString(
			"No chat %1 in this client's data").arg(chatId));
	}
	const auto channel = peer->asChannel();
	const auto forum = channel ? channel->forum() : nullptr;
	if (!forum) {
		return toolError(QString(
			"Chat %1 ('%2') is not a forum, so it has no topics"
			).arg(chatId).arg(peer->name()));
	}
	const auto unreadOnly = args.value("unread_only").toBool(false);
	const auto limit = clampLimit(args.value("limit").toInt(100), 100, 500);

	auto items = QJsonArray();
	auto total = 0;
	auto unreadTotal = 0;
	forum->enumerateTopics([&](not_null<Data::ForumTopic*> topic) {
		++total;
		const auto state = topic->chatListUnreadState();
		const auto unread = state.messages + state.marks;
		if (unread > 0) {
			++unreadTotal;
		}
		if ((unreadOnly && !unread) || items.size() >= limit) {
			return;
		}
		auto entry = QJsonObject();
		entry["topic_id"] = qint64(topic->rootId().bare);
		entry["title"] = topic->title();
		entry["general"] = topic->isGeneral();
		entry["closed"] = topic->closed();
		entry["pinned"] = topic->isPinnedDialog(FilterId());
		entry["unread_count"] = state.messages;
		entry["unread_marks"] = state.marks;
		entry["unread_mentions"] = state.mentions;
		entry["unread_reactions"] = state.reactions;
		entry["muted"] = topic->muted();
		// Without this a zero can mean either "nothing unread" or "this
		// client has not been told yet", and the caller cannot tell which.
		entry["unread_known"] = state.known;
		items.append(entry);
	});

	auto value = QJsonObject();
	value["success"] = true;
	value["chat_id"] = chatId;
	value["count"] = items.size();
	value["topics_total"] = total;
	value["topics_with_unread"] = unreadTotal;
	value["topics"] = items;
	return value;
}

} // namespace MCP
