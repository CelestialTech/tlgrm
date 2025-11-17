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
#include <QtSql/QSqlDatabase>

namespace MCP {

// Forward declarations
class ChatArchiver;
class EphemeralArchiver;
class Analytics;
class SemanticSearch;
class BatchOperations;
class MessageScheduler;
class AuditLogger;
class RBAC;
class VoiceTranscription;
class BotManager;

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

	// Core tool implementations (original 6)
	QJsonObject toolListChats(const QJsonObject &args);
	QJsonObject toolGetChatInfo(const QJsonObject &args);
	QJsonObject toolReadMessages(const QJsonObject &args);
	QJsonObject toolSendMessage(const QJsonObject &args);
	QJsonObject toolSearchMessages(const QJsonObject &args);
	QJsonObject toolGetUserInfo(const QJsonObject &args);

	// Archive tools (7 tools)
	QJsonObject toolArchiveChat(const QJsonObject &args);
	QJsonObject toolExportChat(const QJsonObject &args);
	QJsonObject toolListArchivedChats(const QJsonObject &args);
	QJsonObject toolGetArchiveStats(const QJsonObject &args);
	QJsonObject toolGetEphemeralMessages(const QJsonObject &args);
	QJsonObject toolSearchArchive(const QJsonObject &args);
	QJsonObject toolPurgeArchive(const QJsonObject &args);

	// Analytics tools (8 tools)
	QJsonObject toolGetMessageStats(const QJsonObject &args);
	QJsonObject toolGetUserActivity(const QJsonObject &args);
	QJsonObject toolGetChatActivity(const QJsonObject &args);
	QJsonObject toolGetTimeSeries(const QJsonObject &args);
	QJsonObject toolGetTopUsers(const QJsonObject &args);
	QJsonObject toolGetTopWords(const QJsonObject &args);
	QJsonObject toolExportAnalytics(const QJsonObject &args);
	QJsonObject toolGetTrends(const QJsonObject &args);

	// Semantic search tools (5 tools)
	QJsonObject toolSemanticSearch(const QJsonObject &args);
	QJsonObject toolIndexMessages(const QJsonObject &args);
	QJsonObject toolDetectTopics(const QJsonObject &args);
	QJsonObject toolClassifyIntent(const QJsonObject &args);
	QJsonObject toolExtractEntities(const QJsonObject &args);

	// Message operations (6 tools)
	QJsonObject toolEditMessage(const QJsonObject &args);
	QJsonObject toolDeleteMessage(const QJsonObject &args);
	QJsonObject toolForwardMessage(const QJsonObject &args);
	QJsonObject toolPinMessage(const QJsonObject &args);
	QJsonObject toolUnpinMessage(const QJsonObject &args);
	QJsonObject toolAddReaction(const QJsonObject &args);

	// Batch operations (5 tools)
	QJsonObject toolBatchSend(const QJsonObject &args);
	QJsonObject toolBatchDelete(const QJsonObject &args);
	QJsonObject toolBatchForward(const QJsonObject &args);
	QJsonObject toolBatchPin(const QJsonObject &args);
	QJsonObject toolBatchReaction(const QJsonObject &args);

	// Scheduler tools (4 tools)
	QJsonObject toolScheduleMessage(const QJsonObject &args);
	QJsonObject toolCancelScheduled(const QJsonObject &args);
	QJsonObject toolListScheduled(const QJsonObject &args);
	QJsonObject toolUpdateScheduled(const QJsonObject &args);

	// System tools (4 tools)
	QJsonObject toolGetCacheStats(const QJsonObject &args);
	QJsonObject toolGetServerInfo(const QJsonObject &args);
	QJsonObject toolGetAuditLog(const QJsonObject &args);
	QJsonObject toolHealthCheck(const QJsonObject &args);

	// Voice tools (2 tools)
	QJsonObject toolTranscribeVoice(const QJsonObject &args);
	QJsonObject toolGetTranscription(const QJsonObject &args);

	// Bot framework tools (8 tools)
	QJsonObject toolListBots(const QJsonObject &args);
	QJsonObject toolGetBotInfo(const QJsonObject &args);
	QJsonObject toolStartBot(const QJsonObject &args);
	QJsonObject toolStopBot(const QJsonObject &args);
	QJsonObject toolConfigureBot(const QJsonObject &args);
	QJsonObject toolGetBotStats(const QJsonObject &args);
	QJsonObject toolSendBotCommand(const QJsonObject &args);
	QJsonObject toolGetBotSuggestions(const QJsonObject &args);

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

	// Feature components
	QSqlDatabase _db;
	ChatArchiver *_archiver = nullptr;
	EphemeralArchiver *_ephemeralArchiver = nullptr;
	Analytics *_analytics = nullptr;
	SemanticSearch *_semanticSearch = nullptr;
	BatchOperations *_batchOps = nullptr;
	MessageScheduler *_scheduler = nullptr;
	AuditLogger *_auditLogger = nullptr;
	RBAC *_rbac = nullptr;
	VoiceTranscription *_voiceTranscription = nullptr;
	BotManager *_botManager = nullptr;

	// State
	bool _initialized = false;
	QString _databasePath;
};

} // namespace MCP
