// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"
#include "ui/text/text_entity.h"
#include "iv/iv_rich_page.h"
#include "iv/iv_rich_message_serializer.h"
#include "history/history.h" // NewMessageType::Existing (full enum, not just the fwd decl)
#include "base/random.h" // base::RandomValue for the sendMedia random_id
#include "api/api_polls.h" // Api::Polls::sendVotes for vote_poll
#include "data/data_poll.h" // PollData/PollAnswer for poll create/vote/results

namespace MCP {

// ===== SEND FORMATTING CORE (docs/DESIGN_MCP_SEND.md) =====
// A deep helper that hides all entity parsing/validation and MTProto option
// construction behind two calls, so every send tool stays thin.
namespace {

[[nodiscard]] std::optional<::EntityType> EntityTypeFromString(const QString &s) {
	const auto k = s.trimmed().toLower();
	if (k == "bold") return ::EntityType::Bold;
	if (k == "italic") return ::EntityType::Italic;
	if (k == "underline") return ::EntityType::Underline;
	if (k == "strikethrough" || k == "strike") return ::EntityType::StrikeOut;
	if (k == "spoiler") return ::EntityType::Spoiler;
	if (k == "code") return ::EntityType::Code;
	if (k == "pre") return ::EntityType::Pre;
	if (k == "blockquote") return ::EntityType::Blockquote;
	if (k == "text_link" || k == "text_url") return ::EntityType::CustomUrl;
	if (k == "url") return ::EntityType::Url;
	if (k == "mention") return ::EntityType::Mention;
	if (k == "hashtag") return ::EntityType::Hashtag;
	if (k == "custom_emoji") return ::EntityType::CustomEmoji;
	return std::nullopt;
}

// Compact Telegram-style markdown -> entities: ```pre```, **bold**, __underline__,
// ~~strike~~, ||spoiler||, `code`, *italic* / _italic_, [text](url). Best-effort
// convenience layered over the precise `entities` path.
[[nodiscard]] TextWithEntities ParseMarkdown(const QString &src) {
	struct Rule { QString open; ::EntityType type; };
	static const auto rules = std::vector<Rule>{
		{ "```", ::EntityType::Pre },
		{ "**", ::EntityType::Bold },
		{ "__", ::EntityType::Underline },
		{ "~~", ::EntityType::StrikeOut },
		{ "||", ::EntityType::Spoiler },
		{ "`", ::EntityType::Code },
		{ "*", ::EntityType::Italic },
		{ "_", ::EntityType::Italic },
	};
	auto out = QString();
	auto entities = EntitiesInText();
	const int n = src.size();
	int i = 0;
	while (i < n) {
		if (src[i] == '[') {
			const int close = src.indexOf(']', i + 1);
			if (close > i && close + 1 < n && src[close + 1] == '(') {
				const int paren = src.indexOf(')', close + 2);
				if (paren > close) {
					const auto text = src.mid(i + 1, close - i - 1);
					const auto url = src.mid(close + 2, paren - close - 2);
					entities.push_back(EntityInText(
						::EntityType::CustomUrl, out.size(), text.size(), url));
					out += text;
					i = paren + 1;
					continue;
				}
			}
		}
		auto matched = false;
		for (const auto &r : rules) {
			if (src.mid(i, r.open.size()) == r.open) {
				const int close = src.indexOf(r.open, i + r.open.size());
				if (close > i) {
					const auto inner = src.mid(
						i + r.open.size(), close - i - r.open.size());
					entities.push_back(EntityInText(
						r.type, out.size(), inner.size()));
					out += inner;
					i = close + r.open.size();
					matched = true;
					break;
				}
			}
		}
		if (!matched) {
			out += src[i];
			++i;
		}
	}
	return TextWithEntities{ out, entities };
}

// Resolve text + formatting into a validated TextWithEntities. A precise
// `entities` array wins (full MTProto surface); else `parse_mode` markdown;
// auto-links/mentions/hashtags are always detected. Out-of-range entities are
// dropped (errors defined out of existence).
[[nodiscard]] TextWithEntities ResolveFormatting(
		const QString &text,
		const QString &parseMode,
		const QJsonArray &entities) {
	if (!entities.isEmpty()) {
		auto result = TextWithEntities{ text, EntitiesInText() };
		for (const auto &v : entities) {
			const auto o = v.toObject();
			const auto type = EntityTypeFromString(o.value("type").toString());
			if (!type) {
				continue;
			}
			auto data = QString();
			if (*type == ::EntityType::CustomUrl) {
				data = o.value("url").toString();
			} else if (*type == ::EntityType::Pre) {
				data = o.value("language").toString();
			} else if (*type == ::EntityType::CustomEmoji) {
				data = QString::number(
					o.value("custom_emoji_id").toVariant().toLongLong());
			}
			auto ent = EntityInText(
				*type,
				o.value("offset").toInt(),
				o.value("length").toInt(),
				data);
			if (ent.validForText(text.size())) {
				result.entities.push_back(ent);
			}
		}
		return result;
	}
	const auto mode = parseMode.toLower();
	auto result = (mode == "markdown" || mode == "md" || mode == "markdownv2")
		? ParseMarkdown(text)
		: TextWithEntities{ text, EntitiesInText() };
	TextUtilities::ParseEntities(
		result,
		TextParseHashtags | TextParseMentions);
	return result;
}

// Build the SendAction (reply target + options) from the caller's args.
[[nodiscard]] Api::SendAction BuildSendAction(
		not_null<History*> history,
		PeerId peer,
		qint64 replyToId,
		bool silent,
		TimeId scheduleDate) {
	auto options = Api::SendOptions();
	options.silent = silent;
	if (scheduleDate > 0) {
		options.scheduled = scheduleDate;
	}
	auto action = Api::SendAction(history, options);
	if (replyToId > 0) {
		action.replyTo.messageId = FullMsgId(peer, MsgId(replyToId));
	}
	return action;
}

// Build a RichPage from the caller's block list. Each block is one of the text
// kinds the send path supports today (heading / paragraph / quote / code /
// divider); its text carries full formatting through ResolveFormatting (a
// block's own `entities` win, else the page-level parse_mode). Media / list /
// table kinds require an already-uploaded object and are refused with the real
// reason rather than faked (governing rule: report what happened). Returns
// nullptr and sets `error` on the first unusable block, so the tool never
// serializes a half-built page.
[[nodiscard]] std::shared_ptr<Iv::RichPage> BuildRichPage(
		const QJsonArray &blocks,
		const QString &pageParseMode,
		QString *error) {
	auto page = std::make_shared<Iv::RichPage>();
	page->blocks.reserve(blocks.size());
	for (auto index = 0; index < blocks.size(); ++index) {
		const auto o = blocks[index].toObject();
		const auto type = o.value("type").toString().trimmed().toLower();
		const auto text = o.value("text").toString();
		const auto entities = o.value("entities").toArray();
		const auto parseMode = o.contains("parse_mode")
			? o.value("parse_mode").toString()
			: pageParseMode;

		auto block = Iv::RichPage::Block();
		const auto applyText = [&](bool markdown) {
			block.text.text = markdown
				? ResolveFormatting(text, parseMode, entities)
				: ResolveFormatting(text, QString(), entities);
		};

		if (type == "paragraph" || type == "text" || type.isEmpty()) {
			block.kind = Iv::RichPage::BlockKind::Paragraph;
			applyText(true);
		} else if (type == "heading" || type == "header") {
			block.kind = Iv::RichPage::BlockKind::Heading;
			block.headingLevel = qBound(1, o.value("level").toInt(2), 6);
			applyText(true);
		} else if (type == "quote" || type == "blockquote") {
			block.kind = Iv::RichPage::BlockKind::Quote;
			block.pullquote = o.value("pullquote").toBool(false);
			applyText(true);
		} else if (type == "code" || type == "pre") {
			block.kind = Iv::RichPage::BlockKind::Code;
			block.language = o.value("language").toString();
			applyText(false); // code is literal — never markdown-parsed
		} else if (type == "divider" || type == "hr") {
			block.kind = Iv::RichPage::BlockKind::Divider;
		} else {
			*error = QString("block %1: unsupported type '%2' — the send path "
				"supports heading, paragraph, quote, code, divider (media, "
				"list and table blocks require an uploaded object and are not "
				"sendable here)").arg(index).arg(type);
			return nullptr;
		}

		if (block.kind != Iv::RichPage::BlockKind::Divider
			&& block.text.text.text.isEmpty()) {
			*error = QString("block %1 (%2): text is required")
				.arg(index).arg(type);
			return nullptr;
		}
		page->blocks.push_back(std::move(block));
	}
	return page;
}

} // namespace

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
					chat["id"] = qint64(peer->id.value);
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
			chatInfo["chat_id"] = qint64(chatId);
			return chatInfo;
		}

		// Basic information
		chatInfo["id"] = qint64(peer->id.value);
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
		chatInfo["chat_id"] = qint64(chatId);
		chatInfo["error"] = "Chat info not available (archiver not initialized)";
		chatInfo["source"] = "error";
		return chatInfo;
	}
	chatInfo = _archiver->getChatInfo(chatId);
	if (chatInfo.isEmpty() || !chatInfo.contains("id")) {
		chatInfo["chat_id"] = qint64(chatId);
		chatInfo["error"] = "Chat info not available (session not active)";
		chatInfo["source"] = "error";
	} else {
		chatInfo["source"] = "archived_data";
	}

	return chatInfo;
}

// The raw server-side history API: one page of messages.getHistory straight
// from Telegram, carrying the chat's TRUE total (`count`) and REAL media byte
// sizes. This is the API a Rust export engine pages on — it does no exporting
// itself. Unlike read_messages (local cache) it reaches the whole chat, and
// unlike the gradual engine's estimate it never lies about the total.
QJsonObject Server::toolGetChatHistory(const QJsonObject &args) {
	if (!_session) {
		return toolError("No active session");
	}
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto offsetId = args.value("offset_id").toVariant().toLongLong();
	const auto limit = clampLimit(args.value("limit").toInt(100), 200);

	auto peer = resolvePeer(chatId);
	if (!peer) {
		return toolError(QString("Chat %1 is not loaded; open it once in the "
			"client, or use list_chats to get a valid chat_id").arg(chatId));
	}

	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_GetHistory(
			peer->input(),
			MTP_int(int(offsetId)), // offset_id: 0 = newest; page older via next_offset_id
			MTP_int(0),             // offset_date
			MTP_int(0),             // add_offset
			MTP_int(int(limit)),    // limit
			MTP_int(0),             // max_id
			MTP_int(0),             // min_id
			MTP_long(0)             // hash
		)).done([=](const MTPmessages_Messages &result) {
			int total = 0;
			const QVector<MTPMessage> *list = nullptr;
			result.match([&](const MTPDmessages_messages &d) {
				_session->data().processUsers(d.vusers());
				_session->data().processChats(d.vchats());
				total = int(d.vmessages().v.size()); // small chat: fully returned
				list = &d.vmessages().v;
			}, [&](const MTPDmessages_messagesSlice &d) {
				_session->data().processUsers(d.vusers());
				_session->data().processChats(d.vchats());
				total = d.vcount().v;                // the chat's REAL total
				list = &d.vmessages().v;
			}, [&](const MTPDmessages_channelMessages &d) {
				_session->data().processUsers(d.vusers());
				_session->data().processChats(d.vchats());
				total = d.vcount().v;                // the chat's REAL total
				list = &d.vmessages().v;
			}, [&](const MTPDmessages_messagesNotModified &) {
			});
			// Load the fetched messages into the data layer, so the same ids can
			// then be resolved by download_media and the read tools (a raw MTP
			// page otherwise leaves the client's data session empty).
			if (list) {
				_session->data().processMessages(
					*list, NewMessageType::Existing);
			}

			QJsonArray msgs;
			qint64 oldest = 0;
			if (list) {
				for (const auto &m : *list) {
					m.match([&](const MTPDmessage &data) {
						QJsonObject o;
						const auto id = qint64(data.vid().v);
						o["id"] = id;
						o["date"] = qint64(data.vdate().v);
						o["out"] = data.is_out();
						const auto text = data.vmessage().v;
						if (!text.isEmpty()) {
							o["text"] = QString::fromUtf8(text);
						}
						if (const auto media = data.vmedia()) {
							media->match([&](const MTPDmessageMediaDocument &mm) {
								o["media_type"] = "document";
								if (const auto doc = mm.vdocument()) {
									doc->match([&](const MTPDdocument &dd) {
										o["media_size"] = qint64(dd.vsize().v);
										o["media_mime"] = QString::fromUtf8(dd.vmime_type().v);
										// A document carrying documentAttributeVideo is a
										// video message (inline/streamable), not a plain
										// file — surface that so callers can tell a
										// send_video post from a send_document one.
										for (const auto &attr : dd.vattributes().v) {
											attr.match([&](const MTPDdocumentAttributeVideo &vid) {
												o["is_video"] = true;
												o["supports_streaming"] = vid.is_supports_streaming();
												o["duration"] = vid.vduration().v;
												o["width"] = qint64(vid.vw().v);
												o["height"] = qint64(vid.vh().v);
											}, [](const auto &) {});
										}
									}, [](const MTPDdocumentEmpty &) {});
								}
							}, [&](const MTPDmessageMediaPhoto &mm) {
								o["media_type"] = "photo";
								if (const auto photo = mm.vphoto()) {
									photo->match([&](const MTPDphoto &pp) {
										qint64 best = 0;
										for (const auto &sz : pp.vsizes().v) {
											sz.match([&](const MTPDphotoSize &s) {
												best = std::max(best, qint64(s.vsize().v));
											}, [&](const MTPDphotoSizeProgressive &s) {
												for (const auto &b : s.vsizes().v) {
													best = std::max(best, qint64(b.v));
												}
											}, [&](const auto &) {});
										}
										if (best > 0) {
											o["media_size"] = best; // REAL, not a 512KB guess
										}
									}, [](const MTPDphotoEmpty &) {});
								}
							}, [&](const auto &) {
								o["media_type"] = "other";
							});
						}
						if (oldest == 0 || id < oldest) {
							oldest = id;
						}
						msgs.append(o);
					}, [&](const MTPDmessageService &data) {
						QJsonObject o;
						const auto id = qint64(data.vid().v);
						o["id"] = id;
						o["date"] = qint64(data.vdate().v);
						o["service"] = true;
						if (oldest == 0 || id < oldest) {
							oldest = id;
						}
						msgs.append(o);
					}, [&](const MTPDmessageEmpty &) {});
				}
			}

			QJsonObject value;
			value["success"] = true;
			value["chat_id"] = chatId;
			value["count"] = total;           // the chat's TRUE message total
			value["returned"] = int(msgs.size());
			value["messages"] = msgs;
			value["next_offset_id"] = oldest; // page older from here; 0 = none
			value["has_more"] = (oldest != 0 && total > int(msgs.size()));
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail(error.type());
		}).send();
	});
}

// Download the media file attached to one message to disk, so an exporter can
// keep the actual bytes, not just a size. Reuses the client's own downloader
// (document->save / photo->load with a per-message FileOrigin), the same path
// the UI and the archiver use — nothing is re-implemented. Blocks on a nested
// event loop until the file is written or the timeout fires; a message with no
// media, or an unloaded message/peer, is an honest error, not an empty success.
QJsonObject Server::toolDownloadMedia(const QJsonObject &args) {
	if (!_session) {
		return toolError("No active session");
	}
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto messageId = args["message_id"].toVariant().toLongLong();
	if (!chatId || !messageId) {
		return toolError("chat_id and message_id are required");
	}
	auto peer = resolvePeer(chatId);
	if (!peer) {
		return toolError(QString("Chat %1 is not loaded; open it once in the "
			"client, or use list_chats to get a valid chat_id").arg(chatId));
	}
	const auto item = _session->data().message(peer->id, MsgId(messageId));
	if (!item) {
		return toolError(QString("Message %1 in chat %2 is not loaded; fetch it "
			"with get_chat_history first").arg(messageId).arg(chatId));
	}
	const auto media = item->media();
	if (!media) {
		return toolError("Message has no downloadable media");
	}

	// Destination: out_path (a full file path) wins; else out_dir plus the file's
	// own Telegram name; else a temp file — so even a bare call lands somewhere.
	const auto outPath = args.value("out_path").toString();
	const auto outDir = args.value("out_dir").toString();
	const auto chooseTarget = [&](const QString &suggested) -> QString {
		if (!outPath.isEmpty()) {
			QFileInfo(outPath).dir().mkpath(".");
			return outPath;
		}
		const auto dir = outDir.isEmpty() ? QDir::tempPath() : outDir;
		QDir().mkpath(dir);
		return QDir(dir).filePath(suggested);
	};

	const auto origin = Data::FileOrigin(
		Data::FileOriginMessage(peer->id, MsgId(messageId)));

	if (const auto doc = media->document()) {
		auto name = doc->filename();
		if (name.isEmpty()) {
			name = QString("document_%1").arg(messageId);
		}
		const auto target = chooseTarget(name);
		const auto mime = doc->mimeString();

		// Already on disk? Serve it from the client's cache — no network. The
		// copy to `target` can fail (unwritable dir, locked file), so verify it
		// landed before reporting success; if it didn't, fall back to the real
		// cached path (the bytes ARE on disk there) rather than claim success
		// for a file that isn't there.
		const auto existing = doc->filepath(true);
		if (!existing.isEmpty() && QFile::exists(existing)) {
			const auto cacheResult = [&](const QString &path) {
				return QJsonObject{
					{ "success", true },
					{ "path", path },
					{ "bytes", qint64(QFileInfo(path).size()) },
					{ "mime", mime },
					{ "source", "cache" },
				};
			};
			if (existing == target) {
				return cacheResult(target);
			}
			QFile::remove(target);
			if (QFile::copy(existing, target)
				&& QFile::exists(target)
				&& QFileInfo(target).size() > 0) {
				return cacheResult(target);
			}
			// Couldn't place it at `target`; the cached original is still valid.
			if (QFileInfo(existing).size() > 0) {
				return cacheResult(existing);
			}
			return toolError(
				"media is cached but could not be copied to the destination");
		}

		rpl::lifetime lifetime;
		return awaitMtp([&](auto done, auto fail) {
			const auto finish = [=]() {
				const auto local = doc->filepath(true);
				const auto path = (!local.isEmpty() && QFile::exists(local))
					? local : target;
				if (QFile::exists(path) && QFileInfo(path).size() > 0) {
					done(QJsonObject{
						{ "success", true },
						{ "path", path },
						{ "bytes", qint64(QFileInfo(path).size()) },
						{ "mime", mime },
						{ "source", "cloud" },
					});
				} else {
					fail("download finished but no file was written");
				}
			};
			// Doc-specific progress stream; fire once loading has stopped.
			_session->data().documentLoadProgress(
			) | rpl::filter([doc](not_null<DocumentData*> d) {
				return d == doc && !doc->loading();
			}) | rpl::take(1) | rpl::on_next([finish](not_null<DocumentData*>) {
				finish();
			}, lifetime);

			doc->save(origin, target, LoadFromCloudOrLocal, false);
			if (!doc->loading()) {
				finish(); // completed synchronously (already cached elsewhere)
			}
		}, 180000);
	}

	if (const auto photo = media->photo()) {
		const auto target = chooseTarget(QString("photo_%1.jpg").arg(messageId));
		// Create the media view BEFORE load(): a cached photo's done callback
		// fires synchronously and set()s into the active view; without one the
		// bytes are discarded (learned in the archiver).
		auto view = photo->createMediaView();

		rpl::lifetime lifetime;
		return awaitMtp([&](auto done, auto fail) {
			const auto finish = [=, &view]() {
				if (view->saveToFile(target)
					&& QFile::exists(target)
					&& QFileInfo(target).size() > 0) {
					done(QJsonObject{
						{ "success", true },
						{ "path", target },
						{ "bytes", qint64(QFileInfo(target).size()) },
						{ "mime", QString("image/jpeg") },
						{ "source", "cloud" },
					});
				} else {
					fail("photo loaded but could not be written");
				}
			};
			// Photos have no per-photo stream; wait on the global downloader
			// and gate on this view being loaded.
			_session->downloaderTaskFinished(
			) | rpl::filter([view] {
				return view->loaded();
			}) | rpl::take(1) | rpl::on_next([finish](auto&&) {
				finish();
			}, lifetime);

			photo->load(Data::PhotoSize::Large, origin, LoadFromCloudOrLocal, false);
			if (view->loaded()) {
				finish(); // already cached
			}
		}, 180000);
	}

	return toolError("Message media is neither a document nor a photo");
}

// ===== Gap-closing read tools (layer 229) ==================================
// Plain MTP reads the surface was missing (see docs/API_GAP_ANALYSIS.md). Each
// mirrors the awaitMtp honest-await idiom: block on the reply, return the real
// result, never "submitted".

// users.getFullUser — the FULL profile (bio/about, common-chats count) that
// get_user_info (cache-only) never fetches.
QJsonObject Server::toolGetFullUser(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto userId = args["user_id"].toVariant().toLongLong();
	auto peer = resolvePeer(userId);
	const auto user = peer ? peer->asUser() : nullptr;
	if (!user) return toolError(QString("User %1 not loaded; open it once or use list_chats").arg(userId));
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPusers_GetFullUser(
			user->inputUser()
		)).done([=](const MTPusers_UserFull &result) {
			result.match([&](const MTPDusers_userFull &d) {
				_session->data().processUsers(d.vusers());
				_session->data().processChats(d.vchats());
				QJsonObject o;
				o["success"] = true;
				o["user_id"] = userId;
				d.vfull_user().match([&](const MTPDuserFull &f) {
					o["about"] = QString::fromUtf8(f.vabout().value_or_empty());
					o["common_chats_count"] = f.vcommon_chats_count().v;
					o["blocked"] = f.is_blocked();
					o["phone_calls_available"] = f.is_phone_calls_available();
					o["voice_messages_forbidden"] = f.is_voice_messages_forbidden();
				});
				done(o);
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// contacts.resolvePhone — a phone number -> the resolved user/peer id.
QJsonObject Server::toolResolvePhone(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto phone = args["phone"].toString();
	if (phone.isEmpty()) return toolError("phone is required");
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPcontacts_ResolvePhone(
			MTP_string(phone)
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			result.match([&](const MTPDcontacts_resolvedPeer &d) {
				_session->data().processUsers(d.vusers());
				_session->data().processChats(d.vchats());
				QJsonObject o;
				o["success"] = true;
				o["phone"] = phone;
				o["peer_id"] = qint64(peerFromMTP(d.vpeer()).value);
				done(o);
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// messages.getMessagesViews — per-message view + forward counts.
QJsonObject Server::toolGetMessageViews(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	auto peer = resolvePeer(chatId);
	if (!peer) return toolError(QString("Chat %1 not loaded").arg(chatId));
	QVector<MTPint> ids;
	for (const auto &v : args["message_ids"].toArray()) {
		ids.push_back(MTP_int(int(v.toVariant().toLongLong())));
	}
	if (ids.isEmpty()) return toolError("message_ids (array) is required");
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_GetMessagesViews(
			peer->input(),
			MTP_vector<MTPint>(ids),
			MTP_bool(false) // increment: read-only, do not bump the counter
		)).done([=](const MTPmessages_MessageViews &result) {
			result.match([&](const MTPDmessages_messageViews &d) {
				_session->data().processUsers(d.vusers());
				_session->data().processChats(d.vchats());
				QJsonArray arr;
				const auto &views = d.vviews().v;
				for (auto i = 0; i != views.size() && i != ids.size(); ++i) {
					views[i].match([&](const MTPDmessageViews &v) {
						QJsonObject o;
						o["message_id"] = qint64(ids[i].v);
						o["views"] = v.vviews().value_or_empty();
						o["forwards"] = v.vforwards().value_or_empty();
						arr.append(o);
					});
				}
				done(QJsonObject{{"success", true}, {"chat_id", chatId}, {"views", arr}});
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// stories.getPeerStories — a peer's currently-active stories (ids + dates).
QJsonObject Server::toolGetPeerStories(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	auto peer = resolvePeer(chatId);
	if (!peer) return toolError(QString("Peer %1 not loaded").arg(chatId));
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPstories_GetPeerStories(
			peer->input()
		)).done([=](const MTPstories_PeerStories &result) {
			result.match([&](const MTPDstories_peerStories &d) {
				_session->data().processUsers(d.vusers());
				_session->data().processChats(d.vchats());
				QJsonArray arr;
				d.vstories().match([&](const MTPDpeerStories &ps) {
					for (const auto &s : ps.vstories().v) {
						s.match([&](const MTPDstoryItem &it) {
							QJsonObject o;
							o["id"] = it.vid().v;
							o["date"] = qint64(it.vdate().v);
							o["caption"] = QString::fromUtf8(it.vcaption().value_or_empty());
							arr.append(o);
						}, [&](const MTPDstoryItemSkipped &it) {
							arr.append(QJsonObject{{"id", it.vid().v}, {"skipped", true}});
						}, [](const MTPDstoryItemDeleted &) {});
					}
				});
				done(QJsonObject{{"success", true}, {"chat_id", chatId}, {"count", int(arr.size())}, {"stories", arr}});
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// stories.getAllStories — the followed-peers stories feed (peers + story counts).
QJsonObject Server::toolGetAllStories(const QJsonObject &args) {
	Q_UNUSED(args);
	if (!_session) return toolError("No active session");
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPstories_GetAllStories(
			MTP_flags(0), MTP_string()
		)).done([=](const MTPstories_AllStories &result) {
			result.match([&](const MTPDstories_allStories &d) {
				_session->data().processUsers(d.vusers());
				_session->data().processChats(d.vchats());
				QJsonArray arr;
				for (const auto &ps : d.vpeer_stories().v) {
					ps.match([&](const MTPDpeerStories &p) {
						arr.append(QJsonObject{
							{"peer_id", qint64(peerFromMTP(p.vpeer()).value)},
							{"story_count", int(p.vstories().v.size())},
						});
					});
				}
				done(QJsonObject{{"success", true}, {"count", d.vcount().v}, {"peers", arr}});
			}, [&](const MTPDstories_allStoriesNotModified &) {
				done(QJsonObject{{"success", true}, {"not_modified", true}});
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
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
					msg["message_id"] = qint64(item->id.bare);
					msg["date"] = static_cast<qint64>(item->date());

					// Get message text
					const auto &text = item->originalText();
					msg["text"] = text.text;

					// Get sender information
					auto from = item->from();
					if (from) {
						QJsonObject fromUser;
						fromUser["id"] = qint64(from->id.value);
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
						reply["message_id"] = qint64(item->replyToId().bare);
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
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto text = args["text"].toString();
	if (chatId == 0) {
		return toolError("chat_id is required and must be non-zero");
	}
	if (text.isEmpty()) {
		return toolError("text is required and must not be empty");
	}
	if (!_session) {
		return toolError("Session not available");
	}
	const auto peer = PeerId(chatId);
	if (!resolvePeer(chatId)) {
		return toolError("Chat not found");
	}
	const auto history = _session->data().history(peer);

	// Read every advertised arg here (the handler is a thin extractor; the deep
	// helpers do the parsing/serialization — docs/DESIGN_MCP_SEND.md).
	const auto entities = args.value("entities").toArray();
	const auto parseMode = args.value("parse_mode").toString();
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	const auto scheduleDate = TimeId(
		args.value("schedule_date").toVariant().toLongLong());
	const auto linkPreview = args.value("link_preview");
	const auto replyMarkup = args.value("reply_markup");

	const auto resolved = ResolveFormatting(text, parseMode, entities);
	auto action = BuildSendAction(history, peer, replyToId, silent, scheduleDate);
	auto message = Api::MessageToSend(action);
	message.textWithTags = TextWithTags{
		resolved.text,
		TextUtilities::ConvertEntitiesToTextTags(resolved.entities),
	};
	if (linkPreview.isBool() && !linkPreview.toBool()) {
		message.webPage.removed = true;
	}
	_session->api().sendMessage(std::move(message));

	QJsonObject result;
	result["success"] = true;
	result["chat_id"] = chatId;
	result["text"] = resolved.text;
	result["entities_applied"] = int(resolved.entities.size());
	// Inline keyboards (reply_markup) are a bot-only capability; a user session
	// cannot attach one. Report honestly rather than silently dropping it.
	if (!replyMarkup.isNull() && !replyMarkup.isUndefined()) {
		result["reply_markup_ignored"]
			= "inline keyboards require a bot account; not sent from a user session";
	}
	result["status"] = "Message queued for sending";
	qInfo() << "MCP: Queued formatted message to chat" << chatId
		<< "with" << resolved.entities.size() << "entities";
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
	// Read every advertised arg (thin handler; the deep helpers do the work).
	const auto entities = args.value("entities").toArray();
	const auto parseMode = args.value("parse_mode").toString();
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	const auto scheduleDate = TimeId(
		args.value("schedule_date").toVariant().toLongLong());
	if (!caption.isEmpty()) {
		// Full caption formatting (entities / parse_mode) via the deep send core.
		const auto resolved = ResolveFormatting(caption, parseMode, entities);
		list.files.back().caption = TextWithTags{
			resolved.text,
			TextUtilities::ConvertEntitiesToTextTags(resolved.entities),
		};
	}

	_session->api().sendFiles(
		std::move(list),
		SendMediaType::File,
		nullptr, // not an album
		BuildSendAction(history, PeerId(chatId), replyToId, silent, scheduleDate));

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

// Send a local video file as a Telegram VIDEO message (inline, streamable) —
// NOT a document attachment. Identical to send_document (same PrepareMediaList,
// the same chunked upload with the account's 2 GB/4 GB ceiling, the same
// caption/reply/silent/schedule handling) EXCEPT it hands sendFiles
// SendMediaType::Photo instead of ::File. For a detected video that flips
// forceFile off (apiwrap.cpp: forceFile = (type==File) && file is Video), so the
// FileLoadTask builds a documentAttributeVideo with f_supports_streaming and the
// client library's own w/h/duration/thumbnail detection — the send_file(
// force_document=False, video=True, supports_streaming=True) path. No ffprobe:
// the library reads the mp4 itself; a non-video file just sends as its media.
QJsonObject Server::toolSendVideo(const QJsonObject &args) {
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
		const auto reason = (list.error == Ui::PreparedList::Error::TooLargeFile)
			? QString("file exceeds this account's upload limit")
			: QString("could not prepare file (error %1)"
				).arg(int(list.error));
		return toolError(reason + ": " + list.errorData);
	}
	if (list.files.empty()) {
		return toolError("File prepared to an empty list: " + path);
	}

	// Read every advertised arg (mirror send_document for the shared fields).
	const auto entities = args.value("entities").toArray();
	const auto parseMode = args.value("parse_mode").toString();
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	const auto scheduleDate = TimeId(
		args.value("schedule_date").toVariant().toLongLong());
	const auto supportsStreaming = args.value("supports_streaming").toBool(true);

	// Honor supports_streaming: a well-formed (faststart) mp4 the library reads
	// as streamable already gets the flag; set it on the prepared video info so
	// an explicit request is respected. Guarded — a non-video file has no Video
	// information and is left to send as its natural media.
	if (supportsStreaming) {
		for (auto &file : list.files) {
			if (file.information) {
				if (auto video = std::get_if<Ui::PreparedFileInformation::Video>(
						&file.information->media)) {
					video->supportsStreaming = true;
				}
			}
		}
	}

	if (!caption.isEmpty()) {
		const auto resolved = ResolveFormatting(caption, parseMode, entities);
		list.files.back().caption = TextWithTags{
			resolved.text,
			TextUtilities::ConvertEntitiesToTextTags(resolved.entities),
		};
	}

	// SendMediaType::Photo == "send as natural media" -> a video goes as a
	// streamable video; ::File would force a document (that's send_document).
	_session->api().sendFiles(
		std::move(list),
		SendMediaType::Photo,
		nullptr, // not an album
		BuildSendAction(history, PeerId(chatId), replyToId, silent, scheduleDate));

	QJsonObject result;
	result["success"] = true;
	result["chat_id"] = chatId;
	result["file_path"] = info.absoluteFilePath();
	result["file_name"] = info.fileName();
	result["size"] = qint64(info.size());
	result["supports_streaming"] = supportsStreaming;
	if (!caption.isEmpty()) {
		result["caption"] = caption;
	}
	// Async like every send path: the upload is queued, so no message id yet.
	result["status"] = "Video queued for upload";

	qInfo() << "MCP: Queued video" << info.fileName()
		<< "(" << info.size() << "bytes ) to chat" << chatId;
	return result;
}

// ===== INPUT-MEDIA SENDS (location / venue / contact / dice) =====
// A deep helper that hides the messages.sendMedia round-trip: build the flags,
// an optional reply and a random id, issue ONE raw SendMedia and apply the
// returned Updates so the message appears locally. The four tools below are
// thin wrappers that read their own args and construct the MTPInputMedia.
// (These media types carry no user-visible caption, so none is sent.)
QJsonObject Server::sendInputMedia(
		qint64 chatId,
		const MTPInputMedia &media,
		qint64 replyToId,
		bool silent,
		const QString &kind) {
	if (!_session) return toolError("No active session");
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError(QString("Chat %1 not found").arg(chatId));

	auto replyTo = replyToId
		? MTP_inputReplyToMessage(
			MTP_flags(0),
			MTP_int(int(replyToId)),
			MTPint(), // top_msg_id
			MTPInputPeer(), // reply_to_peer_id
			MTPstring(), // quote_text
			MTPVector<MTPMessageEntity>(), // quote_entities
			MTPint(), // quote_offset
			MTPInputPeer(), // monoforum_peer_id
			MTPint(), // todo_item_id
			MTPbytes()) // poll_option
		: MTPInputReplyTo();

	using Flag = MTPmessages_sendMedia::Flag;
	const auto flags = Flag(0)
		| (silent ? Flag::f_silent : Flag(0))
		| (replyToId ? Flag::f_reply_to : Flag(0));
	const auto randomId = base::RandomValue<uint64>();

	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_SendMedia(
			MTP_flags(flags),
			peer->input(),
			replyTo,
			media,
			MTP_string(QString()), // message: no caption for these media types
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(), // entities
			MTPint(), // schedule_date
			MTPint(), // schedule_repeat_period
			MTPInputPeer(), // send_as
			MTPInputQuickReplyShortcut(),
			MTPlong(), // effect
			MTPlong(), // allow_paid_stars
			MTPSuggestedPost()
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			done(QJsonObject{
				{"success", true},
				{"chat_id", chatId},
				{"kind", kind},
				{"status", QString("%1 sent").arg(kind)},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// messages.sendMedia(inputMediaGeoPoint) — a static location pin.
QJsonObject Server::toolSendLocation(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	if (!args.contains("latitude") || !args.contains("longitude")) {
		return toolError("latitude and longitude are required");
	}
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	auto geo = MTP_inputGeoPoint(
		MTP_flags(0),
		MTP_double(args["latitude"].toDouble()),
		MTP_double(args["longitude"].toDouble()),
		MTPint());
	return sendInputMedia(
		chatId, MTP_inputMediaGeoPoint(geo), replyToId, silent, "location");
}

// messages.sendMedia(inputMediaVenue) — a named place (title + address + geo).
QJsonObject Server::toolSendVenue(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	if (!args.contains("latitude") || !args.contains("longitude")) {
		return toolError("latitude and longitude are required");
	}
	const auto title = args.value("title").toString();
	const auto address = args.value("address").toString();
	if (title.isEmpty() || address.isEmpty()) {
		return toolError("title and address are required");
	}
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	auto geo = MTP_inputGeoPoint(
		MTP_flags(0),
		MTP_double(args["latitude"].toDouble()),
		MTP_double(args["longitude"].toDouble()),
		MTPint());
	auto venue = MTP_inputMediaVenue(
		geo,
		MTP_string(title),
		MTP_string(address),
		MTP_string(args.value("provider").toString()),
		MTP_string(args.value("venue_id").toString()),
		MTP_string(args.value("venue_type").toString()));
	return sendInputMedia(chatId, venue, replyToId, silent, "venue");
}

// messages.sendMedia(inputMediaContact) — a shared phone contact card.
QJsonObject Server::toolSendContact(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto phone = args.value("phone_number").toString();
	const auto first = args.value("first_name").toString();
	if (phone.isEmpty() || first.isEmpty()) {
		return toolError("phone_number and first_name are required");
	}
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	auto contact = MTP_inputMediaContact(
		MTP_string(phone),
		MTP_string(first),
		MTP_string(args.value("last_name").toString()),
		MTP_string(args.value("vcard").toString()));
	return sendInputMedia(chatId, contact, replyToId, silent, "contact");
}

// messages.sendMedia(inputMediaDice) — an animated dice/dart/etc. emoji.
QJsonObject Server::toolSendDice(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto emoticon = args.value("emoticon").toString().isEmpty()
		? QString::fromUtf8("\xF0\x9F\x8E\xB2") // 🎲 default
		: args.value("emoticon").toString();
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	return sendInputMedia(
		chatId, MTP_inputMediaDice(MTP_string(emoticon)), replyToId, silent, "dice");
}

// ===== FILE-UPLOAD SENDS (photo / gif / audio) =====
// Shared core: prepare the local file, apply caption + reply, hand to
// ApiWrap::sendFiles with the caller's SendMediaType. Photo == natural media
// (an image becomes a compressed photo, an animation a looping gif); File ==
// forced document (an audio file becomes a playable music message).
// ponytail: send_document/send_video predate this helper and still inline the
// same prepare-and-send steps — migrate them here if this pattern grows again.
QJsonObject Server::sendPreparedFile(
		qint64 chatId,
		const QString &path,
		const TextWithTags &caption,
		qint64 replyToId,
		bool silent,
		TimeId scheduleDate,
		SendMediaType type,
		const QString &kind) {
	if (!_session) return toolError("Session not available");
	const auto info = QFileInfo(path);
	if (!info.exists() || !info.isFile()) {
		return toolError("file_path is not an existing file: " + path);
	}
	const auto history = _session->data().history(PeerId(chatId));
	if (!history || !resolvePeer(chatId)) return toolError("Chat not found");

	const auto premium = _session->user()->isPremium();
	auto list = Storage::PrepareMediaList(
		QStringList(info.absoluteFilePath()),
		st::sendMediaPreviewSize,
		premium);
	if (list.error != Ui::PreparedList::Error::None) {
		const auto reason = (list.error == Ui::PreparedList::Error::TooLargeFile)
			? QString("file exceeds this account's upload limit")
			: QString("could not prepare file (error %1)").arg(int(list.error));
		return toolError(reason + ": " + list.errorData);
	}
	if (list.files.empty()) {
		return toolError("File prepared to an empty list: " + path);
	}
	if (!caption.text.isEmpty()) {
		list.files.back().caption = caption;
	}

	_session->api().sendFiles(
		std::move(list),
		type,
		nullptr, // not an album
		BuildSendAction(history, PeerId(chatId), replyToId, silent, scheduleDate));

	QJsonObject result;
	result["success"] = true;
	result["chat_id"] = chatId;
	result["file_path"] = info.absoluteFilePath();
	result["file_name"] = info.fileName();
	result["size"] = qint64(info.size());
	result["kind"] = kind;
	// Async like every send path: the upload is queued, so no message id yet.
	result["status"] = kind + " queued for upload";
	qInfo() << "MCP: Queued" << kind << info.fileName()
		<< "(" << info.size() << "bytes ) to chat" << chatId;
	return result;
}

// Read the shared file-send args (caption formatting + reply/silent/schedule)
// and resolve the caption once, so each thin file-send wrapper stays tiny.
namespace {
[[nodiscard]] TextWithTags ResolveCaptionTags(const QJsonObject &args) {
	const auto caption = args.value("caption").toString();
	if (caption.isEmpty()) {
		return TextWithTags();
	}
	const auto resolved = ResolveFormatting(
		caption,
		args.value("parse_mode").toString(),
		args.value("entities").toArray());
	return TextWithTags{
		resolved.text,
		TextUtilities::ConvertEntitiesToTextTags(resolved.entities) };
}
} // namespace

// messages via sendFiles(::Photo) — a local image as a compressed photo.
QJsonObject Server::toolSendPhoto(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto path = args["file_path"].toString();
	if (chatId == 0) return toolError("chat_id is required and must be non-zero");
	if (path.isEmpty()) return toolError("file_path is required");
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	const auto scheduleDate = TimeId(
		args.value("schedule_date").toVariant().toLongLong());
	return sendPreparedFile(
		chatId, path, ResolveCaptionTags(args), replyToId, silent,
		scheduleDate, SendMediaType::Photo, "photo");
}

// messages via sendFiles(::Photo) — a local .gif / short .mp4 as a looping
// animation (the client renders the natural-media path as a gif).
QJsonObject Server::toolSendGif(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto path = args["file_path"].toString();
	if (chatId == 0) return toolError("chat_id is required and must be non-zero");
	if (path.isEmpty()) return toolError("file_path is required");
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	const auto scheduleDate = TimeId(
		args.value("schedule_date").toVariant().toLongLong());
	return sendPreparedFile(
		chatId, path, ResolveCaptionTags(args), replyToId, silent,
		scheduleDate, SendMediaType::Photo, "gif");
}

// messages via sendFiles(::File) — a local audio file as a playable music
// message (the client derives duration/performer/title and attaches the audio
// attribute so it plays inline rather than downloading as a bare file).
QJsonObject Server::toolSendAudio(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto path = args["file_path"].toString();
	if (chatId == 0) return toolError("chat_id is required and must be non-zero");
	if (path.isEmpty()) return toolError("file_path is required");
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	const auto scheduleDate = TimeId(
		args.value("schedule_date").toVariant().toLongLong());
	return sendPreparedFile(
		chatId, path, ResolveCaptionTags(args), replyToId, silent,
		scheduleDate, SendMediaType::File, "audio");
}

// ===== POLLS =====
// messages.sendMedia(inputMediaPoll) — create a regular poll or a quiz. Answers
// are inputPollAnswer (the server assigns option bytes); a quiz carries the
// correct option index in correct_answers. Reuses sendInputMedia for the send.
QJsonObject Server::toolSendPoll(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto question = args["question"].toString();
	const auto optionsArr = args["options"].toArray();
	if (question.isEmpty()) return toolError("question is required");
	if (optionsArr.size() < 2) return toolError("at least 2 options are required");
	const auto multipleChoice = args.value("multiple_choice").toBool(false);
	const auto quiz = args.value("quiz").toBool(false);
	const auto publicVoters = args.value("public_voters").toBool(false);
	const auto correctOption = int(args.value("correct_option").toVariant().toLongLong());
	const auto closePeriod = int(args.value("close_period").toVariant().toLongLong());
	const auto solution = args.value("solution").toString();
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	if (quiz && !args.contains("correct_option")) {
		return toolError("quiz polls require correct_option (0-based index)");
	}
	if (quiz && (correctOption < 0 || correctOption >= optionsArr.size())) {
		return toolError("correct_option is out of range for the given options");
	}

	auto answers = QVector<MTPPollAnswer>();
	answers.reserve(optionsArr.size());
	for (const auto &opt : optionsArr) {
		answers.push_back(MTP_inputPollAnswer(
			MTP_flags(0),
			MTP_textWithEntities(
				MTP_string(opt.toString()),
				MTP_vector<MTPMessageEntity>()),
			MTPInputMedia())); // no per-answer media
	}

	using PFlag = MTPDpoll::Flag;
	const auto pflags = PFlag(0)
		| (multipleChoice ? PFlag::f_multiple_choice : PFlag(0))
		| (quiz ? PFlag::f_quiz : PFlag(0))
		| (publicVoters ? PFlag::f_public_voters : PFlag(0))
		| (closePeriod > 0 ? PFlag::f_close_period : PFlag(0));
	auto poll = MTP_poll(
		MTP_long(0), // id: server assigns for a new poll
		MTP_flags(pflags),
		MTP_textWithEntities(
			MTP_string(question),
			MTP_vector<MTPMessageEntity>()),
		MTP_vector<MTPPollAnswer>(answers),
		MTP_int(closePeriod),
		MTP_int(0), // close_date
		MTP_vector<MTPstring>(), // countries_iso2
		MTP_long(0)); // hash

	using IFlag = MTPDinputMediaPoll::Flag;
	auto iflags = MTPDinputMediaPoll::Flags();
	auto correct = QVector<MTPint>();
	if (quiz) {
		iflags |= IFlag::f_correct_answers;
		correct.push_back(MTP_int(correctOption));
	}
	if (!solution.isEmpty()) {
		iflags |= IFlag::f_solution;
	}
	auto media = MTP_inputMediaPoll(
		MTP_flags(iflags),
		poll,
		MTP_vector<MTPint>(correct),
		MTPInputMedia(), // attached_media
		MTP_string(solution),
		MTP_vector<MTPMessageEntity>(), // solution_entities
		MTPInputMedia()); // solution_media
	return sendInputMedia(chatId, media, replyToId, silent, "poll");
}

// messages.sendVote — vote on an existing poll. The caller passes 0-based answer
// indices; we read the message's poll to map them to the server's real option
// bytes and reuse the client's own Polls::sendVotes so the UI updates too.
QJsonObject Server::toolVotePoll(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto msgId = args["message_id"].toVariant().toLongLong();
	const auto indices = args["option_indices"].toArray();
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError("Chat not found");
	const auto fullId = FullMsgId(peer->id, MsgId(int(msgId)));
	const auto item = _session->data().message(fullId);
	if (!item) return toolError("Message not loaded; open the chat/history first");
	const auto media = item->media();
	const auto poll = media ? media->poll() : nullptr;
	if (!poll) return toolError("Message has no poll");
	if (indices.isEmpty()) return toolError("option_indices (array) is required");

	auto options = std::vector<QByteArray>();
	for (const auto &v : indices) {
		const auto idx = int(v.toVariant().toLongLong());
		if (idx < 0 || idx >= int(poll->answers.size())) {
			return toolError(QString("option index %1 out of range (poll has %2 answers)")
				.arg(idx).arg(poll->answers.size()));
		}
		options.push_back(poll->answers[idx].option);
	}
	_session->api().polls().sendVotes(fullId, options);
	return QJsonObject{
		{"success", true},
		{"chat_id", chatId},
		{"message_id", msgId},
		{"voted_options", int(options.size())},
		{"status", "Vote sent"},
	};
}

// messages.getPollResults — refresh a poll's tallies from the server, apply the
// updates, then return the current per-answer vote counts from local data.
QJsonObject Server::toolGetPollResults(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto msgId = args["message_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError("Chat not found");
	const auto fullId = FullMsgId(peer->id, MsgId(int(msgId)));
	const auto item = _session->data().message(fullId);
	if (!item) return toolError("Message not loaded; open the chat/history first");
	const auto media0 = item->media();
	const auto poll0 = media0 ? media0->poll() : nullptr;
	if (!poll0) return toolError("Message has no poll");
	const auto pollHash = qint64(poll0->hash);

	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_GetPollResults(
			peer->input(),
			MTP_int(int(msgId)),
			MTP_long(pollHash)
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			const auto item2 = _session->data().message(fullId);
			const auto media2 = item2 ? item2->media() : nullptr;
			const auto poll2 = media2 ? media2->poll() : nullptr;
			QJsonObject o{
				{"success", true},
				{"chat_id", chatId},
				{"message_id", msgId},
			};
			if (poll2) {
				o["total_voters"] = poll2->totalVoters;
				o["closed"] = poll2->closed();
				o["quiz"] = poll2->quiz();
				QJsonArray arr;
				for (auto i = 0; i != int(poll2->answers.size()); ++i) {
					const auto &a = poll2->answers[i];
					arr.append(QJsonObject{
						{"index", i},
						{"text", a.text.text},
						{"votes", a.votes},
						{"chosen", a.chosen},
						{"correct", a.correct},
					});
				}
				o["answers"] = arr;
			}
			done(o);
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// ===== NOTIFICATIONS + DRAFTS =====
// account.updateNotifySettings — mute or unmute a chat. mute=true sets a
// far-future mute_until (effectively forever); mute=false clears it; an explicit
// mute_until (unix seconds) overrides both.
QJsonObject Server::toolMuteChat(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError("Chat not found");
	const auto mute = args.value("mute").toBool(true);
	const auto muteUntil = args.contains("mute_until")
		? int(args.value("mute_until").toVariant().toLongLong())
		: (mute ? 2147483647 : 0); // 2147483647 = year 2038, effectively forever
	using Flag = MTPDinputPeerNotifySettings::Flag;
	auto settings = MTP_inputPeerNotifySettings(
		MTP_flags(Flag::f_mute_until),
		MTPBool(), // show_previews
		MTPBool(), // silent
		MTP_int(muteUntil),
		MTPNotificationSound(),
		MTPBool(), // stories_muted
		MTPBool(), // stories_hide_sender
		MTPNotificationSound());
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPaccount_UpdateNotifySettings(
			MTP_inputNotifyPeer(peer->input()),
			settings
		)).done([=](const MTPBool &result) {
			done(QJsonObject{
				{"success", true},
				{"chat_id", chatId},
				{"muted", muteUntil > int(QDateTime::currentSecsSinceEpoch())},
				{"mute_until", muteUntil},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// account.getNotifySettings — read a chat's mute state / preview / silent flags.
QJsonObject Server::toolGetChatNotifySettings(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError("Chat not found");
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPaccount_GetNotifySettings(
			MTP_inputNotifyPeer(peer->input())
		)).done([=](const MTPPeerNotifySettings &result) {
			result.match([&](const MTPDpeerNotifySettings &d) {
				const auto muteUntil = d.vmute_until()
					? int(d.vmute_until()->v)
					: 0;
				QJsonObject o{
					{"success", true},
					{"chat_id", chatId},
					{"mute_until", muteUntil},
					{"muted", muteUntil > int(QDateTime::currentSecsSinceEpoch())},
				};
				if (d.vsilent()) {
					o["silent"] = mtpIsTrue(*d.vsilent());
				}
				if (d.vshow_previews()) {
					o["show_previews"] = mtpIsTrue(*d.vshow_previews());
				}
				done(o);
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// messages.saveDraft — stage an unsent text draft in a chat (optionally a reply).
QJsonObject Server::toolSaveDraft(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto text = args.value("text").toString();
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError("Chat not found");
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto noWebpage = args.value("no_webpage").toBool(false);
	using Flag = MTPmessages_saveDraft::Flag;
	const auto flags = Flag(0)
		| (noWebpage ? Flag::f_no_webpage : Flag(0))
		| (replyToId ? Flag::f_reply_to : Flag(0));
	auto replyTo = replyToId
		? MTP_inputReplyToMessage(
			MTP_flags(0), MTP_int(int(replyToId)), MTPint(), MTPInputPeer(),
			MTPstring(), MTPVector<MTPMessageEntity>(), MTPint(),
			MTPInputPeer(), MTPint(), MTPbytes())
		: MTPInputReplyTo();
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_SaveDraft(
			MTP_flags(flags),
			replyTo,
			peer->input(),
			MTP_string(text),
			MTP_vector<MTPMessageEntity>(), // entities
			MTPInputMedia(), // media
			MTPlong(), // effect
			MTPSuggestedPost(), // suggested_post
			MTPInputRichMessage() // rich_message
		)).done([=](const MTPBool &result) {
			done(QJsonObject{
				{"success", true},
				{"chat_id", chatId},
				{"status", "Draft saved"},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// messages.clearAllDrafts — remove every cloud draft across all chats.
QJsonObject Server::toolClearAllDrafts(const QJsonObject &args) {
	Q_UNUSED(args);
	if (!_session) return toolError("No active session");
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_ClearAllDrafts(
		)).done([=](const MTPBool &result) {
			done(QJsonObject{
				{"success", true},
				{"status", "All cloud drafts cleared"},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// ===== CONTACTS =====
// contacts.getContacts — list the account's saved contacts (id + mutual + name).
QJsonObject Server::toolGetContacts(const QJsonObject &args) {
	Q_UNUSED(args);
	if (!_session) return toolError("No active session");
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPcontacts_GetContacts(
			MTP_long(0) // hash: 0 = always return the full list
		)).done([=](const MTPcontacts_Contacts &result) {
			result.match([&](const MTPDcontacts_contacts &d) {
				_session->data().processUsers(d.vusers());
				QJsonArray arr;
				for (const auto &c : d.vcontacts().v) {
					c.match([&](const MTPDcontact &ct) {
						QJsonObject o{
							{"user_id", qint64(ct.vuser_id().v)},
							{"mutual", mtpIsTrue(ct.vmutual())},
						};
						if (const auto peer = _session->data().peerLoaded(
								peerFromUser(ct.vuser_id()))) {
							o["name"] = peer->name();
						}
						arr.append(o);
					});
				}
				done(QJsonObject{
					{"success", true},
					{"saved_count", d.vsaved_count().v},
					{"count", int(arr.size())},
					{"contacts", arr},
				});
			}, [&](const MTPDcontacts_contactsNotModified &) {
				done(QJsonObject{{"success", true}, {"not_modified", true}});
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// contacts.addContact — add an already-known user to the saved contacts.
QJsonObject Server::toolAddContact(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto userId = args["user_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(userId);
	const auto user = peer ? peer->asUser() : nullptr;
	if (!user) return toolError("User not loaded; open it once or resolve it first");
	const auto first = args.value("first_name").toString();
	const auto last = args.value("last_name").toString();
	const auto phone = args.value("phone").toString();
	const auto sharePhone = args.value("share_phone").toBool(false);
	if (first.isEmpty()) return toolError("first_name is required");
	using Flag = MTPcontacts_addContact::Flag;
	const auto flags = sharePhone
		? Flag::f_add_phone_privacy_exception
		: Flag(0);
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPcontacts_AddContact(
			MTP_flags(flags),
			user->inputUser(),
			MTP_string(first),
			MTP_string(last),
			MTP_string(phone),
			MTPTextWithEntities() // note (flag not set)
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			done(QJsonObject{
				{"success", true},
				{"user_id", userId},
				{"status", "Contact added"},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// contacts.deleteContacts — remove a user from the saved contacts.
QJsonObject Server::toolDeleteContact(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto userId = args["user_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(userId);
	const auto user = peer ? peer->asUser() : nullptr;
	if (!user) return toolError("User not loaded; open it once or resolve it first");
	auto ids = QVector<MTPInputUser>{ user->inputUser() };
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPcontacts_DeleteContacts(
			MTP_vector<MTPInputUser>(ids)
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			done(QJsonObject{
				{"success", true},
				{"user_id", userId},
				{"status", "Contact deleted"},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// contacts.importContacts — add a contact by phone number (resolves to a user
// if the number is on Telegram and reachable under the target's privacy).
QJsonObject Server::toolImportContacts(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto phone = args["phone"].toString();
	const auto first = args.value("first_name").toString();
	const auto last = args.value("last_name").toString();
	if (phone.isEmpty() || first.isEmpty()) {
		return toolError("phone and first_name are required");
	}
	auto contacts = QVector<MTPInputContact>{
		MTP_inputPhoneContact(
			MTP_flags(0),
			MTP_long(0), // client_id
			MTP_string(phone),
			MTP_string(first),
			MTP_string(last),
			MTPTextWithEntities()) // note (flag not set)
	};
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPcontacts_ImportContacts(
			MTP_vector<MTPInputContact>(contacts)
		)).done([=](const MTPcontacts_ImportedContacts &result) {
			result.match([&](const MTPDcontacts_importedContacts &d) {
				_session->data().processUsers(d.vusers());
				QJsonArray imported;
				for (const auto &im : d.vimported().v) {
					im.match([&](const MTPDimportedContact &ic) {
						imported.append(qint64(ic.vuser_id().v));
					});
				}
				done(QJsonObject{
					{"success", true},
					{"phone", phone},
					{"imported_count", int(imported.size())},
					{"imported_user_ids", imported},
				});
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// ===== MESSAGE INTEL (comment threads + read receipts) =====
// messages.getDiscussionMessage — resolve a channel post to its linked comment
// thread: the discussion group's peer id and the thread's top message id, so a
// caller can read/reply to the comments there.
QJsonObject Server::toolGetDiscussionMessage(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto msgId = args["message_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError("Chat not found");
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_GetDiscussionMessage(
			peer->input(),
			MTP_int(int(msgId))
		)).done([=](const MTPmessages_DiscussionMessage &result) {
			result.match([&](const MTPDmessages_discussionMessage &d) {
				_session->data().processChats(d.vchats());
				_session->data().processUsers(d.vusers());
				QJsonObject o{
					{"success", true},
					{"chat_id", chatId},
					{"message_id", msgId},
					{"unread_count", d.vunread_count().v},
				};
				if (d.vmax_id()) o["max_id"] = d.vmax_id()->v;
				if (d.vread_inbox_max_id()) {
					o["read_inbox_max_id"] = d.vread_inbox_max_id()->v;
				}
				QJsonArray thread;
				for (const auto &m : d.vmessages().v) {
					m.match([&](const MTPDmessage &mm) {
						thread.append(QJsonObject{
							{"discussion_peer_id",
								qint64(peerFromMTP(mm.vpeer_id()).value)},
							{"top_message_id", mm.vid().v},
						});
					}, [](const auto &) {});
				}
				o["thread"] = thread;
				done(o);
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// messages.getMessageReadParticipants — who has read a message (small groups /
// channels where read receipts are available), with the read timestamp.
QJsonObject Server::toolGetMessageReadParticipants(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto msgId = args["message_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError("Chat not found");
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_GetMessageReadParticipants(
			peer->input(),
			MTP_int(int(msgId))
		)).done([=](const MTPVector<MTPReadParticipantDate> &result) {
			QJsonArray arr;
			for (const auto &p : result.v) {
				p.match([&](const MTPDreadParticipantDate &rp) {
					arr.append(QJsonObject{
						{"user_id", qint64(rp.vuser_id().v)},
						{"date", qint64(rp.vdate().v)},
					});
				});
			}
			done(QJsonObject{
				{"success", true},
				{"chat_id", chatId},
				{"message_id", msgId},
				{"count", int(arr.size())},
				{"participants", arr},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// ===== MODERATION =====
// channels.editBanned — kick/ban a member (view_messages restricted, permanent)
// or lift the ban (empty rights). chat_id must be a channel/supergroup.
QJsonObject Server::toolBanChatMember(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto userId = args["user_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(chatId);
	const auto channel = peer ? peer->asChannel() : nullptr;
	if (!channel) return toolError("chat_id must be a channel or supergroup");
	const auto target = resolvePeer(userId);
	if (!target) return toolError("user_id not loaded; open it once or resolve it first");
	using BFlag = MTPDchatBannedRights::Flag;
	auto rights = MTP_chatBannedRights(
		MTP_flags(BFlag::f_view_messages), // fully removed
		MTP_int(0)); // until_date: 0 = permanent
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPchannels_EditBanned(
			channel->inputChannel(),
			target->input(),
			rights
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			done(QJsonObject{
				{"success", true},
				{"chat_id", chatId},
				{"user_id", userId},
				{"status", "Member banned"},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// channels.editBanned with empty rights — lift a member's ban/restrictions.
QJsonObject Server::toolUnbanChatMember(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto userId = args["user_id"].toVariant().toLongLong();
	const auto peer = resolvePeer(chatId);
	const auto channel = peer ? peer->asChannel() : nullptr;
	if (!channel) return toolError("chat_id must be a channel or supergroup");
	const auto target = resolvePeer(userId);
	if (!target) return toolError("user_id not loaded; open it once or resolve it first");
	auto rights = MTP_chatBannedRights(
		MTP_flags(MTPDchatBannedRights::Flags(0)), // no restrictions = unbanned
		MTP_int(0));
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPchannels_EditBanned(
			channel->inputChannel(),
			target->input(),
			rights
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			done(QJsonObject{
				{"success", true},
				{"chat_id", chatId},
				{"user_id", userId},
				{"status", "Member unbanned"},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// ===== DRIVE OTHER BOTS =====
// messages.startBot — /start a bot (optionally in a group) with a start param.
// (Named send_bot_start to avoid the existing local bot-manager's start_bot.)
QJsonObject Server::toolSendBotStart(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto botId = args["bot_id"].toVariant().toLongLong();
	const auto chatId = args.value("chat_id").toVariant().toLongLong();
	const auto startParam = args.value("start_param").toString();
	const auto botPeer = resolvePeer(botId);
	const auto bot = botPeer ? botPeer->asUser() : nullptr;
	if (!bot) return toolError("bot_id not loaded; open it once or resolve it first");
	const auto peer = chatId ? resolvePeer(chatId) : botPeer;
	if (!peer) return toolError("chat_id not found");
	const auto randomId = base::RandomValue<uint64>();
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_StartBot(
			bot->inputUser(),
			peer->input(),
			MTP_long(randomId),
			MTP_string(startParam)
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			done(QJsonObject{
				{"success", true},
				{"bot_id", botId},
				{"status", "Bot started"},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// messages.getBotCallbackAnswer — press an inline-keyboard button. `data` is the
// button's callback payload (base64 of the keyboardButtonCallback bytes). Returns
// the bot's answer text / alert / url.
QJsonObject Server::toolGetBotCallbackAnswer(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto msgId = args["message_id"].toVariant().toLongLong();
	const auto data = QByteArray::fromBase64(
		args.value("data").toString().toUtf8());
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError("Chat not found");
	using Flag = MTPmessages_getBotCallbackAnswer::Flag;
	const auto flags = data.isEmpty() ? Flag(0) : Flag::f_data;
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_GetBotCallbackAnswer(
			MTP_flags(flags),
			peer->input(),
			MTP_int(int(msgId)),
			MTP_bytes(data),
			MTPInputCheckPasswordSRP()
		)).done([=](const MTPmessages_BotCallbackAnswer &result) {
			result.match([&](const MTPDmessages_botCallbackAnswer &d) {
				QJsonObject o{
					{"success", true},
					{"chat_id", chatId},
					{"message_id", msgId},
					{"alert", d.is_alert()},
				};
				if (d.vmessage()) o["message"] = qs(*d.vmessage());
				if (d.vurl()) o["url"] = qs(*d.vurl());
				done(o);
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// messages.getInlineBotResults — query an inline bot (e.g. @gif cat). Returns a
// query_id and each result's id + type (feed one to send_inline_bot_result).
QJsonObject Server::toolGetInlineBotResults(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto botId = args["bot_id"].toVariant().toLongLong();
	const auto chatId = args.value("chat_id").toVariant().toLongLong();
	const auto query = args.value("query").toString();
	const auto offset = args.value("offset").toString();
	const auto botPeer = resolvePeer(botId);
	const auto bot = botPeer ? botPeer->asUser() : nullptr;
	if (!bot) return toolError("bot_id not loaded; open it once or resolve it first");
	const auto peer = chatId ? resolvePeer(chatId) : botPeer;
	if (!peer) return toolError("chat_id not found");
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_GetInlineBotResults(
			MTP_flags(0),
			bot->inputUser(),
			peer->input(),
			MTPInputGeoPoint(), // no location
			MTP_string(query),
			MTP_string(offset)
		)).done([=](const MTPmessages_BotResults &result) {
			result.match([&](const MTPDmessages_botResults &d) {
				_session->data().processUsers(d.vusers());
				QJsonArray arr;
				const auto add = [&](const MTPstring &id, const MTPstring &type,
						tl::conditional<MTPstring> title) {
					QJsonObject o{{"id", qs(id)}, {"type", qs(type)}};
					if (title) o["title"] = qs(*title);
					arr.append(o);
				};
				for (const auto &r : d.vresults().v) {
					r.match([&](const MTPDbotInlineResult &br) {
						add(br.vid(), br.vtype(), br.vtitle());
					}, [&](const MTPDbotInlineMediaResult &br) {
						add(br.vid(), br.vtype(), br.vtitle());
					});
				}
				QJsonObject o{
					{"success", true},
					{"query_id", qint64(d.vquery_id().v)},
					{"count", int(arr.size())},
					{"results", arr},
				};
				if (d.vnext_offset()) o["next_offset"] = qs(*d.vnext_offset());
				done(o);
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// messages.sendInlineBotResult — post a chosen inline result (from
// get_inline_bot_results) into a chat.
QJsonObject Server::toolSendInlineBotResult(const QJsonObject &args) {
	if (!_session) return toolError("No active session");
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto queryId = args["query_id"].toVariant().toLongLong();
	const auto resultId = args["result_id"].toString();
	const auto replyToId = args.value("reply_to_message_id").toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	const auto peer = resolvePeer(chatId);
	if (!peer) return toolError("Chat not found");
	using Flag = MTPmessages_sendInlineBotResult::Flag;
	const auto flags = Flag(0)
		| (silent ? Flag::f_silent : Flag(0))
		| (replyToId ? Flag::f_reply_to : Flag(0));
	auto replyTo = replyToId
		? MTP_inputReplyToMessage(
			MTP_flags(0), MTP_int(int(replyToId)), MTPint(), MTPInputPeer(),
			MTPstring(), MTPVector<MTPMessageEntity>(), MTPint(),
			MTPInputPeer(), MTPint(), MTPbytes())
		: MTPInputReplyTo();
	const auto randomId = base::RandomValue<uint64>();
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPmessages_SendInlineBotResult(
			MTP_flags(flags),
			peer->input(),
			replyTo,
			MTP_long(randomId),
			MTP_long(queryId),
			MTP_string(resultId),
			MTPint(), // schedule_date
			MTPInputPeer(), // send_as
			MTPInputQuickReplyShortcut(),
			MTPlong() // allow_paid_stars
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			done(QJsonObject{
				{"success", true},
				{"chat_id", chatId},
				{"status", "Inline result sent"},
			});
		}).fail([=](const MTP::Error &error) { fail(error.type()); }).send();
	});
}

// Send a structured rich-article message (Instant-View-style blocks) to a chat
// over the client's own rich-compose path: build a RichPage from the caller's
// blocks (BuildRichPage), serialize it to an MTPInputRichMessage exactly as the
// editor does on submit (SerializeInputRichMessage, FinalSubmit), then hand it
// to ApiWrap::sendRichMessage — which creates the local item and performs the
// messages.sendMessage(f_rich_message) call. The caller supplies plain block
// JSON and never sees RichPage, block normalization, or MTProto serialization.
// Async like every send path: success is "queued", not delivered.
QJsonObject Server::toolSendRichMessage(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto blocks = args["blocks"].toArray();
	if (chatId == 0) {
		return toolError("chat_id is required and must be non-zero");
	}
	if (blocks.isEmpty()) {
		return toolError("blocks is required and must be a non-empty array");
	}
	if (!_session) {
		return toolError("Session not available");
	}
	const auto peer = PeerId(chatId);
	if (!resolvePeer(chatId)) {
		return toolError("Chat not found");
	}
	const auto history = _session->data().history(peer);

	const auto parseMode = args.value("parse_mode").toString();
	const auto replyToId = args.value("reply_to_message_id")
		.toVariant().toLongLong();
	const auto silent = args.value("silent").toBool(false);
	const auto scheduleDate = TimeId(
		args.value("schedule_date").toVariant().toLongLong());

	auto error = QString();
	auto page = BuildRichPage(blocks, parseMode, &error);
	if (!page) {
		return toolError(error);
	}
	auto serialized = Iv::SerializeInputRichMessage(
		_session,
		*page,
		Iv::SerializeInputRichMessageMode::FinalSubmit);
	using Status = Iv::SerializeInputRichMessageStatus;
	if (serialized.status == Status::EmptyContent) {
		return toolError(
			"the blocks serialized to empty content — nothing to send");
	} else if (serialized.status != Status::Success || !serialized.value) {
		return toolError(
			"could not serialize the rich message (a block was rejected)");
	}
	auto action = BuildSendAction(
		history, peer, replyToId, silent, scheduleDate);
	_session->api().sendRichMessage(page, *serialized.value, std::move(action));

	QJsonObject result;
	result["success"] = true;
	result["chat_id"] = chatId;
	result["blocks_count"] = int(page->blocks.size());
	result["status"] = "Rich message queued for sending";
	qInfo() << "MCP: Queued rich message with" << page->blocks.size()
		<< "blocks to chat" << chatId;
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
						msg["message_id"] = qint64(item->id.bare);
						msg["date"] = static_cast<qint64>(item->date());
						msg["text"] = text.text;

						// Get sender info
						auto from = item->from();
						if (from) {
							QJsonObject fromUser;
							fromUser["id"] = qint64(from->id.value);
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
			userInfo["user_id"] = qint64(userId);
			return userInfo;
		}

		// Get the user data
		auto user = peer->asUser();
		if (!user) {
			qWarning() << "MCP: Peer" << userId << "is not a user";
			userInfo["error"] = "Specified ID is not a user";
			userInfo["user_id"] = qint64(userId);
			return userInfo;
		}

		// Extract user information
		userInfo["id"] = qint64(user->id.value);
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
	userInfo["user_id"] = qint64(userId);
	userInfo["error"] = "User info not available (session not active)";
	userInfo["source"] = "error";

	return userInfo;
}


} // namespace MCP
