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
#include <QtCore/QHash>
#include <QtCore/QString>

namespace MCP {

class Server;  // Forward declaration

class Bridge : public QObject {
	Q_OBJECT

public:
	explicit Bridge(QObject *parent = nullptr);
	~Bridge();

	// Start the IPC server (uses secure path by default)
	bool start(const QString &socketPath = QString());

	// Stop the IPC server
	void stop();

	// Check if server is running
	bool isRunning() const;

	// Set MCP server for delegation
	void setServer(Server *server);

	// Get the auth token (for trusted clients to read)
	QString authToken() const { return _authToken; }

	// Get the actual socket path being used
	QString socketPath() const { return _socketPath; }

private Q_SLOTS:
	void onNewConnection();
	void onReadyRead();
	void onDisconnected();

private:
	// Security
	static QString defaultSocketPath();
	bool verifyPeerCredentials(QLocalSocket *socket);
	void generateAuthToken();
	bool writeTokenFile();
	void removeTokenFile();

	// Handle incoming JSON-RPC command
	QJsonObject handleCommand(const QJsonObject &request, QLocalSocket *socket);

	// Command handlers
	QJsonObject handlePing(const QJsonObject &params);
	QJsonObject handleGetMessages(const QJsonObject &params);
	QJsonObject handleSearchLocal(const QJsonObject &params);
	QJsonObject handleGetDialogs(const QJsonObject &params);

	QLocalServer *_server = nullptr;
	QString _socketPath;
	Server *_mcpServer = nullptr;

	// Auth token — clients must send this in their first request
	QString _authToken;
	QString _tokenFilePath;

	// Per-connection state
	struct ConnectionState {
		bool authenticated = false;
	};
	QHash<QLocalSocket*, ConnectionState> _connections;
};

} // namespace MCP
