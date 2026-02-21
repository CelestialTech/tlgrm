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

		// Load the large size photo
		photo->load(Data::PhotoSize::Large, origin, LoadFromCloudOrLocal, false);

		// Check if already loaded
		auto photoMedia = photo->createMediaView();
		if (photoMedia->loaded()) {
			photoMedia->saveToFile(targetPath);
			item.downloaded = true;
			_activeExport->mediaDownloaded++;
			auto photoSize = QFileInfo(targetPath).size();
			_activeExport->bytesCount += photoSize;
			_activeExport->mediaDownloadedBytes += photoSize;

			QJsonObject msg = _activeExport->messages[item.messageIndex].toObject();
			msg["media_file"] = "media/" + filename;
			_activeExport->messages[item.messageIndex] = msg;

			qWarning() << "MCP: Photo saved:" << filename;

			_activeExport->currentMediaIndex++;
			int delay = 100 + (QRandomGenerator::global()->bounded(200));
			QTimer::singleShot(delay, [this]() { downloadNextMediaItem(); });
			return;
		}

		// Need to wait for photo load - poll for completion
		auto pollTimer = new QTimer(this);
		auto photoPtr = photo.get();
		auto photoMediaPtr = std::move(photoMedia);
		int pollCount = 0;
		connect(pollTimer, &QTimer::timeout, this, [this, photoPtr, targetPath, filename, pollTimer, pollCount]() mutable {
			if (!_activeExport || _activeExport->finished) {
				pollTimer->deleteLater();
				return;
			}
			pollCount++;
			auto media = photoPtr->createMediaView();
			if (media->loaded()) {
				pollTimer->stop();
				pollTimer->deleteLater();
				media->saveToFile(targetPath);
				onMediaDownloadComplete(gsl::make_not_null(photoPtr));
			} else if (pollCount > 300) { // 5 minute timeout (300 * 1s)
				pollTimer->stop();
				pollTimer->deleteLater();
				// Timeout - mark as failed
				auto &currentItem = _activeExport->mediaItems[_activeExport->currentMediaIndex];
				currentItem.failed = true;
				_activeExport->mediaFailed++;
				qWarning() << "MCP: Photo download timeout:" << filename;
				_activeExport->currentMediaIndex++;
				downloadNextMediaItem();
			}
		});
		pollTimer->start(1000); // Poll every second
	}
}

void Server::onMediaDownloadComplete(not_null<DocumentData*> document) {
	if (!_activeExport || _activeExport->finished) return;
	if (_activeExport->currentMediaIndex >= _activeExport->mediaItems.size()) return;

	auto &item = _activeExport->mediaItems[_activeExport->currentMediaIndex];
	if (item.type != ActiveExport::MediaItem::Document || item.documentId != document->id) return;

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

void Server::onAllMediaDownloaded() {
	if (!_activeExport || _activeExport->finished) return;

	_activeExport->downloadingMedia = false;
	_activeExport->mediaLifetime.reset();

	qWarning() << "MCP: Media download phase complete -"
	           << _activeExport->mediaDownloaded << "downloaded,"
	           << _activeExport->mediaFailed << "failed out of"
	           << _activeExport->mediaItems.size();

	writeExportFiles();
}

} // namespace MCP
