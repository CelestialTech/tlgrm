// MCP Server implementation
//
// This file is part of Telegram Desktop MCP integration,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server.h"

#include <QtCore/QTimer>
#include <QtCore/QDebug>

namespace MCP {

Server::Server(QObject *parent)
	: QObject(parent) {
	initializeCapabilities();
	registerTools();
	registerResources();
	registerPrompts();
}

Server::~Server() {
	stop();
}

void Server::initializeCapabilities() {
	_serverInfo.capabilities = QJsonObject{
		{"tools", QJsonObject{{"listChanged", true}}},
		{"resources", QJsonObject{{"listChanged", true}}},
		{"prompts", QJsonObject{{"listChanged", true}}},
	};
}

void Server::registerTools() {
	_tools = {
		Tool{
			"list_chats",
			"Get a list of all Telegram chats (direct access to local database)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"get_chat_info",
			"Get detailed information about a specific chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Telegram chat ID"}
					}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"read_messages",
			"Read messages from local database (instant, no API calls!)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"description", "Number of messages"},
						{"default", 50}
					}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"send_message",
			"Send a message to a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"text", QJsonObject{
						{"type", "string"},
						{"description", "Message text"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "text"}},
			}
		},
		Tool{
			"search_messages",
			"Search messages in local database (semantic search coming soon)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"query", QJsonObject{
						{"type", "string"},
						{"description", "Search query"}
					}},
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Optional: limit to specific chat"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"default", 50}
					}}
				}},
				{"required", QJsonArray{"query"}},
			}
		},
	};
}

void Server::registerResources() {
	_resources = {
		Resource{
			"telegram://chats",
			"All Chats",
			"List of all Telegram chats",
			"application/json"
		},
		Resource{
			"telegram://messages/{chat_id}",
			"Chat Messages",
			"Messages from a specific chat",
			"application/json"
		},
	};
}

void Server::registerPrompts() {
	_prompts = {
		Prompt{
			"summarize_chat",
			"Analyze and summarize recent messages in a chat",
			QJsonArray{
				QJsonObject{
					{"name", "chat_id"},
					{"description", "Chat ID to summarize"},
					{"required", true}
				},
				QJsonObject{
					{"name", "limit"},
					{"description", "Number of messages to analyze"},
					{"required", false}
				}
			}
		},
	};
}

bool Server::start(TransportType transport) {
	if (_initialized) {
		return true;
	}

	_transport = transport;

	switch (_transport) {
	case TransportType::Stdio:
		startStdioTransport();
		break;
	case TransportType::HTTP:
		startHttpTransport();
		break;
	default:
		qWarning() << "MCP: Unsupported transport type";
		return false;
	}

	_initialized = true;
	qInfo() << "MCP Server started (transport:"
		<< (_transport == TransportType::Stdio ? "stdio" : "http")
		<< ")";

	return true;
}

void Server::stop() {
	if (!_initialized) {
		return;
	}

	delete _stdin;
	delete _stdout;
	delete _httpServer;

	_stdin = nullptr;
	_stdout = nullptr;
	_httpServer = nullptr;

	_initialized = false;
	qInfo() << "MCP Server stopped";
}

void Server::startStdioTransport() {
	_stdin = new QTextStream(stdin);
	_stdout = new QTextStream(stdout);

	// Use a timer to poll stdin (Qt doesn't have native stdin notifications)
	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &Server::handleStdioInput);
	timer->start(100); // Poll every 100ms
}

void Server::handleStdioInput() {
	if (!_stdin->atEnd()) {
		QString line = _stdin->readLine();

		if (line.isEmpty()) {
			return;
		}

		// Parse JSON-RPC request
		QJsonParseError error;
		QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);

		if (error.error != QJsonParseError::NoError) {
			qWarning() << "MCP: JSON parse error:" << error.errorString();
			return;
		}

		QJsonObject request = doc.object();
		QJsonObject response = handleRequest(request);

		// Write response to stdout
		*_stdout << QJsonDocument(response).toJson(QJsonDocument::Compact);
		*_stdout << "\n";
		_stdout->flush();
	}
}

void Server::startHttpTransport(int port) {
	// TODO: Implement HTTP transport with QHttpServer (Qt 6)
	// This would enable SSE notifications and web-based clients
	qInfo() << "MCP: HTTP transport on port" << port << "(not implemented yet)";
}

QJsonObject Server::handleRequest(const QJsonObject &request) {
	QString method = request["method"].toString();
	QJsonObject params = request["params"].toObject();
	QJsonValue id = request["id"];

	qDebug() << "MCP: Request" << method;

	// Dispatch to method handlers
	if (method == "initialize") {
		return successResponse(id, handleInitialize(params));
	} else if (method == "tools/list") {
		return successResponse(id, handleListTools(params));
	} else if (method == "tools/call") {
		return successResponse(id, handleCallTool(params));
	} else if (method == "resources/list") {
		return successResponse(id, handleListResources(params));
	} else if (method == "resources/read") {
		return successResponse(id, handleReadResource(params));
	} else if (method == "prompts/list") {
		return successResponse(id, handleListPrompts(params));
	} else if (method == "prompts/get") {
		return successResponse(id, handleGetPrompt(params));
	} else {
		return errorResponse(id, -32601, "Method not found: " + method);
	}
}

QJsonObject Server::handleInitialize(const QJsonObject &params) {
	Q_UNUSED(params);

	return QJsonObject{
		{"protocolVersion", "2024-11-05"},
		{"serverInfo", QJsonObject{
			{"name", _serverInfo.name},
			{"version", _serverInfo.version}
		}},
		{"capabilities", _serverInfo.capabilities},
	};
}

QJsonObject Server::handleListTools(const QJsonObject &params) {
	Q_UNUSED(params);

	QJsonArray tools;
	for (const auto &tool : _tools) {
		tools.append(QJsonObject{
			{"name", tool.name},
			{"description", tool.description},
			{"inputSchema", tool.inputSchema},
		});
	}

	return QJsonObject{{"tools", tools}};
}

QJsonObject Server::handleCallTool(const QJsonObject &params) {
	QString name = params["name"].toString();
	QJsonObject arguments = params["arguments"].toObject();

	qDebug() << "MCP: Calling tool" << name << "with args" << arguments;

	// Dispatch to tool implementations
	QJsonObject result;

	if (name == "list_chats") {
		result = toolListChats(arguments);
	} else if (name == "get_chat_info") {
		result = toolGetChatInfo(arguments);
	} else if (name == "read_messages") {
		result = toolReadMessages(arguments);
	} else if (name == "send_message") {
		result = toolSendMessage(arguments);
	} else if (name == "search_messages") {
		result = toolSearchMessages(arguments);
	} else {
		// Unknown tool
		return QJsonObject{
			{"isError", true},
			{"content", QJsonArray{
				QJsonObject{
					{"type", "text"},
					{"text", "Unknown tool: " + name}
				}
			}}
		};
	}

	// Format as MCP tool response
	QJsonObject contentItem;
	contentItem["type"] = "text";
	contentItem["text"] = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));

	QJsonArray contentArray;
	contentArray.append(contentItem);

	QJsonObject response;
	response["content"] = contentArray;
	return response;
}

QJsonObject Server::handleListResources(const QJsonObject &params) {
	Q_UNUSED(params);

	QJsonArray resources;
	for (const auto &resource : _resources) {
		resources.append(QJsonObject{
			{"uri", resource.uri},
			{"name", resource.name},
			{"description", resource.description},
			{"mimeType", resource.mimeType},
		});
	}

	return QJsonObject{{"resources", resources}};
}

QJsonObject Server::handleReadResource(const QJsonObject &params) {
	QString uri = params["uri"].toString();

	// TODO: Implement resource reading
	return QJsonObject{
		{"uri", uri},
		{"mimeType", "application/json"},
		{"text", "{}"},
	};
}

QJsonObject Server::handleListPrompts(const QJsonObject &params) {
	Q_UNUSED(params);

	QJsonArray prompts;
	for (const auto &prompt : _prompts) {
		prompts.append(QJsonObject{
			{"name", prompt.name},
			{"description", prompt.description},
			{"arguments", prompt.arguments},
		});
	}

	return QJsonObject{{"prompts", prompts}};
}

QJsonObject Server::handleGetPrompt(const QJsonObject &params) {
	QString name = params["name"].toString();

	// TODO: Generate prompt based on arguments
	return QJsonObject{
		{"description", "Prompt: " + name},
		{"messages", QJsonArray{}},
	};
}

// Tool implementations (stubs - will connect to real tdesktop data)

QJsonObject Server::toolListChats(const QJsonObject &args) {
	Q_UNUSED(args);

	// TODO: Access tdesktop's dialog list
	// Example: _session->data().chatsListFor(Data::Folder::kAll)

	return QJsonObject{
		{"chats", QJsonArray{}},
		{"source", "local_database"},
		{"note", "Will access tdesktop's local chat list"},
	};
}

QJsonObject Server::toolGetChatInfo(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	// TODO: Get chat from tdesktop
	// Example: _session->data().peer(peerFromUser(chatId))

	return QJsonObject{
		{"chat_id", chatId},
		{"source", "local_database"},
		{"note", "Will access tdesktop's chat data"},
	};
}

QJsonObject Server::toolReadMessages(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args["limit"].toInt(50);

	// TODO: Access tdesktop's message history
	// Example: _session->data().history(peer)->messages()

	qDebug() << "MCP: read_messages chat_id=" << chatId << "limit=" << limit;

	return QJsonObject{
		{"chat_id", chatId},
		{"messages", QJsonArray{}},
		{"source", "local_database"},
		{"note", "Will access tdesktop's SQLite message cache - INSTANT!"},
	};
}

QJsonObject Server::toolSendMessage(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString text = args["text"].toString();

	// TODO: Send message via tdesktop
	// Example: _session->api().sendMessage(peer, text)

	return QJsonObject{
		{"chat_id", chatId},
		{"text", text},
		{"status", "pending"},
		{"note", "Will use tdesktop's message sending"},
	};
}

QJsonObject Server::toolSearchMessages(const QJsonObject &args) {
	QString query = args["query"].toString();

	// TODO: Search tdesktop's local database
	// Future: Semantic search with FAISS

	return QJsonObject{
		{"query", query},
		{"results", QJsonArray{}},
		{"source", "local_database"},
		{"note", "Will search tdesktop's local message cache"},
	};
}

QJsonObject Server::errorResponse(
	const QJsonValue &id,
	int code,
	const QString &message
) {
	return QJsonObject{
		{"jsonrpc", "2.0"},
		{"id", id},
		{"error", QJsonObject{
			{"code", code},
			{"message", message}
		}},
	};
}

QJsonObject Server::successResponse(
	const QJsonValue &id,
	const QJsonObject &result
) {
	return QJsonObject{
		{"jsonrpc", "2.0"},
		{"id", id},
		{"result", result},
	};
}

} // namespace MCP
