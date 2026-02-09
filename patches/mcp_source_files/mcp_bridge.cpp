// MCP Bridge - IPC service implementation
//
// This file is part of Telegram Desktop MCP integration,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_bridge.h"

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
	} else {
		// Unknown method
		response["error"] = QJsonObject{
			{"code", -32601},
			{"message", "Method not found: " + method}
		};
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
	// TODO: Implement local database access
	// This will query tdesktop's SQLite database directly
	// For now, return stub data

	qint64 chatId = params["chat_id"].toVariant().toLongLong();
	int limit = params["limit"].toInt(50);
	int offset = params["offset"].toInt(0);

	qDebug() << "MCP Bridge: get_messages"
		<< "chat_id=" << chatId
		<< "limit=" << limit
		<< "offset=" << offset;

	QJsonObject result;
	result["chat_id"] = chatId;
	result["messages"] = QJsonArray{}; // TODO: Populate from database
	result["total"] = 0;
	result["source"] = "local_database";
	result["note"] = "Implementation pending - will access local SQLite";

	return result;
}

QJsonObject Bridge::handleSearchLocal(const QJsonObject &params) {
	// TODO: Implement local search
	// This will search tdesktop's local message cache

	QString query = params["query"].toString();
	qint64 chatId = params["chat_id"].toVariant().toLongLong();
	int limit = params["limit"].toInt(50);

	qDebug() << "MCP Bridge: search_local"
		<< "query=" << query
		<< "chat_id=" << chatId
		<< "limit=" << limit;

	QJsonObject result;
	result["query"] = query;
	result["results"] = QJsonArray{}; // TODO: Populate from search
	result["total"] = 0;
	result["source"] = "local_cache";
	result["note"] = "Implementation pending - will search local database";

	return result;
}

QJsonObject Bridge::handleGetDialogs(const QJsonObject &params) {
	// TODO: Implement dialog list access
	// This will return all chats/dialogs from tdesktop

	int limit = params["limit"].toInt(100);
	int offset = params["offset"].toInt(0);

	qDebug() << "MCP Bridge: get_dialogs"
		<< "limit=" << limit
		<< "offset=" << offset;

	QJsonObject result;
	result["dialogs"] = QJsonArray{}; // TODO: Populate from tdesktop
	result["total"] = 0;
	result["source"] = "local_cache";
	result["note"] = "Implementation pending - will access tdesktop's dialog list";

	return result;
}

} // namespace MCP
