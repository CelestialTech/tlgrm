// MCP Bridge - IPC service for Python MCP Server
// Exposes Telegram Desktop features via Unix domain socket
//
// This file is part of Telegram Desktop MCP integration,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#pragma once

#include <QtCore/QObject>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace MCP {

class Bridge : public QObject {
	Q_OBJECT

public:
	explicit Bridge(QObject *parent = nullptr);
	~Bridge();

	// Start the IPC server
	bool start(const QString &socketPath = "/tmp/tdesktop_mcp.sock");

	// Stop the IPC server
	void stop();

	// Check if server is running
	bool isRunning() const;

private Q_SLOTS:
	void onNewConnection();
	void onReadyRead();
	void onDisconnected();

private:
	// Handle incoming JSON-RPC command
	QJsonObject handleCommand(const QJsonObject &request);

	// Command handlers
	QJsonObject handlePing(const QJsonObject &params);
	QJsonObject handleGetMessages(const QJsonObject &params);
	QJsonObject handleSearchLocal(const QJsonObject &params);
	QJsonObject handleGetDialogs(const QJsonObject &params);

	QLocalServer *_server = nullptr;
	QString _socketPath;
};

} // namespace MCP
