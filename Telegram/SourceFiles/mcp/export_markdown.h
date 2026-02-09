// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QByteArray>

namespace Data {
class Session;
} // namespace Data

class HistoryItem;

namespace MCP {

struct MarkdownExportOptions {
	bool includeMedia = true;
	bool embedImagesAsBase64 = false;
	bool createMediaFolder = true;
	bool includeReplies = true;
	bool includeForwards = true;
	QDateTime startDate;
	QDateTime endDate;
};

class MarkdownExporter : public QObject {
	Q_OBJECT

public:
	explicit MarkdownExporter(QObject *parent = nullptr);
	~MarkdownExporter();

	void setDataSession(Data::Session *session) { _dataSession = session; }

	bool exportChat(
		qint64 chatId,
		const QString &outputPath,
		const MarkdownExportOptions &options = MarkdownExportOptions{});

	bool exportFromArchive(
		const QString &chatTitle,
		const QJsonArray &messages,
		const QString &outputPath,
		const MarkdownExportOptions &options = MarkdownExportOptions{});

	QString lastError() const { return _lastError; }
	int exportedCount() const { return _exportedCount; }

Q_SIGNALS:
	void progressChanged(int current, int total);
	void exportFinished(bool success, const QString &path);
	void error(const QString &message);

private:
	QString generateMarkdown(
		const QString &chatTitle,
		const QJsonArray &messages,
		const QString &mediaFolder,
		const MarkdownExportOptions &options);

	QString formatMessageMarkdown(
		const QJsonObject &message,
		const QString &mediaFolder,
		bool embedImages);

	QString formatTextWithEntities(const QString &text, const QJsonArray &entities);
	QString escapeMarkdown(const QString &text);

	bool saveMedia(
		HistoryItem *item,
		const QString &mediaFolder,
		QString &relativePath,
		QString &mimeType);

	QString formatDate(qint64 timestamp);

	Data::Session *_dataSession = nullptr;
	QString _lastError;
	int _exportedCount = 0;
};

} // namespace MCP
