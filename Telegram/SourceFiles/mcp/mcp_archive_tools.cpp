// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== ARCHIVE TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolArchiveChat(const QJsonObject &args) {
	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(1000);

	bool success = _archiver->archiveChat(chatId, limit);

	QJsonObject result;
	result["success"] = success;
	result["chat_id"] = chatId;
	result["requested_limit"] = limit;

	if (!success) {
		result["error"] = "Failed to archive chat";
	}

	return result;
}

QJsonObject Server::toolExportChat(const QJsonObject &args) {
	// Direct export using messages.getHistory - no Takeout/Export::Controller.
	// Returns immediately; use get_export_status to poll for completion.

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString outputPath = args["output_path"].toString();

	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	if (_activeExport && !_activeExport->finished) {
		QJsonObject error;
		error["error"] = "Another export is already in progress";
		error["chat_id"] = _activeExport->chatId;
		error["chat_name"] = _activeExport->chatName;
		return error;
	}

	// Resolve peer
	PeerId peerId(chatId);
	auto peer = _session->data().peerLoaded(peerId);
	if (!peer) {
		auto history = _session->data().history(peerId);
		if (history) {
			peer = history->peer;
		}
	}
	if (!peer) {
		QJsonObject error;
		error["error"] = "Chat not found: " + QString::number(chatId);
		return error;
	}

	// Determine peer type
	QString peerType;
	if (peer->isChannel()) {
		peerType = peer->asChannel()->isBroadcast() ? "Channel" : "Group";
	} else if (peer->isChat()) {
		peerType = "Group";
	} else {
		peerType = "Chat";
	}

	QString peerName = peer->name();

	if (outputPath.isEmpty()) {
		outputPath = File::DefaultDownloadPath(
			gsl::make_not_null(_session));
	}

	QString resolvedPath = createExportDirectory(outputPath, peerType, peerName);

	// Initialize active export tracking
	_activeExport = std::make_unique<ActiveExport>();
	_activeExport->chatId = chatId;
	_activeExport->chatName = peerName;
	_activeExport->chatType = peerType;
	_activeExport->outputPath = outputPath;
	_activeExport->resolvedPath = resolvedPath;
	_activeExport->exportPeerId = peerId;
	_activeExport->startTime = QDateTime::currentDateTime();

	qWarning() << "MCP: toolExportChat starting direct export for"
	           << peerName << "(" << peerType << ") to" << resolvedPath;

	// Start async message fetch chain
	startDirectExport();

	QJsonObject result;
	result["success"] = true;
	result["status"] = "started";
	result["chat_id"] = chatId;
	result["chat_name"] = peerName;
	result["chat_type"] = peerType;
	result["output_path"] = resolvedPath;
	result["message"] = "Export started. Use get_export_status to poll for completion.";

	return result;
}

QJsonObject Server::toolGetExportStatus(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_activeExport) {
		QJsonObject result;
		result["success"] = true;
		result["state"] = "idle";
		result["message"] = "No export in progress";
		return result;
	}

	QJsonObject result;
	result["success"] = true;
	result["chat_id"] = _activeExport->chatId;
	result["chat_name"] = _activeExport->chatName;
	result["chat_type"] = _activeExport->chatType;
	result["output_path"] = _activeExport->outputPath;

	if (_activeExport->finished) {
		result["state"] = _activeExport->success ? "completed" : "failed";
		result["export_success"] = _activeExport->success;

		if (_activeExport->success) {
			result["output_directory"] = _activeExport->finishedPath;
			result["files_count"] = _activeExport->filesCount;
			result["bytes_count"] = _activeExport->bytesCount;
			result["messages_exported"] = _activeExport->totalMessagesFetched;
		} else {
			result["error"] = _activeExport->errorMessage.isEmpty()
				? "Export failed" : _activeExport->errorMessage;
		}

		// Calculate duration
		qint64 durationSecs = _activeExport->startTime.secsTo(QDateTime::currentDateTime());
		result["duration_seconds"] = durationSecs;
	} else {
		result["state"] = "in_progress";
		result["current_step"] = _activeExport->currentStep;
		result["messages_fetched"] = _activeExport->totalMessagesFetched;
		result["batches_fetched"] = _activeExport->batchesFetched;

		if (_activeExport->downloadingMedia) {
			result["phase"] = "downloading_media";
			result["media_total"] = _activeExport->mediaItems.size();
			result["media_downloaded"] = _activeExport->mediaDownloaded;
			result["media_failed"] = _activeExport->mediaFailed;
			result["media_current"] = _activeExport->currentMediaIndex;
			result["media_total_bytes"] = _activeExport->totalMediaBytes;
			result["media_downloaded_bytes"] = _activeExport->mediaDownloadedBytes;

			// ETA calculation based on download speed
			qint64 mediaElapsed = _activeExport->mediaPhaseStartTime.secsTo(
				QDateTime::currentDateTime());
			if (mediaElapsed > 0 && _activeExport->mediaDownloadedBytes > 0) {
				double bytesPerSec = double(_activeExport->mediaDownloadedBytes) / mediaElapsed;
				result["download_speed_bps"] = qint64(bytesPerSec);
				int64_t remainingBytes = _activeExport->totalMediaBytes
					- _activeExport->mediaDownloadedBytes;
				if (remainingBytes > 0 && bytesPerSec > 0) {
					result["estimated_seconds_remaining"] = qint64(remainingBytes / bytesPerSec);
				} else {
					result["estimated_seconds_remaining"] = 0;
				}
			}
		} else {
			result["phase"] = "fetching_messages";
		}

		// Elapsed time
		qint64 elapsedSecs = _activeExport->startTime.secsTo(QDateTime::currentDateTime());
		result["elapsed_seconds"] = elapsedSecs;
	}

	return result;
}

QJsonObject Server::toolListArchivedChats(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	QJsonArray chats = _archiver->listArchivedChats();

	QJsonObject result;
	result["chats"] = chats;
	result["count"] = chats.size();

	return result;
}

QJsonObject Server::toolGetArchiveStats(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	auto stats = _archiver->getStats();

	QJsonObject result;
	result["total_messages"] = stats.totalMessages;
	result["total_chats"] = stats.totalChats;
	result["total_users"] = stats.totalUsers;
	result["ephemeral_captured"] = stats.ephemeralCaptured;
	result["media_downloaded"] = stats.mediaDownloaded;
	result["database_size_bytes"] = stats.databaseSize;
	result["last_archived"] = stats.lastArchived.toString(Qt::ISODate);
	result["success"] = true;

	return result;
}

QJsonObject Server::toolGetEphemeralMessages(const QJsonObject &args) {
	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	QString type = args.value("type").toString();  // "self_destruct", "view_once", "vanishing", or empty for all
	int limit = args.value("limit").toInt(50);

	// Query ephemeral messages from database
	QSqlQuery query(_archiver->database());

	QString sql;
	if (chatId > 0 && !type.isEmpty()) {
		sql = QString("SELECT message_id, chat_id, from_user_id, text, date, ephemeral_type, ttl "
			"FROM messages WHERE chat_id = ? AND ephemeral_type = ? "
			"ORDER BY date DESC LIMIT ?");
		query.prepare(sql);
		query.addBindValue(chatId);
		query.addBindValue(type);
		query.addBindValue(limit);
	} else if (chatId > 0) {
		sql = QString("SELECT message_id, chat_id, from_user_id, text, date, ephemeral_type, ttl "
			"FROM messages WHERE chat_id = ? AND ephemeral_type IS NOT NULL "
			"ORDER BY date DESC LIMIT ?");
		query.prepare(sql);
		query.addBindValue(chatId);
		query.addBindValue(limit);
	} else if (!type.isEmpty()) {
		sql = QString("SELECT message_id, chat_id, from_user_id, text, date, ephemeral_type, ttl "
			"FROM messages WHERE ephemeral_type = ? "
			"ORDER BY date DESC LIMIT ?");
		query.prepare(sql);
		query.addBindValue(type);
		query.addBindValue(limit);
	} else {
		sql = QString("SELECT message_id, chat_id, from_user_id, text, date, ephemeral_type, ttl "
			"FROM messages WHERE ephemeral_type IS NOT NULL "
			"ORDER BY date DESC LIMIT ?");
		query.prepare(sql);
		query.addBindValue(limit);
	}

	QJsonArray messages;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject msg;
			msg["message_id"] = query.value(0).toLongLong();
			msg["chat_id"] = query.value(1).toLongLong();
			msg["from_user_id"] = query.value(2).toLongLong();
			msg["text"] = query.value(3).toString();
			msg["date"] = query.value(4).toLongLong();
			msg["ephemeral_type"] = query.value(5).toString();
			msg["ttl_seconds"] = query.value(6).toInt();
			messages.append(msg);
		}
	}

	QJsonObject result;
	result["messages"] = messages;
	result["count"] = messages.size();
	result["success"] = true;

	if (!type.isEmpty()) {
		result["type"] = type;
	}
	if (chatId > 0) {
		result["chat_id"] = chatId;
	}

	return result;
}

QJsonObject Server::toolSearchArchive(const QJsonObject &args) {
	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	QString query = args["query"].toString();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(50);

	QJsonArray results = _archiver->searchMessages(chatId, query, limit);

	QJsonObject result;
	result["results"] = results;
	result["count"] = results.size();
	result["query"] = query;

	return result;
}

QJsonObject Server::toolPurgeArchive(const QJsonObject &args) {
	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	int daysToKeep = args["days_to_keep"].toInt();

	qint64 cutoffTimestamp = QDateTime::currentSecsSinceEpoch() - (daysToKeep * 86400);
	int deleted = _archiver->purgeOldMessages(cutoffTimestamp);

	QJsonObject result;
	result["success"] = true;
	result["deleted_count"] = deleted;
	result["days_kept"] = daysToKeep;

	return result;
}


// ===== DIRECT EXPORT HELPERS (messages.getHistory based, no Takeout) =====

QString Server::sanitizeForFilename(const QString &name) {
	QString result = name;
	result.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
	result.replace(' ', '_');
	while (result.startsWith('.') || result.startsWith('_')) {
		result = result.mid(1);
	}
	while (result.endsWith('.') || result.endsWith('_')) {
		result.chop(1);
	}
	if (result.length() > 50) {
		result = result.left(50);
	}
	if (result.isEmpty()) {
		result = "export";
	}
	return result;
}

QString Server::createExportDirectory(
		const QString &basePath,
		const QString &peerType,
		const QString &peerName) {
	QString safeName = sanitizeForFilename(peerName);
	QString timestamp = QDateTime::currentDateTime().toString("ddMMyyyy-HHmmss");
	QString dirName = peerType + "-" + safeName + "-" + timestamp;

	QDir base(basePath);
	base.mkpath(dirName);

	return base.absoluteFilePath(dirName);
}

void Server::startDirectExport() {
	if (!_activeExport) return;
	_activeExport->currentStep = 0;
	qWarning() << "MCP: Starting direct message fetch for"
	           << _activeExport->chatName;
	fetchNextMessageBatch();
}

void Server::fetchNextMessageBatch() {
	if (!_activeExport || _activeExport->finished) return;
	if (!_session) {
		_activeExport->errorMessage = "Session lost during export";
		_activeExport->finished = true;
		return;
	}

	auto peer = _session->data().peerLoaded(_activeExport->exportPeerId);
	if (!peer) {
		_activeExport->errorMessage = "Peer lost during export";
		_activeExport->finished = true;
		return;
	}

	const auto offsetIdInt = int(std::clamp(
		_activeExport->nextOffsetId.bare,
		int64(0),
		int64(0x3FFFFFFF)));

	qWarning() << "MCP: Fetching batch" << (_activeExport->batchesFetched + 1)
	           << "offset_id:" << offsetIdInt
	           << "fetched so far:" << _activeExport->totalMessagesFetched;

	_session->api().request(MTPmessages_GetHistory(
		peer->input,
		MTP_int(offsetIdInt),  // offset_id
		MTP_int(0),            // offset_date
		MTP_int(0),            // add_offset
		MTP_int(100),          // limit
		MTP_int(0),            // max_id
		MTP_int(0),            // min_id
		MTP_long(0)            // hash
	)).done([this](const MTPmessages_Messages &result) {
		onMessageBatchReceived(result);
	}).fail([this](const MTP::Error &error) {
		if (!_activeExport || _activeExport->finished) return;

		if (error.type().startsWith("FLOOD_WAIT_")) {
			int waitSecs = error.type().mid(11).toInt();
			if (waitSecs < 1) waitSecs = 5;
			qWarning() << "MCP: FLOOD_WAIT, retrying in" << waitSecs << "seconds";
			QTimer::singleShot(waitSecs * 1000, [this]() {
				if (_activeExport && !_activeExport->finished) {
					fetchNextMessageBatch();
				}
			});
		} else {
			_activeExport->errorMessage = "API Error: " + error.type();
			_activeExport->finished = true;
			qWarning() << "MCP: Export failed:" << _activeExport->errorMessage;
		}
	}).send();
}

void Server::onMessageBatchReceived(const MTPmessages_Messages &result) {
	if (!_activeExport || _activeExport->finished) return;

	const QVector<MTPMessage> *messagesList = nullptr;
	bool hasMore = false;

	result.match([&](const MTPDmessages_messages &data) {
		if (_session) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
		}
		messagesList = &data.vmessages().v;
		hasMore = false;
	}, [&](const MTPDmessages_messagesSlice &data) {
		if (_session) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
		}
		messagesList = &data.vmessages().v;
		hasMore = (data.vmessages().v.size() == 100);
	}, [&](const MTPDmessages_channelMessages &data) {
		if (_session) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
		}
		messagesList = &data.vmessages().v;
		hasMore = (data.vmessages().v.size() == 100);
	}, [&](const MTPDmessages_messagesNotModified &) {
		hasMore = false;
	});

	if (!messagesList || messagesList->isEmpty()) {
		qWarning() << "MCP: All messages fetched. Total:"
		           << _activeExport->totalMessagesFetched;
		startMediaDownloadPhase();
		return;
	}

	// Process messages and track lowest ID for pagination
	MsgId lowestId = MsgId(0);
	bool firstMsg = true;

	for (const auto &message : *messagesList) {
		QJsonObject msgJson = mtpMessageToJson(message);
		if (!msgJson.isEmpty() && !msgJson.contains("empty")) {
			_activeExport->messages.append(msgJson);
		}

		// Track lowest message ID for next batch offset
		message.match([&](const MTPDmessage &data) {
			MsgId msgId = MsgId(data.vid().v);
			if (firstMsg || msgId < lowestId) {
				lowestId = msgId;
				firstMsg = false;
			}
		}, [&](const MTPDmessageService &data) {
			MsgId msgId = MsgId(data.vid().v);
			if (firstMsg || msgId < lowestId) {
				lowestId = msgId;
				firstMsg = false;
			}
		}, [&](const MTPDmessageEmpty &data) {
			MsgId msgId = MsgId(data.vid().v);
			if (firstMsg || msgId < lowestId) {
				lowestId = msgId;
				firstMsg = false;
			}
		});
	}

	_activeExport->totalMessagesFetched += messagesList->size();
	_activeExport->batchesFetched++;
	_activeExport->nextOffsetId = lowestId;
	_activeExport->currentStep = _activeExport->batchesFetched;

	qWarning() << "MCP: Batch" << _activeExport->batchesFetched
	           << "received" << messagesList->size() << "messages."
	           << "Total:" << _activeExport->totalMessagesFetched
	           << "Next offset:" << lowestId.bare;

	if (!hasMore) {
		qWarning() << "MCP: All messages fetched. Total:"
		           << _activeExport->totalMessagesFetched;
		startMediaDownloadPhase();
		return;
	}

	// Schedule next batch with random delay (1-3 seconds) to avoid flood
	int delay = 1000 + (QRandomGenerator::global()->bounded(2000));
	QTimer::singleShot(delay, [this]() {
		if (_activeExport && !_activeExport->finished) {
			fetchNextMessageBatch();
		}
	});
}

QJsonObject Server::mtpMessageToJson(const MTPMessage &message) {
	QJsonObject msg;

	message.match([&](const MTPDmessage &data) {
		msg["id"] = data.vid().v;
		msg["date"] = data.vdate().v;
		msg["text"] = qs(data.vmessage());
		msg["out"] = data.is_out();

		if (const auto fromId = data.vfrom_id()) {
			fromId->match([&](const MTPDpeerUser &p) {
				msg["from_id"] = qlonglong(p.vuser_id().v);
			}, [&](const MTPDpeerChat &p) {
				msg["from_id"] = qlonglong(p.vchat_id().v);
			}, [&](const MTPDpeerChannel &p) {
				msg["from_id"] = qlonglong(p.vchannel_id().v);
			});
			// Resolve display name
			if (_session) {
				PeerId fromPeerId = peerFromMTP(*fromId);
				auto fromPeer = _session->data().peerLoaded(fromPeerId);
				if (fromPeer) {
					msg["from_name"] = fromPeer->name();
				}
			}
		}

		if (const auto media = data.vmedia()) {
			media->match([&](const MTPDmessageMediaPhoto &photoMedia) {
				msg["media_type"] = "photo";
				if (const auto photo = photoMedia.vphoto()) {
					photo->match([&](const MTPDphoto &p) {
						msg["photo_id"] = qlonglong(p.vid().v);
						// Process photo into session's data to get PhotoData*
						if (_session) {
							_session->data().processPhoto(p);
						}
					}, [&](const MTPDphotoEmpty &) {
						// Empty photo
					});
				}
			}, [&](const MTPDmessageMediaDocument &docMedia) {
				msg["media_type"] = "document";
				if (const auto doc = docMedia.vdocument()) {
					doc->match([&](const MTPDdocument &d) {
						msg["document_id"] = qlonglong(d.vid().v);
						msg["document_size"] = qlonglong(d.vsize().v);
						msg["document_mime"] = qs(d.vmime_type());
						// Extract filename from attributes
						for (const auto &attr : d.vattributes().v) {
							attr.match([&](const MTPDdocumentAttributeFilename &fn) {
								msg["document_filename"] = qs(fn.vfile_name());
							}, [&](const MTPDdocumentAttributeVideo &) {
								msg["document_subtype"] = "video";
							}, [&](const MTPDdocumentAttributeAudio &a) {
								if (a.is_voice()) {
									msg["document_subtype"] = "voice";
								} else {
									msg["document_subtype"] = "audio";
								}
							}, [&](const MTPDdocumentAttributeSticker &) {
								msg["document_subtype"] = "sticker";
							}, [&](const MTPDdocumentAttributeAnimated &) {
								msg["document_subtype"] = "animation";
							}, [&](const auto &) {
								// Other attributes
							});
						}
						// Process document into session's data
						if (_session) {
							_session->data().processDocument(d);
						}
					}, [&](const MTPDdocumentEmpty &) {
						// Empty document
					});
				}
			}, [&](const MTPDmessageMediaGeo &) {
				msg["media_type"] = "geo";
			}, [&](const MTPDmessageMediaContact &) {
				msg["media_type"] = "contact";
			}, [&](const MTPDmessageMediaWebPage &) {
				msg["media_type"] = "webpage";
			}, [&](const MTPDmessageMediaPoll &) {
				msg["media_type"] = "poll";
			}, [&](const MTPDmessageMediaEmpty &) {
				// No media
			}, [&](const auto &) {
				msg["media_type"] = "other";
			});
		}

		if (data.is_pinned()) {
			msg["pinned"] = true;
		}
		if (const auto fwdFrom = data.vfwd_from()) {
			msg["forwarded"] = true;
		}

	}, [&](const MTPDmessageService &data) {
		msg["id"] = data.vid().v;
		msg["date"] = data.vdate().v;
		msg["service"] = true;

	}, [&](const MTPDmessageEmpty &data) {
		msg["id"] = data.vid().v;
		msg["empty"] = true;
	});

	return msg;
}

void Server::writeExportFiles() {
	if (!_activeExport) return;

	QJsonObject root;
	root["chat_id"] = _activeExport->chatId;
	root["chat_name"] = _activeExport->chatName;
	root["chat_type"] = _activeExport->chatType;
	root["export_date"] = QDateTime::currentDateTime().toString(Qt::ISODate);
	root["message_count"] = _activeExport->messages.size();
	root["media_downloaded"] = _activeExport->mediaDownloaded;
	root["media_failed"] = _activeExport->mediaFailed;
	root["messages"] = _activeExport->messages;

	QString filePath = _activeExport->resolvedPath + "/result.json";
	QFile file(filePath);
	if (file.open(QIODevice::WriteOnly)) {
		QJsonDocument doc(root);
		file.write(doc.toJson(QJsonDocument::Indented));
		file.close();

		_activeExport->finishedPath = _activeExport->resolvedPath;
		_activeExport->filesCount = 1 + _activeExport->mediaDownloaded;
		_activeExport->bytesCount = file.size();
		_activeExport->success = true;
		_activeExport->finished = true;

		// Also write HTML export
		writeHtmlExport();

		qWarning() << "MCP: Export complete -" << _activeExport->messages.size()
		           << "messages," << _activeExport->mediaDownloaded << "media files"
		           << "written to" << filePath
		           << "(" << file.size() << "bytes)";
	} else {
		_activeExport->errorMessage = "Failed to write: " + filePath;
		_activeExport->finished = true;
		qWarning() << "MCP: Export write failed:" << _activeExport->errorMessage;
	}
}

QString Server::escapeHtml(const QString &text) {
	QString result;
	result.reserve(text.size());
	for (const auto &ch : text) {
		if (ch == u'<') result += u"&lt;"_q;
		else if (ch == u'>') result += u"&gt;"_q;
		else if (ch == u'&') result += u"&amp;"_q;
		else if (ch == u'"') result += u"&quot;"_q;
		else if (ch == u'\'') result += u"&apos;"_q;
		else result += ch;
	}
	return result;
}

void Server::writeHtmlExport() {
	if (!_activeExport) return;

	QString htmlPath = _activeExport->resolvedPath + "/messages.html";
	QFile htmlFile(htmlPath);
	if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		qWarning() << "MCP: Failed to write HTML:" << htmlPath;
		return;
	}

	QTextStream out(&htmlFile);
	out.setEncoding(QStringConverter::Utf8);

	// Month names for date formatting
	static const char *months[] = {
		"January", "February", "March", "April", "May", "June",
		"July", "August", "September", "October", "November", "December"
	};

	// Write HTML header with inline CSS matching Telegram Desktop export style
	out << "<!DOCTYPE html>\n"
	    << "<html>\n"
	    << "<head>\n"
	    << " <meta charset=\"utf-8\">\n"
	    << " <title>" << escapeHtml(_activeExport->chatName) << " - Exported Data</title>\n"
	    << " <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
	    << " <style>\n"
	    // Core layout
	    << "body{margin:0;font:12px/18px 'Open Sans',\"Lucida Grande\",\"Lucida Sans Unicode\",Arial,Helvetica,Verdana,sans-serif;}\n"
	    << "strong{font-weight:700;}\n"
	    << "code,kbd,pre,samp{font-family:Menlo,Monaco,Consolas,\"Courier New\",monospace;}\n"
	    << "code{padding:2px 4px;font-size:90%;color:#c7254e;background-color:#f9f2f4;border-radius:4px;}\n"
	    << "pre{display:block;margin:0;line-height:1.42857143;word-break:break-all;word-wrap:break-word;"
	    << "color:#333;background-color:#f5f5f5;border-radius:4px;overflow:auto;padding:3px;border:1px solid #eee;max-height:none;font-size:inherit;}\n"
	    << ".clearfix:after{content:\" \";visibility:hidden;display:block;height:0;clear:both;}\n"
	    << ".pull_left{float:left;}.pull_right{float:right;}\n"
	    << ".page_wrap{background-color:#fff;color:#000;}\n"
	    << ".page_wrap a{color:#168acd;text-decoration:none;}\n"
	    << ".page_wrap a:hover{text-decoration:underline;}\n"
	    << ".page_header{position:fixed;z-index:10;background-color:#fff;width:100%;border-bottom:1px solid #e3e6e8;}\n"
	    << ".page_header .content{width:480px;margin:0 auto;}\n"
	    << ".bold{color:#212121;font-weight:700;}\n"
	    << ".details{color:#70777b;}\n"
	    << ".page_header .content .text{padding:24px 24px 22px 24px;font-size:22px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}\n"
	    << ".page_body{padding-top:64px;width:480px;margin:0 auto;}\n"
	    // Userpic styles
	    << ".userpic{display:block;border-radius:50%;overflow:hidden;}\n"
	    << ".userpic .initials{display:block;color:#fff;text-align:center;text-transform:uppercase;user-select:none;}\n"
	    << ".userpic1{background-color:#ff5555;}.userpic2{background-color:#64bf47;}\n"
	    << ".userpic3{background-color:#ffab00;}.userpic4{background-color:#4f9cd9;}\n"
	    << ".userpic5{background-color:#9884e8;}.userpic6{background-color:#e671a5;}\n"
	    << ".userpic7{background-color:#47bcd1;}.userpic8{background-color:#ff8c44;}\n"
	    // History/message styles
	    << ".history{padding:16px 0;}\n"
	    << ".message{margin:0 -10px;transition:background-color 2.0s ease;}\n"
	    << "div.selected{background-color:rgba(242,246,250,255);transition:background-color 0.5s ease;}\n"
	    << ".service{padding:10px 24px;}\n"
	    << ".service .body{text-align:center;}\n"
	    << ".message .userpic .initials{font-size:16px;}\n"
	    << ".default{padding:10px;}\n"
	    << ".default.joined{margin-top:-10px;}\n"
	    << ".default .from_name{color:#3892db;font-weight:700;padding-bottom:5px;}\n"
	    << ".default .body{margin-left:60px;}\n"
	    << ".default .text{word-wrap:break-word;line-height:150%;unicode-bidi:plaintext;text-align:start;}\n"
	    << ".default .reply_to,.default .media_wrap{padding-bottom:5px;}\n"
	    // Media styles
	    << ".default .video_file_wrap,.default .animated_wrap{position:relative;}\n"
	    << ".default .video_file,.default .animated,.default .photo,.default .sticker{display:block;}\n"
	    << ".video_duration{background:rgba(0,0,0,.4);padding:0px 5px;position:absolute;z-index:2;"
	    << "border-radius:2px;right:3px;bottom:3px;color:#fff;font-size:11px;}\n"
	    << ".video_play_bg{background:rgba(0,0,0,.4);width:40px;height:40px;line-height:0;"
	    << "position:absolute;z-index:2;border-radius:50%;overflow:hidden;"
	    << "margin:-20px auto 0 -20px;top:50%;left:50%;pointer-events:none;}\n"
	    << ".video_play{position:absolute;display:inline-block;top:50%;left:50%;"
	    << "margin-left:-5px;margin-top:-9px;z-index:1;width:0;height:0;"
	    << "border-style:solid;border-width:9px 0 9px 14px;border-color:transparent transparent transparent #fff;}\n"
	    // Date divider
	    << ".date_divider{text-align:center;padding:10px 0;color:#70777b;font-weight:700;}\n"
	    // Toast
	    << ".toast_container{position:fixed;left:50%;top:50%;opacity:0;transition:opacity 3.0s ease;}\n"
	    << ".toast_body{margin:0 -50%;float:left;border-radius:15px;padding:10px 20px;background:rgba(0,0,0,0.7);color:#fff;}\n"
	    << "div.toast_shown{opacity:1;transition:opacity 0.4s ease;}\n"
	    << " </style>\n"
	    // Inline JS for message linking
	    << " <script>\n"
	    << "\"use strict\";\n"
	    << "function CheckLocation(){var s=\"#go_to_message\",h=location.hash;if(h.substr(0,s.length)==s){var m=parseInt(h.substr(s.length));if(m)GoToMessage(m);}}\n"
	    << "function GoToMessage(id){var e=document.getElementById(\"message\"+id);if(e){location.hash=\"#go_to_message\"+id;"
	    << "e.scrollIntoView({behavior:'smooth',block:'center'});e.classList.add('selected');setTimeout(function(){e.classList.remove('selected');},2000);"
	    << "}}\n"
	    << " </script>\n"
	    << "</head>\n"
	    << "<body onload=\"CheckLocation();\">\n"
	    << "<div class=\"page_wrap\">\n";

	// Page header
	out << " <div class=\"page_header\">\n"
	    << "  <div class=\"content\">\n"
	    << "   <div class=\"text\">" << escapeHtml(_activeExport->chatName) << "</div>\n"
	    << "  </div>\n"
	    << " </div>\n";

	// Page body
	out << " <div class=\"page_body\">\n"
	    << "  <div class=\"history\">\n";

	// Track date for date dividers and sender for grouping
	QString lastDateStr;
	qlonglong lastFromId = -1;

	// Messages are in reverse chronological order (newest first) from API,
	// but we stored them in fetch order. Reverse for chronological display.
	for (int i = _activeExport->messages.size() - 1; i >= 0; --i) {
		QJsonObject msg = _activeExport->messages[i].toObject();
		if (msg.isEmpty()) continue;

		int msgId = msg["id"].toInt();
		int dateTs = msg["date"].toInt();
		QDateTime dateTime = QDateTime::fromSecsSinceEpoch(dateTs);
		QString timeStr = dateTime.toString("HH:mm");
		QString fullDateStr = dateTime.toString("dd MMMM yyyy");

		// Date divider when day changes
		QString dayStr = dateTime.toString("yyyy-MM-dd");
		if (dayStr != lastDateStr) {
			QString dateLabel = QString::number(dateTime.date().day())
				+ " " + months[dateTime.date().month() - 1]
				+ " " + QString::number(dateTime.date().year());
			out << "   <div class=\"date_divider\">" << escapeHtml(dateLabel) << "</div>\n";
			lastDateStr = dayStr;
			lastFromId = -1; // Reset grouping on date change
		}

		bool isService = msg["service"].toBool();

		if (isService) {
			out << "   <div class=\"message service\" id=\"message" << msgId << "\">\n"
			    << "    <div class=\"body details\">Service message</div>\n"
			    << "   </div>\n";
			lastFromId = -1;
			continue;
		}

		// Regular message
		QString fromName = msg["from_name"].toString();
		if (fromName.isEmpty()) {
			fromName = _activeExport->chatName;
		}
		qlonglong fromId = msg["from_id"].toVariant().toLongLong();
		if (fromId == 0) fromId = msgId; // Unique per-message if no from_id

		bool joined = (fromId == lastFromId);
		int colorIndex = (static_cast<int>(fromId % 8)) + 1;

		QString initials;
		QStringList words = fromName.split(' ', Qt::SkipEmptyParts);
		if (words.size() >= 2) {
			initials = words[0].left(1).toUpper() + words[1].left(1).toUpper();
		} else if (!words.isEmpty()) {
			initials = words[0].left(2).toUpper();
		}

		out << "   <div class=\"message default clearfix"
		    << (joined ? " joined" : "") << "\" id=\"message" << msgId << "\">\n";

		// Userpic (only if not joined)
		if (!joined) {
			out << "    <div class=\"pull_left userpic_wrap\">\n"
			    << "     <div class=\"userpic userpic" << colorIndex
			    << "\" style=\"width:42px;height:42px;\">\n"
			    << "      <div class=\"initials\" style=\"line-height:42px;font-size:16px;\">"
			    << escapeHtml(initials) << "</div>\n"
			    << "     </div>\n"
			    << "    </div>\n";
		}

		out << "    <div class=\"body\">\n";

		// Timestamp
		out << "     <div class=\"pull_right date details\" title=\""
		    << escapeHtml(fullDateStr + " " + timeStr) << "\">"
		    << escapeHtml(timeStr) << "</div>\n";

		// Sender name (only if not joined)
		if (!joined) {
			out << "     <div class=\"from_name\">" << escapeHtml(fromName) << "</div>\n";
		}

		// Forwarded indicator
		if (msg["forwarded"].toBool()) {
			out << "     <div class=\"details\">Forwarded message</div>\n";
		}

		// Media
		QString mediaFile = msg["media_file"].toString();
		QString mediaType = msg["media_type"].toString();

		if (!mediaFile.isEmpty()) {
			out << "     <div class=\"media_wrap clearfix\">\n";

			if (mediaType == "photo") {
				out << "      <a class=\"photo_wrap clearfix pull_left\" href=\""
				    << escapeHtml(mediaFile) << "\">\n"
				    << "       <img class=\"photo\" style=\"max-width:260px;max-height:260px;\" src=\""
				    << escapeHtml(mediaFile) << "\">\n"
				    << "      </a>\n";
			} else if (mediaType == "document") {
				QString subtype = msg["document_subtype"].toString();
				QString filename = msg["document_filename"].toString();
				qlonglong fileSize = msg["document_size"].toVariant().toLongLong();

				// Format file size
				QString sizeStr;
				if (fileSize < 1024) sizeStr = QString::number(fileSize) + " B";
				else if (fileSize < 1024 * 1024) sizeStr = QString::number(fileSize / 1024.0, 'f', 1) + " KB";
				else sizeStr = QString::number(fileSize / (1024.0 * 1024.0), 'f', 1) + " MB";

				if (subtype == "video") {
					out << "      <a class=\"video_file_wrap clearfix pull_left\" href=\""
					    << escapeHtml(mediaFile) << "\">\n"
					    << "       <div class=\"video_play_bg\"><div class=\"video_play\"></div></div>\n"
					    << "       <video class=\"video_file\" style=\"max-width:260px;max-height:260px;\" preload=\"metadata\" src=\""
					    << escapeHtml(mediaFile) << "\"></video>\n"
					    << "      </a>\n";
				} else {
					// Generic file attachment
					out << "      <div class=\"media clearfix pull_left\">\n"
					    << "       <div class=\"body\">\n"
					    << "        <div class=\"title bold\">" << escapeHtml(filename) << "</div>\n"
					    << "        <div class=\"status details\">" << escapeHtml(sizeStr) << "</div>\n"
					    << "       </div>\n"
					    << "      </div>\n";
				}
			}

			out << "     </div>\n";
		} else if (!mediaType.isEmpty() && mediaFile.isEmpty()) {
			// Media exists but wasn't downloaded
			out << "     <div class=\"media_wrap clearfix\">\n"
			    << "      <div class=\"media clearfix pull_left\">\n"
			    << "       <div class=\"body\">\n"
			    << "        <div class=\"title bold\">[" << escapeHtml(mediaType) << "]</div>\n"
			    << "        <div class=\"status details\">Not downloaded</div>\n"
			    << "       </div>\n"
			    << "      </div>\n"
			    << "     </div>\n";
		}

		// Text
		QString text = msg["text"].toString();
		if (!text.isEmpty()) {
			// Convert newlines to <br> for HTML display
			QString htmlText = escapeHtml(text);
			htmlText.replace('\n', "<br>\n");
			out << "     <div class=\"text\">" << htmlText << "</div>\n";
		}

		out << "    </div>\n"  // body
		    << "   </div>\n";  // message

		lastFromId = fromId;
	}

	// Close history, page_body, page_wrap
	out << "  </div>\n"   // history
	    << " </div>\n"    // page_body
	    << "</div>\n"      // page_wrap
	    << "</body>\n"
	    << "</html>\n";

	htmlFile.close();
	_activeExport->filesCount++;
	qWarning() << "MCP: HTML export written to" << htmlPath;
}

// ===== MEDIA DOWNLOAD PHASE =====

void Server::startMediaDownloadPhase() {
	if (!_activeExport || _activeExport->finished) return;

	// Scan all fetched messages for media (documents and photos)
	_activeExport->mediaItems.clear();
	_activeExport->currentMediaIndex = 0;
	_activeExport->mediaDownloaded = 0;
	_activeExport->mediaFailed = 0;
	_activeExport->downloadingMedia = true;

	int64_t totalBytes = 0;
	for (int i = 0; i < _activeExport->messages.size(); ++i) {
		QJsonObject msg = _activeExport->messages[i].toObject();
		int msgId = msg["id"].toInt();

		if (msg.contains("document_id")) {
			ActiveExport::MediaItem item;
			item.type = ActiveExport::MediaItem::Document;
			item.documentId = DocumentId(msg["document_id"].toVariant().toLongLong());
			item.messageId = msgId;
			item.messageIndex = i;
			_activeExport->mediaItems.append(item);
			totalBytes += msg["document_size"].toVariant().toLongLong();
		} else if (msg.contains("photo_id")) {
			ActiveExport::MediaItem item;
			item.type = ActiveExport::MediaItem::Photo;
			item.photoId = PhotoId(msg["photo_id"].toVariant().toLongLong());
			item.messageId = msgId;
			item.messageIndex = i;
			_activeExport->mediaItems.append(item);
			totalBytes += 500 * 1024; // estimate ~500KB per photo
		}
	}

	_activeExport->totalMediaBytes = totalBytes;
	_activeExport->mediaDownloadedBytes = 0;
	_activeExport->mediaPhaseStartTime = QDateTime::currentDateTime();

	if (_activeExport->mediaItems.isEmpty()) {
		qWarning() << "MCP: No media to download, writing export files";
		_activeExport->downloadingMedia = false;
		writeExportFiles();
		return;
	}

	// Create media/ subdirectory
	QDir exportDir(_activeExport->resolvedPath);
	exportDir.mkpath("media");

	qWarning() << "MCP: Starting media download phase -"
	           << _activeExport->mediaItems.size() << "items to download";

	// Subscribe to document load progress for completion notifications
	_activeExport->mediaLifetime = std::make_unique<rpl::lifetime>();

	downloadNextMediaItem();
}

QString Server::generateMediaFilename(DocumentData *document, int msgId) {
	QString name;
	if (document) {
		name = document->filename();
	}
	if (name.isEmpty()) {
		// Generate from mime type
		QString ext;
		if (document) {
			QString mime = document->mimeString();
			if (mime.startsWith("image/")) {
				ext = "." + mime.mid(6);
				if (ext == ".jpeg") ext = ".jpg";
			} else if (mime.startsWith("video/")) {
				ext = "." + mime.mid(6);
			} else if (mime.startsWith("audio/")) {
				ext = "." + mime.mid(6);
				if (ext == ".mpeg") ext = ".mp3";
			} else {
				ext = ".bin";
			}
		} else {
			ext = ".bin";
		}
		name = QString("doc_%1_%2%3").arg(msgId).arg(document ? document->id : 0).arg(ext);
	}
	return name;
}

QString Server::generateMediaFilename(PhotoData *photo, int msgId) {
	Q_UNUSED(photo);
	return QString("photo_%1.jpg").arg(msgId);
}

void Server::downloadNextMediaItem() {
	if (!_activeExport || _activeExport->finished) return;
	if (_activeExport->currentMediaIndex >= _activeExport->mediaItems.size()) {
		onAllMediaDownloaded();
		return;
	}

	// Start per-item timeout scaled to file size.
	// Assumes minimum throughput of 100 KB/s; floor 60s, cap 30 min.
	if (!_mediaItemTimeoutTimer) {
		_mediaItemTimeoutTimer = new QTimer(this);
		_mediaItemTimeoutTimer->setSingleShot(true);
		connect(_mediaItemTimeoutTimer, &QTimer::timeout,
			this, &Server::onMediaItemTimeout);
	}

	auto &item = _activeExport->mediaItems[_activeExport->currentMediaIndex];
	QString mediaDir = _activeExport->resolvedPath + "/media/";

	if (item.type == ActiveExport::MediaItem::Document) {
		if (!_session) {
			item.failed = true;
			_activeExport->mediaFailed++;
			_activeExport->currentMediaIndex++;
			QTimer::singleShot(100, [this]() { downloadNextMediaItem(); });
			return;
		}

		auto document = _session->data().document(item.documentId);

		// Start size-based timeout: assumes min throughput 100 KB/s.
		// Floor 60s, cap 30 min. E.g. 100MB -> 1000s, 4GB -> 1800s (capped).
		{
			int64_t sizeBytes = document->size;
			int timeoutSecs = 60; // default for unknown size
			if (sizeBytes > 0) {
				timeoutSecs = static_cast<int>(sizeBytes / (100 * 1024));
				timeoutSecs = std::clamp(timeoutSecs, 60, 1800);
			}
			_mediaItemTimeoutTimer->start(timeoutSecs * 1000);
			qWarning() << "MCP: Timeout for document:" << timeoutSecs << "s"
			           << "(size:" << sizeBytes << "bytes)";
		}

		QString filename = generateMediaFilename(document, item.messageId);
		item.targetFilename = filename;
		QString targetPath = mediaDir + filename;

		// Check if already downloaded locally
		QString existingPath = document->filepath(true);
		if (!existingPath.isEmpty() && QFile::exists(existingPath)) {
			// Copy from local cache
			QFile::copy(existingPath, targetPath);
			item.downloaded = true;
			_activeExport->mediaDownloaded++;
			_activeExport->mediaDownloadedBytes += QFileInfo(targetPath).size();

			// Update message JSON with media_file reference
			QJsonObject msg = _activeExport->messages[item.messageIndex].toObject();
			msg["media_file"] = "media/" + filename;
			_activeExport->messages[item.messageIndex] = msg;

			qWarning() << "MCP: Media" << (_activeExport->currentMediaIndex + 1)
			           << "/" << _activeExport->mediaItems.size()
			           << "copied from cache:" << filename;

			_activeExport->currentMediaIndex++;
			int delay = 100 + (QRandomGenerator::global()->bounded(200));
			QTimer::singleShot(delay, [this]() { downloadNextMediaItem(); });
			return;
		}

		// Need to download from cloud using document->save() - same as UI
		Data::FileOrigin origin = Data::FileOrigin(
			Data::FileOriginMessage(_activeExport->exportPeerId, MsgId(item.messageId)));

		qWarning() << "MCP: Media" << (_activeExport->currentMediaIndex + 1)
		           << "/" << _activeExport->mediaItems.size()
		           << "downloading document:" << filename;

		// Subscribe to load completion for this document
		_session->data().documentLoadProgress(
		) | rpl::filter([docPtr = document.get()](not_null<DocumentData*> d) {
			return d == docPtr;
		}) | rpl::start_with_next([this, document](not_null<DocumentData*>) {
			if (!_activeExport || _activeExport->finished) return;
			if (document->loading()) return; // still in progress

			onMediaDownloadComplete(document);
		}, *_activeExport->mediaLifetime);

		document->save(origin, targetPath, LoadFromCloudOrLocal, false);

		// If it was already loaded and save() completed synchronously
		if (!document->loading()) {
			onMediaDownloadComplete(document);
		}

	} else if (item.type == ActiveExport::MediaItem::Photo) {
		if (!_session) {
			item.failed = true;
			_activeExport->mediaFailed++;
			_activeExport->currentMediaIndex++;
			QTimer::singleShot(100, [this]() { downloadNextMediaItem(); });
			return;
		}

		auto photo = _session->data().photo(item.photoId);
		QString filename = generateMediaFilename(photo, item.messageId);
		item.targetFilename = filename;
		QString targetPath = mediaDir + filename;

		Data::FileOrigin origin = Data::FileOrigin(
			Data::FileOriginMessage(_activeExport->exportPeerId, MsgId(item.messageId)));

		qWarning() << "MCP: Media" << (_activeExport->currentMediaIndex + 1)
		           << "/" << _activeExport->mediaItems.size()
		           << "downloading photo:" << filename;

		// CRITICAL: Create media view BEFORE calling load().
		// If the photo is already cached, load()'s done callback fires
		// synchronously and calls activeMediaView()->set(). Without an
		// active media view, set() is never called, data is discarded,
		// and downloaderTaskFinished never fires for this photo.
		auto photoMedia = photo->createMediaView();

		// Photos are small (typically <10MB), 120s timeout is generous
		_mediaItemTimeoutTimer->start(120 * 1000);

		// Start loading - may complete synchronously if cached
		photo->load(Data::PhotoSize::Large, origin, LoadFromCloudOrLocal, false);

		// Check if already loaded (synchronous completion or was cached)
		if (photoMedia->loaded()) {
			_mediaItemTimeoutTimer->stop();
			photoMedia->saveToFile(targetPath);
			item.downloaded = true;
			_activeExport->mediaDownloaded++;
			auto photoSize = QFileInfo(targetPath).size();
			_activeExport->bytesCount += photoSize;
			_activeExport->mediaDownloadedBytes += photoSize;

			QJsonObject msg = _activeExport->messages[item.messageIndex].toObject();
			msg["media_file"] = "media/" + filename;
			_activeExport->messages[item.messageIndex] = msg;

			qWarning() << "MCP: Photo saved (immediate):" << filename;

			_activeExport->currentMediaIndex++;
			int delay = 100 + (QRandomGenerator::global()->bounded(200));
			QTimer::singleShot(delay, [this]() { downloadNextMediaItem(); });
			return;
		}

		// Not loaded yet - wait for downloaderTaskFinished signal.
		// photoMedia is kept alive via shared_ptr capture in the lambda,
		// ensuring activeMediaView() returns it when load() completes.
		rpl::single(
			rpl::empty_value()
		) | rpl::then(
			_session->downloaderTaskFinished()
		) | rpl::filter([photoMedia] {
			return photoMedia->loaded();
		}) | rpl::take(1) | rpl::start_with_next([this, photoMedia, targetPath, photo](auto&&) {
			if (!_activeExport || _activeExport->finished) return;
			photoMedia->saveToFile(targetPath);
			onMediaDownloadComplete(photo);
		}, *_activeExport->mediaLifetime);
	}
}

void Server::onMediaDownloadComplete(not_null<DocumentData*> document) {
	if (!_activeExport || _activeExport->finished) return;
	if (_activeExport->currentMediaIndex >= _activeExport->mediaItems.size()) return;

	auto &item = _activeExport->mediaItems[_activeExport->currentMediaIndex];
	if (item.type != ActiveExport::MediaItem::Document || item.documentId != document->id) return;

	// Stop per-item timeout - download completed normally
	if (_mediaItemTimeoutTimer) {
		_mediaItemTimeoutTimer->stop();
	}

	// Reset the lifetime to unsubscribe from this document's signals
	_activeExport->mediaLifetime = std::make_unique<rpl::lifetime>();

	QString targetPath = _activeExport->resolvedPath + "/media/" + item.targetFilename;

	// Check if file was written successfully
	if (QFile::exists(targetPath) && QFileInfo(targetPath).size() > 0) {
		auto fileSize = QFileInfo(targetPath).size();
		item.downloaded = true;
		_activeExport->mediaDownloaded++;
		_activeExport->bytesCount += fileSize;
		_activeExport->mediaDownloadedBytes += fileSize;

		QJsonObject msg = _activeExport->messages[item.messageIndex].toObject();
		msg["media_file"] = "media/" + item.targetFilename;
		_activeExport->messages[item.messageIndex] = msg;

		qWarning() << "MCP: Document downloaded:" << item.targetFilename
		           << "(" << fileSize << "bytes)";
	} else {
		// File wasn't saved to target path - check if bytes are available
		auto media = document->activeMediaView();
		if (media && !media->bytes().isEmpty()) {
			QFile f(targetPath);
			if (f.open(QIODevice::WriteOnly)) {
				f.write(media->bytes());
				f.close();
				auto fileSize = f.size();
				item.downloaded = true;
				_activeExport->mediaDownloaded++;
				_activeExport->bytesCount += fileSize;
				_activeExport->mediaDownloadedBytes += fileSize;

				QJsonObject msg = _activeExport->messages[item.messageIndex].toObject();
				msg["media_file"] = "media/" + item.targetFilename;
				_activeExport->messages[item.messageIndex] = msg;

				qWarning() << "MCP: Document saved from bytes:" << item.targetFilename;
			} else {
				item.failed = true;
				_activeExport->mediaFailed++;
				qWarning() << "MCP: Failed to write document:" << item.targetFilename;
			}
		} else {
			// Also try copying from location
			auto &loc = document->location(true);
			if (!loc.isEmpty() && loc.accessEnable()) {
				QFile::copy(loc.name(), targetPath);
				loc.accessDisable();
				if (QFile::exists(targetPath)) {
					auto fileSize = QFileInfo(targetPath).size();
					item.downloaded = true;
					_activeExport->mediaDownloaded++;
					_activeExport->bytesCount += fileSize;
					_activeExport->mediaDownloadedBytes += fileSize;

					QJsonObject msg = _activeExport->messages[item.messageIndex].toObject();
					msg["media_file"] = "media/" + item.targetFilename;
					_activeExport->messages[item.messageIndex] = msg;

					qWarning() << "MCP: Document copied from location:" << item.targetFilename;
				} else {
					item.failed = true;
					_activeExport->mediaFailed++;
					qWarning() << "MCP: Failed to copy document:" << item.targetFilename;
				}
			} else {
				item.failed = true;
				_activeExport->mediaFailed++;
				qWarning() << "MCP: Document not available:" << item.targetFilename;
			}
		}
	}

	_activeExport->currentMediaIndex++;
	int delay = 500 + (QRandomGenerator::global()->bounded(1500));
	QTimer::singleShot(delay, [this]() { downloadNextMediaItem(); });
}

void Server::onMediaDownloadComplete(not_null<PhotoData*> photo) {
	if (!_activeExport || _activeExport->finished) return;
	if (_activeExport->currentMediaIndex >= _activeExport->mediaItems.size()) return;

	auto &item = _activeExport->mediaItems[_activeExport->currentMediaIndex];
	if (item.type != ActiveExport::MediaItem::Photo) return;

	// Stop per-item timeout - download completed normally
	if (_mediaItemTimeoutTimer) {
		_mediaItemTimeoutTimer->stop();
	}

	// Reset lifetime to unsubscribe from downloaderTaskFinished
	_activeExport->mediaLifetime = std::make_unique<rpl::lifetime>();

	QString targetPath = _activeExport->resolvedPath + "/media/" + item.targetFilename;

	if (QFile::exists(targetPath) && QFileInfo(targetPath).size() > 0) {
		auto fileSize = QFileInfo(targetPath).size();
		item.downloaded = true;
		_activeExport->mediaDownloaded++;
		_activeExport->bytesCount += fileSize;
		_activeExport->mediaDownloadedBytes += fileSize;

		QJsonObject msg = _activeExport->messages[item.messageIndex].toObject();
		msg["media_file"] = "media/" + item.targetFilename;
		_activeExport->messages[item.messageIndex] = msg;

		qWarning() << "MCP: Photo downloaded:" << item.targetFilename;
	} else {
		item.failed = true;
		_activeExport->mediaFailed++;
		qWarning() << "MCP: Photo not saved:" << item.targetFilename;
	}

	_activeExport->currentMediaIndex++;
	int delay = 500 + (QRandomGenerator::global()->bounded(1500));
	QTimer::singleShot(delay, [this]() { downloadNextMediaItem(); });
}

void Server::onMediaItemTimeout() {
	if (!_activeExport || _activeExport->finished) return;
	if (_activeExport->currentMediaIndex >= _activeExport->mediaItems.size()) return;

	auto &item = _activeExport->mediaItems[_activeExport->currentMediaIndex];
	QString itemDesc = (item.type == ActiveExport::MediaItem::Document)
		? ("document " + item.targetFilename)
		: ("photo " + item.targetFilename);

	qWarning() << "MCP: Media item timeout for" << itemDesc
	           << "- skipping to next item";

	// Unsubscribe from load signals (documents use documentLoadProgress,
	// photos use downloaderTaskFinished - both via mediaLifetime)
	_activeExport->mediaLifetime = std::make_unique<rpl::lifetime>();

	item.failed = true;
	_activeExport->mediaFailed++;
	_activeExport->currentMediaIndex++;

	// Small delay before next item
	QTimer::singleShot(200, [this]() { downloadNextMediaItem(); });
}

void Server::onAllMediaDownloaded() {
	if (!_activeExport || _activeExport->finished) return;

	// Clean up timeout timer
	if (_mediaItemTimeoutTimer) {
		_mediaItemTimeoutTimer->stop();
	}

	_activeExport->downloadingMedia = false;
	_activeExport->mediaLifetime.reset();

	qWarning() << "MCP: Media download phase complete -"
	           << _activeExport->mediaDownloaded << "downloaded,"
	           << _activeExport->mediaFailed << "failed out of"
	           << _activeExport->mediaItems.size();

	writeExportFiles();
}

} // namespace MCP
