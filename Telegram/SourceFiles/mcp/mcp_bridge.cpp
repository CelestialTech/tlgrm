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
#include <QtCore/QDir>
#include <QtCore/QDebug>
#include <QtCore/QUuid>
#include <QtCore/QStandardPaths>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// macOS peer credentials
#ifdef Q_OS_MAC
#include <sys/ucred.h>
#endif

namespace MCP {

Bridge::Bridge(QObject *parent)
	: QObject(parent)
	, _server(new QLocalServer(this)) {
	generateAuthToken();
}

Bridge::~Bridge() {
	stop();
}

QString Bridge::defaultSocketPath() {
	// Use a user-private directory instead of world-writable /tmp
	const auto cacheDir = QStandardPaths::writableLocation(
		QStandardPaths::CacheLocation);
	const auto socketDir = cacheDir + "/mcp";

	QDir dir;
	if (!dir.mkpath(socketDir)) {
		qWarning() << "MCP Bridge: Failed to create socket directory" << socketDir;
		// Fallback to /tmp with restricted permissions
		return "/tmp/tdesktop_mcp.sock";
	}

	// Restrict directory to owner only (0700)
	::chmod(socketDir.toUtf8().constData(), 0700);

	return socketDir + "/bridge.sock";
}

void Bridge::generateAuthToken() {
	_authToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool Bridge::writeTokenFile() {
	// Write token to a file next to the socket so trusted clients can read it.
	// Use POSIX open() with 0600 mode to avoid the race between create and chmod.
	const auto dir = QFileInfo(_socketPath).absolutePath();
	_tokenFilePath = dir + "/auth_token";

	const int fd = ::open(
		_tokenFilePath.toUtf8().constData(),
		O_CREAT | O_WRONLY | O_TRUNC,
		0600);
	if (fd < 0) {
		qWarning() << "MCP Bridge: Failed to create token file" << _tokenFilePath;
		return false;
	}

	const auto tokenData = _authToken.toUtf8();
	const auto written = ::write(fd, tokenData.constData(), tokenData.size());
	::close(fd);

	if (written != tokenData.size()) {
		qWarning() << "MCP Bridge: Failed to write token";
		QFile::remove(_tokenFilePath);
		return false;
	}

	qInfo() << "MCP Bridge: Auth token written to" << _tokenFilePath;
	return true;
}

void Bridge::removeTokenFile() {
	if (!_tokenFilePath.isEmpty()) {
		QFile::remove(_tokenFilePath);
		_tokenFilePath.clear();
	}
}

bool Bridge::start(const QString &socketPath) {
	if (_server->isListening()) {
		return true;
	}

	_socketPath = socketPath.isEmpty() ? defaultSocketPath() : socketPath;

	// Remove existing socket file if it exists
	QFile::remove(_socketPath);

	// Start listening
	if (!_server->listen(_socketPath)) {
		qWarning() << "MCP Bridge: Failed to start server on" << _socketPath;
		qWarning() << "Error:" << _server->errorString();
		return false;
	}

	// Restrict socket file to owner only (0600) — immediately after creation
	::chmod(_socketPath.toUtf8().constData(), 0600);

	// Write socket path to a discoverable config file (NOT a /tmp symlink,
	// which would let any user on the system reach our socket).
	{
		const auto configDir = QStandardPaths::writableLocation(
			QStandardPaths::ConfigLocation);
		const auto configPath = configDir + "/tdesktop/mcp_socket_path";
		QDir().mkpath(QFileInfo(configPath).absolutePath());
		QFile configFile(configPath);
		if (configFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			configFile.write(_socketPath.toUtf8());
			configFile.close();
			::chmod(configPath.toUtf8().constData(), 0600);
		}
	}

	// Write auth token file
	writeTokenFile();

	connect(_server, &QLocalServer::newConnection,
		this, &Bridge::onNewConnection);

	qInfo() << "MCP Bridge: Server started on" << _socketPath;
	qInfo() << "MCP Bridge: Auth required — token in" << _tokenFilePath;
	return true;
}

void Bridge::stop() {
	if (_server->isListening()) {
		_server->close();
		QFile::remove(_socketPath);
		removeTokenFile();
		_connections.clear();
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

bool Bridge::verifyPeerCredentials(QLocalSocket *socket) {
#ifdef Q_OS_MAC
	// Verify connecting process runs as same user (macOS)
	const auto fd = socket->socketDescriptor();
	if (fd < 0) {
		qWarning() << "MCP Bridge: Invalid socket descriptor";
		return false;
	}

	struct xucred peercred = {};
	peercred.cr_version = XUCRED_VERSION;
	socklen_t len = sizeof(peercred);
	if (::getsockopt(fd, SOL_LOCAL, LOCAL_PEERCRED, &peercred, &len) != 0) {
		qWarning() << "MCP Bridge: Failed to get peer credentials — rejecting";
		return false;
	}

	if (peercred.cr_version != XUCRED_VERSION) {
		qWarning() << "MCP Bridge: Invalid xucred version — rejecting";
		return false;
	}

	const auto peerUid = peercred.cr_uid;
	const auto ourUid = ::getuid();
	if (peerUid != ourUid) {
		qWarning() << "MCP Bridge: Rejected connection from UID"
			<< peerUid << "(our UID:" << ourUid << ")";
		return false;
	}

	return true;
#else
	// On other platforms, allow (socket permissions are the primary guard)
	Q_UNUSED(socket);
	return true;
#endif
}

void Bridge::onNewConnection() {
	QLocalSocket *socket = _server->nextPendingConnection();
	if (!socket) {
		return;
	}

	// UID verification
	if (!verifyPeerCredentials(socket)) {
		socket->disconnectFromServer();
		socket->deleteLater();
		return;
	}

	qDebug() << "MCP Bridge: New connection (authenticated UID)";

	// Initialize connection state
	_connections[socket] = ConnectionState{};

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
		QJsonObject errObj;
		errObj["code"] = -32700;
		errObj["message"] = QStringLiteral("Parse error");
		error["error"] = errObj;

		socket->write(QJsonDocument(error).toJson(QJsonDocument::Compact));
		socket->write("\n");
		socket->flush();
		return;
	}

	QJsonObject request = doc.object();
	qDebug() << "MCP Bridge: Request:" << request;

	// Handle command (with per-connection auth state)
	QJsonObject response = handleCommand(request, socket);

	// Send response
	socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
	socket->write("\n");
	socket->flush();
}

void Bridge::onDisconnected() {
	QLocalSocket *socket = qobject_cast<QLocalSocket*>(sender());
	if (socket) {
		_connections.remove(socket);
		socket->deleteLater();
		qDebug() << "MCP Bridge: Connection closed";
	}
}

QJsonObject Bridge::handleCommand(const QJsonObject &request, QLocalSocket *socket) {
	QString method = request["method"].toString();
	QJsonObject params = request["params"].toObject();
	QJsonValue requestId = request["id"];

	QJsonObject response;
	response["jsonrpc"] = QStringLiteral("2.0");
	response["id"] = requestId;

	// Token authentication check
	// The "initialize" method must include the auth token.
	// Once authenticated, subsequent requests on this connection are trusted.
	auto &connState = _connections[socket];
	if (!connState.authenticated) {
		if (method == "initialize") {
			const auto token = params["auth_token"].toString();
			if (token.isEmpty() || token != _authToken) {
				QJsonObject errObj;
				errObj["code"] = -32001;
				errObj["message"] = QStringLiteral("Authentication required. "
					"Provide auth_token in initialize params. "
					"Token is in: ") + _tokenFilePath;
				response["error"] = errObj;
				return response;
			}
			connState.authenticated = true;
			// Fall through to handle initialize normally
		} else if (method == "ping") {
			// Allow unauthenticated ping (for health checks)
		} else {
			QJsonObject errObj;
			errObj["code"] = -32001;
			errObj["message"] = QStringLiteral("Not authenticated. "
				"Send initialize with auth_token first.");
			response["error"] = errObj;
			return response;
		}
	}

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
			QJsonObject errObj;
			errObj["code"] = -32603;
			errObj["message"] = QStringLiteral("MCP server not connected");
			response["error"] = errObj;
			return response;
		}
	} else if (method == "tools/call") {
		// MCP protocol method: call a tool by name
		if (_mcpServer) {
			result = _mcpServer->handleCallTool(params);
		} else {
			QJsonObject errObj;
			errObj["code"] = -32603;
			errObj["message"] = QStringLiteral("MCP server not connected");
			response["error"] = errObj;
			return response;
		}
	} else if (method == "initialize") {
		// MCP protocol method: initialize
		if (_mcpServer) {
			result = _mcpServer->handleInitialize(params);
		} else {
			QJsonObject errObj;
			errObj["code"] = -32603;
			errObj["message"] = QStringLiteral("MCP server not connected");
			response["error"] = errObj;
			return response;
		}
	} else if (_mcpServer) {
		// Try to dispatch to MCP server's tool handlers
		result = _mcpServer->callTool(method, params);
		if (result.contains("error") && result["error"].toString() == "tool_not_found") {
			// Tool not found in MCP server either
			QJsonObject errObj;
			errObj["code"] = -32601;
			errObj["message"] = "Method not found: " + method;
			response["error"] = errObj;
			return response;
		}
	} else {
		// No MCP server connected and unknown method
		QJsonObject errObj;
		errObj["code"] = -32601;
		errObj["message"] = "Method not found: " + method;
		response["error"] = errObj;
		return response;
	}

	response["result"] = result;
	return response;
}

QJsonObject Bridge::handlePing(const QJsonObject &params) {
	Q_UNUSED(params);

	QJsonObject result;
	result["status"] = QStringLiteral("pong");
	result["version"] = QStringLiteral("0.2.0");
	result["auth_required"] = true;

	QJsonArray features;
	features.append(QStringLiteral("local_database"));
	features.append(QStringLiteral("voice_transcription"));
	features.append(QStringLiteral("semantic_search"));
	features.append(QStringLiteral("media_processing"));
	result["features"] = features;

	return result;
}

QJsonObject Bridge::handleGetMessages(const QJsonObject &params) {
	if (!_mcpServer) {
		QJsonObject error;
		error["error"] = QStringLiteral("MCP server not connected");
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
		error["error"] = QStringLiteral("MCP server not connected");
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
		error["error"] = QStringLiteral("MCP server not connected");
		return error;
	}

	Q_UNUSED(params);
	qDebug() << "MCP Bridge: get_dialogs (delegating to MCP server)";

	// Delegate to MCP server's list_chats tool
	QJsonObject args;  // list_chats doesn't need parameters

	return _mcpServer->toolListChats(args);
}

} // namespace MCP
