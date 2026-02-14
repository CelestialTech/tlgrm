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

struct HtmlExportOptions {
	bool embedMedia = true;
	int maxMediaSizeMB = 50;
	bool includeReplies = true;
	bool includeForwards = true;
	bool respectContentRestrictions = false;
	QDateTime startDate;
	QDateTime endDate;
};

struct MediaData {
	QByteArray data;
	QString mimeType;
	QString filename;
	qint64 size = 0;
	int width = 0;
	int height = 0;
	bool isVideo = false;
	bool isDocument = false;
	bool downloaded = false;
};

class HtmlExporter : public QObject {
	Q_OBJECT

public:
	explicit HtmlExporter(QObject *parent = nullptr);
	~HtmlExporter();

	void setDataSession(Data::Session *session) { _dataSession = session; }

	bool exportChat(
		qint64 chatId,
		const QString &outputPath,
		const HtmlExportOptions &options = HtmlExportOptions{});

	bool exportFromArchive(
		const QString &chatTitle,
		const QJsonArray &messages,
		const QString &outputPath,
		const HtmlExportOptions &options = HtmlExportOptions{});

	QString lastError() const { return _lastError; }
	int exportedCount() const { return _exportedCount; }

Q_SIGNALS:
	void progressChanged(int current, int total);
	void exportFinished(bool success, const QString &path);
	void error(const QString &message);

private:
	QString generateHtml(
		const QString &chatTitle,
		const QJsonArray &messages,
		const HtmlExportOptions &options);

	QString formatMessageHtml(const QJsonObject &message);
	QString formatTextWithEntities(const QString &text, const QJsonArray &entities);
	QString escapeHtml(const QString &text);

	MediaData downloadMedia(HistoryItem *item);
	QString embedMediaHtml(const MediaData &media, const QString &caption);
	QString imageToBase64DataUri(const QByteArray &data, const QString &mimeType);

	QString generateCss();
	QString formatDate(qint64 timestamp);
	QString formatTime(qint64 timestamp);

	Data::Session *_dataSession = nullptr;
	QString _lastError;
	int _exportedCount = 0;
};

} // namespace MCP
