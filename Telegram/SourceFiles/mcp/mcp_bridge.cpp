// MCP Bridge - IPC service implementation
//
// This file is part of Telegram Desktop MCP integration,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_bridge.h"
#include "mcp_server.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QFile>
#include <QtCore/QDebug>

namespace MCP {

Bridge::Bridge(QObject *parent)
	: QObject(parent)
	, _server(new QLocalServer(this)) {
}

Bridge::~Bridge() {
	stop();
}

bool Bridge::start(const QString &socketPath) {
	if (_server->isListening()) {
		return true;
	}

	_socketPath = socketPath;

	// Remove existing socket file if it exists
	QFile::remove(_socketPath);

	// Start listening
	if (!_server->listen(_socketPath)) {
		qWarning() << "MCP Bridge: Failed to start server on" << _socketPath;
		qWarning() << "Error:" << _server->errorString();
		return false;
	}

	connect(_server, &QLocalServer::newConnection,
		this, &Bridge::onNewConnection);

	qInfo() << "MCP Bridge: Server started on" << _socketPath;
	return true;
}

void Bridge::stop() {
	if (_server->isListening()) {
		_server->close();
		QFile::remove(_socketPath);
		qInfo() << "MCP Bridge: Server stopped";
	}
}

bool Bridge::isRunning() const {
	return _server && _server->isListening();
}

void Bridge::setServer(Server *server) {
	_mcpServer = server;
	qInfo() << "MCP Bridge: MCP server connected";
}

void Bridge::onNewConnection() {
	QLocalSocket *socket = _server->nextPendingConnection();
	if (!socket) {
		return;
	}

	qDebug() << "MCP Bridge: New connection";

	connect(socket, &QLocalSocket::readyRead,
		this, &Bridge::onReadyRead);
	connect(socket, &QLocalSocket::disconnected,
		this, &Bridge::onDisconnected);
}

void Bridge::onReadyRead() {
	QLocalSocket *socket = qobject_cast<QLocalSocket*>(sender());
	if (!socket) {
		return;
	}

	// Read all available data
	QByteArray data = socket->readAll();

	// Parse JSON-RPC request
	QJsonParseError parseError;
	QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

	if (parseError.error != QJsonParseError::NoError) {
		qWarning() << "MCP Bridge: JSON parse error:" << parseError.errorString();

		// Send error response
		QJsonObject error;
		error["id"] = QJsonValue::Null;
		error["error"] = QJsonObject{
			{"code", -32700},
			{"message", "Parse error"}
		};

		socket->write(QJsonDocument(error).toJson(QJsonDocument::Compact));
		socket->write("\n");
		socket->flush();
		return;
	}

	QJsonObject request = doc.object();
	qDebug() << "MCP Bridge: Request:" << request;

	// Handle command
	QJsonObject response = handleCommand(request);

	// Send response
	socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
	socket->write("\n");
	socket->flush();
}

void Bridge::onDisconnected() {
	QLocalSocket *socket = qobject_cast<QLocalSocket*>(sender());
	if (socket) {
		socket->deleteLater();
		qDebug() << "MCP Bridge: Connection closed";
	}
}

QJsonObject Bridge::handleCommand(const QJsonObject &request) {
	QString method = request["method"].toString();
	QJsonObject params = request["params"].toObject();
	QJsonValue requestId = request["id"];

	QJsonObject response;
	response["id"] = requestId;

	// Dispatch to appropriate handler
	QJsonObject result;

	if (method == "ping") {
		result = handlePing(params);
	} else if (method == "get_messages") {
		result = handleGetMessages(params);
	} else if (method == "search_local") {
		result = handleSearchLocal(params);
	} else if (method == "get_dialogs") {
		result = handleGetDialogs(params);
	} else if (method == "tools/list") {
		// MCP protocol method: list available tools
		if (_mcpServer) {
			result = _mcpServer->handleListTools(params);
		} else {
			QJsonObject error;
			error["code"] = -32603;
			error["message"] = "MCP server not connected";
			response["error"] = error;
			return response;
		}
	} else if (method == "tools/call") {
		// MCP protocol method: call a tool by name
		if (_mcpServer) {
			result = _mcpServer->handleCallTool(params);
		} else {
			QJsonObject error;
			error["code"] = -32603;
			error["message"] = "MCP server not connected";
			response["error"] = error;
			return response;
		}
	} else if (method == "initialize") {
		// MCP protocol method: initialize
		if (_mcpServer) {
			result = _mcpServer->handleInitialize(params);
		} else {
			QJsonObject error;
			error["code"] = -32603;
			error["message"] = "MCP server not connected";
			response["error"] = error;
			return response;
		}
	} else if (_mcpServer) {
		// Try to dispatch to MCP server's tool handlers
		result = _mcpServer->callTool(method, params);
		if (result.contains("error") && result["error"].toString() == "tool_not_found") {
			// Tool not found in MCP server either
			QJsonObject error;
			error["code"] = -32601;
			error["message"] = "Method not found: " + method;
			response["error"] = error;
			return response;
		}
	} else {
		// No MCP server connected and unknown method
		QJsonObject error;
		error["code"] = -32601;
		error["message"] = "Method not found: " + method;
		response["error"] = error;
		return response;
	}

	response["result"] = result;
	return response;
}

QJsonObject Bridge::handlePing(const QJsonObject &params) {
	Q_UNUSED(params);

	QJsonObject result;
	result["status"] = "pong";
	result["version"] = "0.1.0";
	result["features"] = QJsonArray{
		"local_database",
		"voice_transcription",
		"semantic_search",
		"media_processing"
	};

	return result;
}

QJsonObject Bridge::handleGetMessages(const QJsonObject &params) {
	if (!_mcpServer) {
		QJsonObject error;
		error["error"] = "MCP server not connected";
		return error;
	}

	qint64 chatId = params["chat_id"].toVariant().toLongLong();
	int limit = params["limit"].toInt(50);

	qDebug() << "MCP Bridge: get_messages (delegating to MCP server)"
		<< "chat_id=" << chatId
		<< "limit=" << limit;

	// Delegate to MCP server's read_messages tool
	QJsonObject args;
	args["chat_id"] = chatId;
	args["limit"] = limit;

	return _mcpServer->toolReadMessages(args);
}

QJsonObject Bridge::handleSearchLocal(const QJsonObject &params) {
	if (!_mcpServer) {
		QJsonObject error;
		error["error"] = "MCP server not connected";
		return error;
	}

	QString query = params["query"].toString();
	qint64 chatId = params["chat_id"].toVariant().toLongLong();
	int limit = params["limit"].toInt(50);

	qDebug() << "MCP Bridge: search_local (delegating to MCP server)"
		<< "query=" << query
		<< "chat_id=" << chatId
		<< "limit=" << limit;

	// Delegate to MCP server's search_messages tool
	QJsonObject args;
	args["query"] = query;
	if (chatId != 0) {
		args["chat_id"] = chatId;
	}
	args["limit"] = limit;

	return _mcpServer->toolSearchMessages(args);
}

QJsonObject Bridge::handleGetDialogs(const QJsonObject &params) {
	if (!_mcpServer) {
		QJsonObject error;
		error["error"] = "MCP server not connected";
		return error;
	}

	qDebug() << "MCP Bridge: get_dialogs (delegating to MCP server)";

	// Delegate to MCP server's list_chats tool
	QJsonObject args;  // list_chats doesn't need parameters

	return _mcpServer->toolListChats(args);
}

} // namespace MCP
