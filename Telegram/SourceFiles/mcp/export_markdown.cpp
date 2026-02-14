// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "mcp/export_markdown.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_user.h"
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
#include <QtCore/QFileInfo>

namespace MCP {

MarkdownExporter::MarkdownExporter(QObject *parent)
	: QObject(parent) {
}

MarkdownExporter::~MarkdownExporter() = default;

bool MarkdownExporter::exportChat(
		qint64 chatId,
		const QString &outputPath,
		const MarkdownExportOptions &options) {

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

	QString mediaFolder;
	if (options.includeMedia && options.createMediaFolder) {
		QFileInfo fileInfo(outputPath);
		mediaFolder = fileInfo.absolutePath() + "/" + fileInfo.baseName() + "_media";
		QDir().mkpath(mediaFolder);
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

			if (options.includeMedia && item->media()) {
				QString relativePath;
				QString mimeType;
				if (saveMedia(item, mediaFolder, relativePath, mimeType)) {
					msg["media_path"] = relativePath;
					msg["media_mime"] = mimeType;
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

	QString markdown = generateMarkdown(chatTitle, messages, mediaFolder, options);

	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		_lastError = QString("Cannot open file: %1").arg(outputPath);
		return false;
	}

	QTextStream out(&file);
	out.setEncoding(QStringConverter::Utf8);
	out << markdown;
	file.close();

	Q_EMIT exportFinished(true, outputPath);
	return true;
}

bool MarkdownExporter::exportFromArchive(
		const QString &chatTitle,
		const QJsonArray &messages,
		const QString &outputPath,
		const MarkdownExportOptions &options) {

	_exportedCount = 0;
	_lastError.clear();

	if (messages.isEmpty()) {
		_lastError = "No messages to export";
		return false;
	}

	QString mediaFolder;
	if (options.includeMedia && options.createMediaFolder) {
		QFileInfo fileInfo(outputPath);
		mediaFolder = fileInfo.absolutePath() + "/" + fileInfo.baseName() + "_media";
		QDir().mkpath(mediaFolder);
	}

	_exportedCount = messages.size();

	QString markdown = generateMarkdown(chatTitle, messages, mediaFolder, options);

	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		_lastError = QString("Cannot open file: %1").arg(outputPath);
		return false;
	}

	QTextStream out(&file);
	out.setEncoding(QStringConverter::Utf8);
	out << markdown;
	file.close();

	Q_EMIT exportFinished(true, outputPath);
	return true;
}

QString MarkdownExporter::generateMarkdown(
		const QString &chatTitle,
		const QJsonArray &messages,
		const QString &mediaFolder,
		const MarkdownExportOptions &options) {

	QString md;
	md += QString("# %1\n\n").arg(chatTitle);
	md += QString("*Exported: %1 | Messages: %2*\n\n")
		.arg(QDateTime::currentDateTime().toString(Qt::ISODate))
		.arg(messages.size());
	md += "---\n\n";

	QString currentDate;

	for (const auto &msgRef : messages) {
		QJsonObject msg = msgRef.toObject();
		qint64 timestamp = msg["date"].toVariant().toLongLong();
		QString msgDate = QDateTime::fromSecsSinceEpoch(timestamp).toString("yyyy-MM-dd");

		if (msgDate != currentDate) {
			md += QString("\n## %1\n\n").arg(msgDate);
			currentDate = msgDate;
		}

		md += formatMessageMarkdown(msg, mediaFolder, options.embedImagesAsBase64);
	}

	return md;
}

QString MarkdownExporter::formatMessageMarkdown(
		const QJsonObject &message,
		const QString &mediaFolder,
		bool embedImages) {

	QString md;
	qint64 timestamp = message["date"].toVariant().toLongLong();
	QString time = QDateTime::fromSecsSinceEpoch(timestamp).toString("HH:mm");

	QJsonObject from = message["from"].toObject();
	QString fromName = from["name"].toString();
	QString username = from["username"].toString();

	md += QString("**%1**").arg(fromName.isEmpty() ? "Unknown" : fromName);
	if (!username.isEmpty()) {
		md += QString(" (@%1)").arg(username);
	}
	md += QString(" *%1*\n\n").arg(time);

	if (message.contains("reply_to")) {
		md += QString("> *Reply to message #%1*\n\n").arg(message["reply_to"].toString());
	}

	QString text = message["text"].toString();
	if (!text.isEmpty()) {
		QJsonArray entities = message["entities"].toArray();
		md += formatTextWithEntities(text, entities);
		md += "\n\n";
	}

	if (message.contains("media_path")) {
		QString relativePath = message["media_path"].toString();
		QString mimeType = message["media_mime"].toString();

		if (mimeType.startsWith("image/")) {
			md += QString("![Image](%1)\n\n").arg(relativePath);
		} else if (mimeType.startsWith("video/")) {
			md += QString("[Video: %1](%1)\n\n").arg(relativePath);
		} else {
			md += QString("[File: %1](%1)\n\n").arg(relativePath);
		}
	}

	md += "---\n\n";
	return md;
}

QString MarkdownExporter::formatTextWithEntities(const QString &text, const QJsonArray &entities) {
	if (entities.isEmpty()) {
		return escapeMarkdown(text);
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
			result += escapeMarkdown(between);
		}

		QString entityText = text.mid(entity.offset, entity.length);

		if (entity.type == "bold") {
			result += "**" + entityText + "**";
		} else if (entity.type == "italic") {
			result += "*" + entityText + "*";
		} else if (entity.type == "code") {
			result += "`" + entityText + "`";
		} else if (entity.type == "pre") {
			result += "\n```\n" + entityText + "\n```\n";
		} else if (entity.type == "underline") {
			result += "<u>" + entityText + "</u>";
		} else if (entity.type == "strikethrough") {
			result += "~~" + entityText + "~~";
		} else if (entity.type == "spoiler") {
			result += "||" + entityText + "||";
		} else if (entity.type == "text_link") {
			result += QString("[%1](%2)").arg(entityText).arg(entity.url);
		} else if (entity.type == "url") {
			result += entityText;
		} else if (entity.type == "mention") {
			result += QString("[%1](https://t.me/%2)")
				.arg(entityText)
				.arg(entityText.mid(1));
		} else if (entity.type == "blockquote") {
			QStringList lines = entityText.split('\n');
			for (const QString &line : lines) {
				result += "> " + line + "\n";
			}
		} else {
			result += entityText;
		}

		lastEnd = entity.offset + entity.length;
	}

	if (lastEnd < text.length()) {
		result += escapeMarkdown(text.mid(lastEnd));
	}

	return result;
}

QString MarkdownExporter::escapeMarkdown(const QString &text) {
	QString result = text;
	result.replace("\\", "\\\\");
	result.replace("`", "\\`");
	result.replace("*", "\\*");
	result.replace("_", "\\_");
	result.replace("{", "\\{");
	result.replace("}", "\\}");
	result.replace("[", "\\[");
	result.replace("]", "\\]");
	result.replace("(", "\\(");
	result.replace(")", "\\)");
	result.replace("#", "\\#");
	result.replace("+", "\\+");
	result.replace("-", "\\-");
	result.replace(".", "\\.");
	result.replace("!", "\\!");
	return result;
}

bool MarkdownExporter::saveMedia(
		HistoryItem *item,
		const QString &mediaFolder,
		QString &relativePath,
		QString &mimeType) {

	if (!item || !item->media() || mediaFolder.isEmpty()) {
		return false;
	}

	const auto media = item->media();

	if (const auto document = media->document()) {
		QString localPath = document->filepath(true);

		if (!localPath.isEmpty() && QFile::exists(localPath)) {
			QString filename = document->filename();
			if (filename.isEmpty()) {
				filename = QString("file_%1").arg(document->id);
			}

			QString destPath = mediaFolder + "/" + filename;

			int counter = 1;
			while (QFile::exists(destPath)) {
				QFileInfo fi(filename);
				destPath = QString("%1/%2_%3.%4")
					.arg(mediaFolder)
					.arg(fi.baseName())
					.arg(counter++)
					.arg(fi.suffix());
			}

			if (QFile::copy(localPath, destPath)) {
				QFileInfo mediaFolderInfo(mediaFolder);
				relativePath = mediaFolderInfo.fileName() + "/" + QFileInfo(destPath).fileName();
				mimeType = document->mimeString();
				return true;
			}
		}
	} else if (const auto photo = media->photo()) {
		// Photo export requires async loading - skip for now
		// Photos are typically embedded in archive data already
		Q_UNUSED(photo);
	}

	return false;
}

QString MarkdownExporter::formatDate(qint64 timestamp) {
	return QDateTime::fromSecsSinceEpoch(timestamp).toString("yyyy-MM-dd HH:mm");
}

} // namespace MCP
