// Community tools for the Telegram Desktop MCP integration.
//
// A community is a channel that groups other chats under one dialogs row.
// Upstream exposes it only through the chats list and the edit-peer-info box;
// these tools expose the same operations -- the ones Api::Communities backs --
// so a client can inspect and change a community without driving the UI.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

#include "api/api_communities.h"
#include "data/data_community.h"

namespace MCP {
namespace {

[[nodiscard]] QJsonObject CommunityBrief(not_null<ChannelData*> channel) {
	auto value = QJsonObject();
	value["chat_id"] = qint64(channel->id.value);
	value["title"] = channel->name();
	value["collapsed"] = channel->collapsedInDialogs();
	value["anyone_can_add_peers"] = channel->communityAnyoneCanAddPeers();
	if (const auto info = channel->communityInfo()) {
		value["linked_chats"] = int(info->linkedPeers().size());
	}
	return value;
}

} // namespace

// Resolves chat_id to a community channel, or returns the error to send back.
// Returned by value rather than through an out-parameter so a caller cannot
// use the channel without having looked at the error first.
Server::CommunityLookup Server::resolveCommunity(const QJsonObject &args) {
	auto result = CommunityLookup();
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(chatId);
	if (!peer) {
		result.error = toolError(QString(
			"No chat %1 in this client's data").arg(chatId));
		return result;
	}
	const auto channel = peer->asChannel();
	if (!channel) {
		result.error = toolError(QString(
			"Chat %1 is not a channel, so it cannot be a community"
			).arg(chatId));
		return result;
	}
	if (!channel->isCommunity()) {
		result.error = toolError(QString(
			"Channel %1 ('%2') is not a community").arg(chatId).arg(
				channel->name()));
		return result;
	}
	result.channel = channel;
	return result;
}

QJsonObject Server::toolListCommunities(const QJsonObject &args) {
	return awaitMtp([&](auto done, auto fail) {
		_session->api().communities().requestJoinedCommunities([=](
				const std::vector<not_null<ChannelData*>> &list) {
			auto items = QJsonArray();
			for (const auto &channel : list) {
				items.append(CommunityBrief(channel));
			}
			auto value = QJsonObject();
			value["success"] = true;
			value["count"] = items.size();
			value["communities"] = items;
			done(value);
		});
	});
}

QJsonObject Server::toolGetCommunity(const QJsonObject &args) {
	const auto lookup = resolveCommunity(args);
	if (!lookup.channel) {
		return lookup.error;
	}
	const auto channel = not_null(lookup.channel);
	auto value = CommunityBrief(channel);
	value["success"] = true;
	value["about"] = channel->about();
	if (const auto username = channel->username(); !username.isEmpty()) {
		value["username"] = username;
	}

	// linkedPeers() is filled by a communityFull response. Reporting the
	// list as empty when it has simply not been fetched would be a lie the
	// caller could not detect, so the two cases are distinguished.
	const auto info = channel->communityInfo();
	if (!info) {
		value["linked_chats_known"] = false;
		return value;
	}
	value["linked_chats_known"] = true;
	auto chats = QJsonArray();
	for (const auto &linked : info->linkedPeers()) {
		auto entry = QJsonObject();
		entry["chat_id"] = qint64(linked.peer->id.value);
		entry["title"] = linked.peer->name();
		entry["joined"] = Data::CommunityChatJoined(linked.peer);
		entry["can_view_history"] = linked.canViewHistory;
		if (linked.visible.has_value()) {
			entry["visible"] = *linked.visible;
		}
		chats.append(entry);
	}
	value["linked_chats_list"] = chats;
	return value;
}

QJsonObject Server::toolCreateCommunity(const QJsonObject &args) {
	const auto title = args["title"].toString().trimmed();
	const auto about = args.value("about").toString();
	const auto hidden = args.value("hidden").toBool(false);
	const auto firstChatId = args["first_chat_id"].toVariant().toLongLong();
	if (title.isEmpty()) {
		return toolError("title cannot be empty");
	}

	// communities.create takes the first member chat as a required argument:
	// a community with nothing in it is not a state the server will produce.
	const auto peer = resolvePeer(firstChatId);
	if (!peer) {
		return toolError(QString(
			"No chat %1 in this client's data; a community is created around "
			"a first chat, so one has to be named").arg(firstChatId));
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().communities().create(
			title,
			about,
			peer,
			hidden,
			[=](not_null<ChannelData*> community) {
				auto value = CommunityBrief(community);
				value["success"] = true;
				done(value);
			},
			[=](const QString &error) {
				fail(error.isEmpty()
					? QString("Telegram accepted the request but returned no "
						"community to identify")
					: error);
			});
	});
}

QJsonObject Server::toolAddChatToCommunity(const QJsonObject &args) {
	const auto lookup = resolveCommunity(args);
	if (!lookup.channel) {
		return lookup.error;
	}
	const auto memberId = args["member_chat_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(memberId);
	if (!peer) {
		return toolError(QString(
			"No chat %1 in this client's data").arg(memberId));
	}
	const auto visible = args.value("visible").toBool(true);
	const auto community = not_null(lookup.channel);
	return awaitMtp([&](auto done, auto fail) {
		_session->api().communities().addPeerLink(
			community,
			peer,
			visible,
			[=] {
				auto value = QJsonObject();
				value["success"] = true;
				value["chat_id"] = qint64(community->id.value);
				value["member_chat_id"] = qint64(peer->id.value);
				value["visible"] = visible;
				done(value);
			},
			[=](const QString &error) {
				// The server answers a link that needs an admin's approval
				// with an error type, not a success. Saying "failed" would
				// be wrong -- the request exists and is waiting.
				if (error == Api::kCommunityRequestCreated.utf16()) {
					auto value = QJsonObject();
					value["success"] = true;
					value["pending_approval"] = true;
					value["chat_id"] = qint64(community->id.value);
					value["member_chat_id"] = qint64(peer->id.value);
					done(value);
					return;
				}
				fail(error.isEmpty() ? QString("Telegram refused the link")
					: error);
			});
	});
}

QJsonObject Server::toolRemoveChatFromCommunity(const QJsonObject &args) {
	const auto lookup = resolveCommunity(args);
	if (!lookup.channel) {
		return lookup.error;
	}
	const auto memberId = args["member_chat_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(memberId);
	if (!peer) {
		return toolError(QString(
			"No chat %1 in this client's data").arg(memberId));
	}
	const auto community = not_null(lookup.channel);
	return awaitMtp([&](auto done, auto fail) {
		_session->api().communities().removePeerLink(
			community,
			peer,
			[=] {
				auto value = QJsonObject();
				value["success"] = true;
				value["chat_id"] = qint64(community->id.value);
				value["member_chat_id"] = qint64(peer->id.value);
				done(value);
			},
			[=](const QString &error) {
				fail(error.isEmpty()
					? QString("Telegram refused to remove the chat")
					: error);
			});
	});
}

QJsonObject Server::toolSetCommunityCollapsed(const QJsonObject &args) {
	const auto lookup = resolveCommunity(args);
	if (!lookup.channel) {
		return lookup.error;
	}
	const auto collapsed = args["collapsed"].toBool();
	const auto community = not_null(lookup.channel);

	// This is the only community operation that is not a request/reply: the
	// client sets the flag locally and tells the server after the fact. There
	// is nothing to await, so the result reports the flag we just set rather
	// than one Telegram confirmed.
	_session->api().communities().toggleCollapsedInDialogs(
		community,
		collapsed);
	auto value = QJsonObject();
	value["success"] = true;
	value["chat_id"] = qint64(community->id.value);
	value["collapsed"] = community->collapsedInDialogs();
	return value;
}

QJsonObject Server::toolListCommunityJoinRequests(const QJsonObject &args) {
	const auto lookup = resolveCommunity(args);
	if (!lookup.channel) {
		return lookup.error;
	}
	const auto community = not_null(lookup.channel);
	const auto limit = clampLimit(
		args.value("limit").toInt(50),
		50,
		100);
	const auto offset = args.value("offset").toString();
	return awaitMtp([&](auto done, auto fail) {
		_session->api().communities().requestPeerLinkRequests(
			community,
			offset,
			limit,
			[=](Api::CommunityPeerRequestsSlice slice) {
				auto items = QJsonArray();
				for (const auto &request : slice.list) {
					auto entry = QJsonObject();
					entry["chat_id"] = qint64(request.peer->id.value);
					entry["title"] = request.peer->name();
					entry["date"] = qint64(request.date);
					entry["visible"] = request.visible;
					if (request.requestedBy) {
						entry["requested_by_id"]
							= qint64(request.requestedBy->id.value);
						entry["requested_by"] = request.requestedBy->name();
					}
					items.append(entry);
				}
				auto value = QJsonObject();
				value["success"] = true;
				value["count"] = items.size();
				value["total_count"] = slice.totalCount;
				value["requests"] = items;
				if (!slice.nextOffset.isEmpty()) {
					value["next_offset"] = slice.nextOffset;
				}
				done(value);
			});
	});
}

QJsonObject Server::toolReviewCommunityJoinRequest(const QJsonObject &args) {
	const auto lookup = resolveCommunity(args);
	if (!lookup.channel) {
		return lookup.error;
	}
	const auto community = not_null(lookup.channel);
	const auto approve = args["approve"].toBool();

	// One call covers both "decide about this chat" and "decide about every
	// pending chat", because the server has a separate method for each and a
	// caller should not have to know which one it wants.
	if (args.value("all").toBool(false)) {
		return awaitMtp([&](auto done, auto fail) {
			_session->api().communities().toggleAllPeerLinkRequestApproval(
				community,
				!approve,
				[=] {
					auto value = QJsonObject();
					value["success"] = true;
					value["chat_id"] = qint64(community->id.value);
					value["approved"] = approve;
					value["scope"] = "all";
					done(value);
				},
				[=](const QString &error) {
					fail(error.isEmpty()
						? QString("Telegram refused the decision")
						: error);
				});
		});
	}

	const auto memberId = args["member_chat_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(memberId);
	if (!peer) {
		return toolError(QString(
			"No chat %1 in this client's data; pass all=true to decide about "
			"every pending request instead").arg(memberId));
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().communities().togglePeerLinkRequestApproval(
			community,
			peer,
			!approve,
			[=] {
				auto value = QJsonObject();
				value["success"] = true;
				value["chat_id"] = qint64(community->id.value);
				value["member_chat_id"] = qint64(peer->id.value);
				value["approved"] = approve;
				value["scope"] = "one";
				done(value);
			},
			[=](const QString &error) {
				fail(error.isEmpty()
					? QString("Telegram refused the decision")
					: error);
			});
	});
}

} // namespace MCP
