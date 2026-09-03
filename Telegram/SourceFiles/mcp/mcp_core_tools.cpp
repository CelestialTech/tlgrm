// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"
#include "ui/text/text_entity.h"
#include "iv/iv_rich_page.h"
#include "iv/iv_rich_message_serializer.h"

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
				total = int(d.vmessages().v.size()); // small chat: fully returned
				list = &d.vmessages().v;
			}, [&](const MTPDmessages_messagesSlice &d) {
				total = d.vcount().v;                // the chat's REAL total
				list = &d.vmessages().v;
			}, [&](const MTPDmessages_channelMessages &d) {
				total = d.vcount().v;                // the chat's REAL total
				list = &d.vmessages().v;
			}, [&](const MTPDmessages_messagesNotModified &) {
			});

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
