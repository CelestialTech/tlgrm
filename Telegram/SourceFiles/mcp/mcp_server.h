// MCP Server - Model Context Protocol implementation in C++
// Native MCP server embedded in Telegram Desktop
//
// This file is part of Telegram Desktop MCP integration,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#pragma once

#include <QtCore/QObject>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QTextStream>
#include <QtNetwork/QTcpServer>

namespace MCP {

// MCP Protocol types
enum class TransportType {
	Stdio,    // Standard input/output (default for Claude Desktop)
	HTTP,     // HTTP with SSE for notifications
	WebSocket // WebSocket (future)
};

// MCP Tool definition
struct Tool {
	QString name;
	QString description;
	QJsonObject inputSchema;
};

// MCP Resource definition
struct Resource {
	QString uri;
	QString name;
	QString description;
	QString mimeType;
};

// MCP Prompt definition
struct Prompt {
	QString name;
	QString description;
	QJsonArray arguments;
};

class Server : public QObject {
	Q_OBJECT

public:
	explicit Server(QObject *parent = nullptr);
	~Server();

	// Start MCP server
	bool start(TransportType transport = TransportType::Stdio);

	// Stop MCP server
	void stop();

	// Server info
	struct ServerInfo {
		QString name = "Telegram Desktop MCP";
		QString version = "1.0.0";
		QJsonObject capabilities;
	};

	ServerInfo serverInfo() const { return _serverInfo; }

private:
	// Initialize server capabilities
	void initializeCapabilities();

	// Register tools, resources, prompts
	void registerTools();
	void registerResources();
	void registerPrompts();

	// Handle incoming JSON-RPC requests
	QJsonObject handleRequest(const QJsonObject &request);

	// JSON-RPC method handlers
	QJsonObject handleInitialize(const QJsonObject &params);
	QJsonObject handleListTools(const QJsonObject &params);
	QJsonObject handleCallTool(const QJsonObject &params);
	QJsonObject handleListResources(const QJsonObject &params);
	QJsonObject handleReadResource(const QJsonObject &params);
	QJsonObject handleListPrompts(const QJsonObject &params);
	QJsonObject handleGetPrompt(const QJsonObject &params);

	// Tool implementations
	QJsonObject toolListChats(const QJsonObject &args);
	QJsonObject toolGetChatInfo(const QJsonObject &args);
	QJsonObject toolReadMessages(const QJsonObject &args);
	QJsonObject toolSendMessage(const QJsonObject &args);
	QJsonObject toolSearchMessages(const QJsonObject &args);
	QJsonObject toolGetUserInfo(const QJsonObject &args);

	// Stdio transport
	void startStdioTransport();
	void handleStdioInput();

	// HTTP transport
	void startHttpTransport(int port = 8000);

	// Error response helper
	QJsonObject errorResponse(
		const QJsonValue &id,
		int code,
		const QString &message
	);

	// Success response helper
	QJsonObject successResponse(
		const QJsonValue &id,
		const QJsonObject &result
	);

private:
	ServerInfo _serverInfo;
	TransportType _transport = TransportType::Stdio;

	// Registered MCP components
	QVector<Tool> _tools;
	QVector<Resource> _resources;
	QVector<Prompt> _prompts;

	// Transports
	QTextStream *_stdin = nullptr;
	QTextStream *_stdout = nullptr;
	QTcpServer *_httpServer = nullptr;

	// State
	bool _initialized = false;
};

} // namespace MCP
