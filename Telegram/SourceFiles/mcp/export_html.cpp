// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "mcp/export_html.h"
#include "mcp/chat_archiver.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"

#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QTextStream>
#include <QtCore/QJsonDocument>

namespace MCP {

HtmlExporter::HtmlExporter(QObject *parent)
	: QObject(parent) {
}

HtmlExporter::~HtmlExporter() = default;

bool HtmlExporter::exportChat(
		qint64 chatId,
		const QString &outputPath,
		const HtmlExportOptions &options) {

	_exportedCount = 0;
	_lastError.clear();

	if (!_dataSession) {
		_lastError = "No data session available";
		return false;
	}

	PeerId peerId(chatId);
	auto peer = _dataSession->peer(peerId);
	if (!peer) {
		_lastError = QString("Chat %1 not found").arg(chatId);
		return false;
	}

	QString chatTitle = peer->name();
	auto history = _dataSession->history(peerId);
	if (!history) {
		_lastError = QString("No history for chat %1").arg(chatId);
		return false;
	}

	QJsonArray messages;
	int total = 0;

	for (auto blockIt = history->blocks.begin();
		 blockIt != history->blocks.end();
		 ++blockIt) {
		const auto &block = *blockIt;
		if (!block) continue;

		for (auto msgIt = block->messages.begin();
			 msgIt != block->messages.end();
			 ++msgIt) {
			const auto &element = *msgIt;
			if (!element) continue;
			auto item = element->data();
			if (!item) continue;

			qint64 timestamp = item->date();

			if (options.startDate.isValid() &&
				timestamp < options.startDate.toSecsSinceEpoch()) {
				continue;
			}
			if (options.endDate.isValid() &&
				timestamp > options.endDate.toSecsSinceEpoch()) {
				continue;
			}

			QJsonObject msg;
			msg["message_id"] = QString::number(item->id.bare);
			msg["date"] = timestamp;
			msg["text"] = item->originalText().text;

			auto from = item->from();
			if (from) {
				QJsonObject fromUser;
				fromUser["id"] = QString::number(from->id.value);
				fromUser["name"] = from->name();
				if (!from->username().isEmpty()) {
					fromUser["username"] = from->username();
				}
				msg["from"] = fromUser;
			}

			msg["is_outgoing"] = item->out();

			if (item->replyToId()) {
				msg["reply_to"] = QString::number(item->replyToId().bare);
			}

			if (options.embedMedia && item->media()) {
				MediaData media = downloadMedia(item);
				if (media.downloaded) {
					msg["media_data"] = QString::fromLatin1(media.data.toBase64());
					msg["media_mime"] = media.mimeType;
					msg["media_filename"] = media.filename;
					msg["media_width"] = media.width;
					msg["media_height"] = media.height;
					msg["is_video"] = media.isVideo;
					msg["is_document"] = media.isDocument;
				}
			}

			messages.append(msg);
			_exportedCount++;
			total++;

			if (total % 100 == 0) {
				Q_EMIT progressChanged(total, -1);
			}
		}
	}

	QString html = generateHtml(chatTitle, messages, options);

	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		_lastError = QString("Cannot open file: %1").arg(outputPath);
		return false;
	}

	QTextStream out(&file);
	out.setEncoding(QStringConverter::Utf8);
	out << html;
	file.close();

	Q_EMIT exportFinished(true, outputPath);
	return true;
}

bool HtmlExporter::exportFromArchive(
		const QString &chatTitle,
		const QJsonArray &messages,
		const QString &outputPath,
		const HtmlExportOptions &options) {

	_exportedCount = 0;
	_lastError.clear();

	if (messages.isEmpty()) {
		_lastError = "No messages to export";
		return false;
	}

	_exportedCount = messages.size();

	QString html = generateHtml(chatTitle, messages, options);

	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		_lastError = QString("Cannot open file: %1").arg(outputPath);
		return false;
	}

	QTextStream out(&file);
	out.setEncoding(QStringConverter::Utf8);
	out << html;
	file.close();

	Q_EMIT exportFinished(true, outputPath);
	return true;
}

QString HtmlExporter::generateHtml(
		const QString &chatTitle,
		const QJsonArray &messages,
		const HtmlExportOptions &options) {

	QString html;
	html += "<!DOCTYPE html>\n<html>\n<head>\n";
	html += "<meta charset=\"UTF-8\">\n";
	html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
	html += QString("<title>%1 - Telegram Export</title>\n").arg(escapeHtml(chatTitle));
	html += "<style>\n" + generateCss() + "</style>\n";
	html += "</head>\n<body>\n";

	html += QString("<h1>%1</h1>\n").arg(escapeHtml(chatTitle));
	html += QString("<p class=\"meta\">Exported: %1 | Messages: %2</p>\n")
		.arg(QDateTime::currentDateTime().toString(Qt::ISODate))
		.arg(messages.size());
	html += "<hr>\n";
	html += "<div class=\"messages\">\n";

	for (const auto &msgRef : messages) {
		QJsonObject msg = msgRef.toObject();
		html += formatMessageHtml(msg);
	}

	html += "</div>\n</body>\n</html>\n";
	return html;
}

QString HtmlExporter::formatMessageHtml(const QJsonObject &message) {
	QString html;
	bool isOutgoing = message["is_outgoing"].toBool();
	qint64 timestamp = message["date"].toVariant().toLongLong();

	html += QString("<div class=\"message%1\">\n")
		.arg(isOutgoing ? " outgoing" : "");

	QJsonObject from = message["from"].toObject();
	QString fromName = from["name"].toString();
	QString username = from["username"].toString();

	html += "<div class=\"header\">\n";
	if (!fromName.isEmpty()) {
		html += QString("<span class=\"from\">%1</span>")
			.arg(escapeHtml(fromName));
		if (!username.isEmpty()) {
			html += QString(" <span class=\"username\">@%1</span>")
				.arg(escapeHtml(username));
		}
	}
	html += QString(" <span class=\"date\">%1</span>\n")
		.arg(formatDate(timestamp));
	html += "</div>\n";

	if (message.contains("reply_to")) {
		html += QString("<div class=\"reply-to\">Reply to message #%1</div>\n")
			.arg(message["reply_to"].toString());
	}

	QString text = message["text"].toString();
	if (!text.isEmpty()) {
		QJsonArray entities = message["entities"].toArray();
		html += "<div class=\"content\">\n";
		html += formatTextWithEntities(text, entities);
		html += "</div>\n";
	}

	if (message.contains("media_data")) {
		MediaData media;
		media.data = QByteArray::fromBase64(message["media_data"].toString().toLatin1());
		media.mimeType = message["media_mime"].toString();
		media.filename = message["media_filename"].toString();
		media.width = message["media_width"].toInt();
		media.height = message["media_height"].toInt();
		media.isVideo = message["is_video"].toBool();
		media.isDocument = message["is_document"].toBool();
		media.downloaded = true;

		html += embedMediaHtml(media, QString());
	}

	html += "</div>\n";
	return html;
}

QString HtmlExporter::formatTextWithEntities(const QString &text, const QJsonArray &entities) {
	if (entities.isEmpty()) {
		QString escaped = escapeHtml(text);
		escaped.replace("\n", "<br>\n");
		return escaped;
	}

	QString result;
	int lastEnd = 0;

	struct Entity {
		int offset;
		int length;
		QString type;
		QString url;
	};
	QVector<Entity> sortedEntities;

	for (const auto &e : entities) {
		QJsonObject obj = e.toObject();
		Entity entity;
		entity.offset = obj["offset"].toInt();
		entity.length = obj["length"].toInt();
		entity.type = obj["type"].toString();
		entity.url = obj["url"].toString();
		sortedEntities.append(entity);
	}

	std::sort(sortedEntities.begin(), sortedEntities.end(),
		[](const Entity &a, const Entity &b) { return a.offset < b.offset; });

	for (const auto &entity : sortedEntities) {
		if (entity.offset > lastEnd) {
			QString between = text.mid(lastEnd, entity.offset - lastEnd);
			result += escapeHtml(between);
		}

		QString entityText = text.mid(entity.offset, entity.length);
		QString escapedText = escapeHtml(entityText);

		if (entity.type == "bold") {
			result += "<strong>" + escapedText + "</strong>";
		} else if (entity.type == "italic") {
			result += "<em>" + escapedText + "</em>";
		} else if (entity.type == "code") {
			result += "<code>" + escapedText + "</code>";
		} else if (entity.type == "pre") {
			result += "<pre>" + escapedText + "</pre>";
		} else if (entity.type == "underline") {
			result += "<u>" + escapedText + "</u>";
		} else if (entity.type == "strikethrough") {
			result += "<s>" + escapedText + "</s>";
		} else if (entity.type == "spoiler") {
			result += "<span class=\"spoiler\">" + escapedText + "</span>";
		} else if (entity.type == "text_link") {
			result += QString("<a href=\"%1\">%2</a>")
				.arg(escapeHtml(entity.url))
				.arg(escapedText);
		} else if (entity.type == "url") {
			result += QString("<a href=\"%1\">%1</a>").arg(escapedText);
		} else if (entity.type == "mention") {
			result += QString("<a href=\"https://t.me/%1\">%2</a>")
				.arg(escapedText.mid(1))
				.arg(escapedText);
		} else if (entity.type == "blockquote") {
			result += "<blockquote>" + escapedText + "</blockquote>";
		} else {
			result += escapedText;
		}

		lastEnd = entity.offset + entity.length;
	}

	if (lastEnd < text.length()) {
		result += escapeHtml(text.mid(lastEnd));
	}

	result.replace("\n", "<br>\n");
	return result;
}

QString HtmlExporter::escapeHtml(const QString &text) {
	QString result = text;
	result.replace("&", "&amp;");
	result.replace("<", "&lt;");
	result.replace(">", "&gt;");
	result.replace("\"", "&quot;");
	result.replace("'", "&#39;");
	return result;
}

MediaData HtmlExporter::downloadMedia(HistoryItem *item) {
	MediaData result;

	if (!item || !item->media()) {
		return result;
	}

	const auto media = item->media();

	if (const auto document = media->document()) {
		QString localPath = document->filepath(true);

		if (!localPath.isEmpty() && QFile::exists(localPath)) {
			QFile file(localPath);
			if (file.open(QIODevice::ReadOnly)) {
				result.data = file.readAll();
				result.mimeType = document->mimeString();
				result.filename = document->filename();
				result.size = document->size;
				result.isVideo = document->isVideoFile();
				result.isDocument = !result.isVideo;
				result.downloaded = true;
				file.close();
			}
		}
	} else if (const auto photo = media->photo()) {
		auto photoMedia = photo->createMediaView();
		if (photoMedia && photoMedia->loaded()) {
			QByteArray bytes = photoMedia->imageBytes(Data::PhotoSize::Large);
			if (bytes.isEmpty()) {
				bytes = photoMedia->imageBytes(Data::PhotoSize::Small);
			}
			if (!bytes.isEmpty()) {
				result.data = bytes;
				result.mimeType = "image/jpeg";
				result.filename = QString("photo_%1.jpg").arg(photo->id);
				result.size = bytes.size();
				result.isDocument = false;
				result.isVideo = false;
				result.downloaded = true;
			}
		}
	}

	return result;
}

QString HtmlExporter::embedMediaHtml(const MediaData &media, const QString &caption) {
	QString html;

	if (!media.downloaded || media.data.isEmpty()) {
		return html;
	}

	QString dataUri = imageToBase64DataUri(media.data, media.mimeType);

	html += "<div class=\"media\">\n";

	if (media.isVideo) {
		html += QString("<video controls src=\"%1\"").arg(dataUri);
		if (media.width > 0 && media.height > 0) {
			html += QString(" width=\"%1\" height=\"%2\"")
				.arg(qMin(media.width, 640))
				.arg(qMin(media.height, 480));
		}
		html += "></video>\n";
	} else if (media.isDocument) {
		html += QString("<div class=\"document\"><a href=\"%1\" download=\"%2\">%3</a></div>\n")
			.arg(dataUri)
			.arg(escapeHtml(media.filename))
			.arg(escapeHtml(media.filename));
	} else {
		html += QString("<img src=\"%1\"").arg(dataUri);
		if (media.width > 0 && media.height > 0) {
			int displayWidth = qMin(media.width, 800);
			int displayHeight = media.height * displayWidth / media.width;
			html += QString(" width=\"%1\" height=\"%2\"")
				.arg(displayWidth)
				.arg(displayHeight);
		}
		html += " loading=\"lazy\">\n";
	}

	if (!caption.isEmpty()) {
		html += QString("<div class=\"caption\">%1</div>\n").arg(escapeHtml(caption));
	}

	html += "</div>\n";
	return html;
}

QString HtmlExporter::imageToBase64DataUri(const QByteArray &data, const QString &mimeType) {
	return QString("data:%1;base64,%2")
		.arg(mimeType.isEmpty() ? "application/octet-stream" : mimeType)
		.arg(QString::fromLatin1(data.toBase64()));
}

QString HtmlExporter::generateCss() {
	return R"(
body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    max-width: 900px;
    margin: 0 auto;
    padding: 20px;
    background: #f5f5f5;
    color: #333;
}
h1 {
    color: #2196F3;
    border-bottom: 2px solid #2196F3;
    padding-bottom: 10px;
}
.meta {
    color: #666;
    font-size: 0.9em;
}
.messages {
    margin-top: 20px;
}
.message {
    background: white;
    margin: 10px 0;
    padding: 15px;
    border-radius: 8px;
    border-left: 4px solid #ddd;
    box-shadow: 0 1px 3px rgba(0,0,0,0.1);
}
.message.outgoing {
    border-left-color: #4CAF50;
    background: #f0fff0;
}
.header {
    margin-bottom: 8px;
}
.from {
    font-weight: bold;
    color: #2196F3;
}
.username {
    color: #888;
    font-size: 0.9em;
}
.date {
    color: #888;
    font-size: 0.85em;
    float: right;
}
.reply-to {
    background: #f0f0f0;
    padding: 5px 10px;
    border-left: 3px solid #999;
    margin-bottom: 8px;
    font-size: 0.9em;
    color: #666;
}
.content {
    line-height: 1.5;
    word-wrap: break-word;
}
.media {
    margin: 10px 0;
}
.media img {
    max-width: 100%;
    height: auto;
    border-radius: 4px;
}
.media video {
    max-width: 100%;
    border-radius: 4px;
}
.document {
    background: #e8f4fc;
    padding: 10px 15px;
    border-radius: 4px;
}
.document a {
    color: #2196F3;
    text-decoration: none;
}
.caption {
    color: #666;
    font-size: 0.9em;
    margin-top: 5px;
}
code {
    background: #f0f0f0;
    padding: 2px 6px;
    border-radius: 3px;
    font-family: 'SF Mono', Monaco, monospace;
}
pre {
    background: #2d2d2d;
    color: #f8f8f2;
    padding: 15px;
    border-radius: 4px;
    overflow-x: auto;
    font-family: 'SF Mono', Monaco, monospace;
}
blockquote {
    border-left: 3px solid #ccc;
    padding-left: 15px;
    margin-left: 0;
    color: #666;
}
.spoiler {
    background: #333;
    color: #333;
    cursor: pointer;
    padding: 0 4px;
    border-radius: 2px;
    transition: all 0.2s;
}
.spoiler:hover {
    color: white;
}
a {
    color: #2196F3;
}
)";
}

QString HtmlExporter::formatDate(qint64 timestamp) {
	return QDateTime::fromSecsSinceEpoch(timestamp).toString("yyyy-MM-dd HH:mm");
}

QString HtmlExporter::formatTime(qint64 timestamp) {
	return QDateTime::fromSecsSinceEpoch(timestamp).toString("HH:mm");
}

} // namespace MCP
