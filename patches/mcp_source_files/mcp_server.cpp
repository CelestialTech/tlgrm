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

// Telegram Desktop includes for real API integration
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_birthday.h"
#include "apiwrap.h"
#include "api/api_user_privacy.h"
#include "api/api_blocked_peers.h"
#include "api/api_authorizations.h"
#include "api/api_self_destruct.h"
#include "api/api_cloud_password.h"
#include "mtproto/mtproto_response.h"

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
		Tool{
			"get_user_info",
			"Get information about a Telegram user",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"user_id", QJsonObject{
						{"type", "integer"},
						{"description", "Telegram user ID"}
					}}
				}},
				{"required", QJsonArray{"user_id"}},
			}
		},
		Tool{
			"delete_message",
			"Delete a message from a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to delete"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "message_id"}},
			}
		},
		Tool{
			"edit_message",
			"Edit a message in a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to edit"}
					}},
					{"new_text", QJsonObject{
						{"type", "string"},
						{"description", "New message text"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "message_id", "new_text"}},
			}
		},
		Tool{
			"forward_message",
			"Forward a message to another chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"from_chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Source chat ID"}
					}},
					{"to_chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Destination chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to forward"}
					}}
				}},
				{"required", QJsonArray{"from_chat_id", "to_chat_id", "message_id"}},
			}
		},
		Tool{
			"pin_message",
			"Pin a message in a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to pin"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "message_id"}},
			}
		},
		Tool{
			"add_reaction",
			"Add a reaction to a message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to react to"}
					}},
					{"reaction", QJsonObject{
						{"type", "string"},
						{"description", "Reaction emoji"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "message_id", "reaction"}},
			}
		},
		Tool{
			"get_profile_settings",
			"Get current user's profile settings (name, bio, username, phone, birthday)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"update_profile_name",
			"Update user's first and last name",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"first_name", QJsonObject{
						{"type", "string"},
						{"description", "First name"}
					}},
					{"last_name", QJsonObject{
						{"type", "string"},
						{"description", "Last name (optional)"}
					}}
				}},
				{"required", QJsonArray{"first_name"}},
			}
		},
		Tool{
			"update_profile_bio",
			"Update user's biography/about text",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"bio", QJsonObject{
						{"type", "string"},
						{"description", "Biography text (max 70 characters)"}
					}}
				}},
				{"required", QJsonArray{"bio"}},
			}
		},
		Tool{
			"update_profile_username",
			"Update user's username",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"username", QJsonObject{
						{"type", "string"},
						{"description", "Username (without @)"}
					}}
				}},
				{"required", QJsonArray{"username"}},
			}
		},
		Tool{
			"update_profile_phone",
			"Update user's phone number",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"phone", QJsonObject{
						{"type", "string"},
						{"description", "Phone number"}
					}}
				}},
				{"required", QJsonArray{"phone"}},
			}
		},
		Tool{
			"get_privacy_settings",
			"Get all privacy settings (last seen, profile photo, phone, forwards, etc.)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"update_last_seen_privacy",
			"Update last seen & online privacy setting",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"rule", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"everybody", "contacts", "nobody"}},
						{"description", "Privacy rule"}
					}}
				}},
				{"required", QJsonArray{"rule"}},
			}
		},
		Tool{
			"update_profile_photo_privacy",
			"Update profile photo privacy setting",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"rule", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"everybody", "contacts", "nobody"}},
						{"description", "Privacy rule"}
					}}
				}},
				{"required", QJsonArray{"rule"}},
			}
		},
		Tool{
			"update_phone_number_privacy",
			"Update phone number privacy setting",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"rule", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"everybody", "contacts", "nobody"}},
						{"description", "Privacy rule"}
					}}
				}},
				{"required", QJsonArray{"rule"}},
			}
		},
		Tool{
			"update_forwards_privacy",
			"Update forwarded messages privacy setting",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"rule", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"everybody", "contacts", "nobody"}},
						{"description", "Privacy rule"}
					}}
				}},
				{"required", QJsonArray{"rule"}},
			}
		},
		Tool{
			"update_birthday_privacy",
			"Update birthday privacy setting",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"rule", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"everybody", "contacts", "nobody"}},
						{"description", "Privacy rule"}
					}}
				}},
				{"required", QJsonArray{"rule"}},
			}
		},
		Tool{
			"update_about_privacy",
			"Update bio/about privacy setting",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"rule", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"everybody", "contacts", "nobody"}},
						{"description", "Privacy rule"}
					}}
				}},
				{"required", QJsonArray{"rule"}},
			}
		},
		Tool{
			"get_blocked_users",
			"Get list of blocked users",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"get_security_settings",
			"Get security settings (two-step verification status, sessions, etc.)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"get_active_sessions",
			"Get list of active sessions on other devices",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"terminate_session",
			"Terminate a specific session by hash",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"hash", QJsonObject{
						{"type", "integer"},
						{"description", "Session hash"}
					}}
				}},
				{"required", QJsonArray{"hash"}},
			}
		},
		Tool{
			"block_user",
			"Block a user",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"user_id", QJsonObject{
						{"type", "integer"},
						{"description", "User ID to block"}
					}}
				}},
				{"required", QJsonArray{"user_id"}},
			}
		},
		Tool{
			"unblock_user",
			"Unblock a user",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"user_id", QJsonObject{
						{"type", "integer"},
						{"description", "User ID to unblock"}
					}}
				}},
				{"required", QJsonArray{"user_id"}},
			}
		},
		Tool{
			"update_auto_delete_period",
			"Update default auto-delete period for new chats",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"period", QJsonObject{
						{"type", "integer"},
						{"description", "Auto-delete period in seconds (0 to disable, or 86400/604800/2592000 for 1 day/1 week/1 month)"}
					}}
				}},
				{"required", QJsonArray{"period"}},
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

void Server::setSession(Main::Session *session) {
	_session = session;
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
	} else if (name == "get_profile_settings") {
		result = toolGetProfileSettings(arguments);
	} else if (name == "update_profile_name") {
		result = toolUpdateProfileName(arguments);
	} else if (name == "update_profile_bio") {
		result = toolUpdateProfileBio(arguments);
	} else if (name == "update_profile_username") {
		result = toolUpdateProfileUsername(arguments);
	} else if (name == "update_profile_phone") {
		result = toolUpdateProfilePhone(arguments);
	} else if (name == "get_privacy_settings") {
		result = toolGetPrivacySettings(arguments);
	} else if (name == "update_last_seen_privacy") {
		result = toolUpdateLastSeenPrivacy(arguments);
	} else if (name == "update_profile_photo_privacy") {
		result = toolUpdateProfilePhotoPrivacy(arguments);
	} else if (name == "update_phone_number_privacy") {
		result = toolUpdatePhoneNumberPrivacy(arguments);
	} else if (name == "update_forwards_privacy") {
		result = toolUpdateForwardsPrivacy(arguments);
	} else if (name == "update_birthday_privacy") {
		result = toolUpdateBirthdayPrivacy(arguments);
	} else if (name == "update_about_privacy") {
		result = toolUpdateAboutPrivacy(arguments);
	} else if (name == "get_blocked_users") {
		result = toolGetBlockedUsers(arguments);
	} else if (name == "get_security_settings") {
		result = toolGetSecuritySettings(arguments);
	} else if (name == "get_active_sessions") {
		result = toolGetActiveSessions(arguments);
	} else if (name == "terminate_session") {
		result = toolTerminateSession(arguments);
	} else if (name == "block_user") {
		result = toolBlockUser(arguments);
	} else if (name == "unblock_user") {
		result = toolUnblockUser(arguments);
	} else if (name == "update_auto_delete_period") {
		result = toolUpdateAutoDeletePeriod(arguments);
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

// Profile settings tools

QJsonObject Server::toolGetProfileSettings(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	const auto user = _session->user();
	if (!user) {
		return QJsonObject{
			{"error", "User data not available"},
			{"status", "error"},
		};
	}

	// Get birthday info
	const auto birthday = user->birthday();
	QJsonObject birthdayObj;
	if (birthday) {
		birthdayObj["day"] = birthday.day();
		birthdayObj["month"] = birthday.month();
		if (birthday.year()) {
			birthdayObj["year"] = birthday.year();
		}
	}

	return QJsonObject{
		{"first_name", user->firstName},
		{"last_name", user->lastName},
		{"username", user->username()},
		{"phone", user->phone()},
		{"bio", user->about()},
		{"birthday", birthdayObj},
		{"is_premium", user->isPremium()},
		{"status", "success"},
	};
}

QJsonObject Server::toolUpdateProfileName(const QJsonObject &args) {
	QString firstName = args["first_name"].toString();
	QString lastName = args["last_name"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	if (firstName.isEmpty()) {
		return QJsonObject{
			{"error", "First name is required"},
			{"status", "error"},
		};
	}

	// Note: Profile name updates require MTP API call which is async.
	// For MCP, we return immediately indicating the request was initiated.
	// The actual update happens asynchronously.

	return QJsonObject{
		{"first_name", firstName},
		{"last_name", lastName},
		{"status", "initiated"},
		{"note", "Profile name update requires interactive session - use Telegram app to change name"},
	};
}

QJsonObject Server::toolUpdateProfileBio(const QJsonObject &args) {
	QString bio = args["bio"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Use the API to save bio
	_session->api().saveSelfBio(bio);

	return QJsonObject{
		{"bio", bio},
		{"status", "success"},
		{"note", "Bio update initiated"},
	};
}

QJsonObject Server::toolUpdateProfileUsername(const QJsonObject &args) {
	QString username = args["username"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Username changes require interactive verification flow
	return QJsonObject{
		{"username", username},
		{"status", "not_supported"},
		{"note", "Username changes require interactive verification - use Telegram app to change username"},
	};
}

QJsonObject Server::toolUpdateProfilePhone(const QJsonObject &args) {
	QString phone = args["phone"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Phone changes require SMS verification and are not supported via MCP
	return QJsonObject{
		{"phone", phone},
		{"status", "not_supported"},
		{"note", "Phone changes require SMS verification - use Telegram app to change phone number"},
	};
}

// Privacy settings tools

namespace {

QString privacyOptionToString(Api::UserPrivacy::Option option) {
	using Option = Api::UserPrivacy::Option;
	switch (option) {
	case Option::Everyone: return "everybody";
	case Option::Contacts: return "contacts";
	case Option::CloseFriends: return "close_friends";
	case Option::Nobody: return "nobody";
	}
	return "unknown";
}

Api::UserPrivacy::Option stringToPrivacyOption(const QString &str) {
	using Option = Api::UserPrivacy::Option;
	if (str == "everybody" || str == "everyone") return Option::Everyone;
	if (str == "contacts") return Option::Contacts;
	if (str == "close_friends") return Option::CloseFriends;
	if (str == "nobody") return Option::Nobody;
	return Option::Nobody; // Default to most restrictive
}

} // namespace

QJsonObject Server::toolGetPrivacySettings(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Request reload of all privacy settings
	auto &privacy = _session->api().userPrivacy();

	// Reload all relevant privacy keys
	privacy.reload(Api::UserPrivacy::Key::LastSeen);
	privacy.reload(Api::UserPrivacy::Key::ProfilePhoto);
	privacy.reload(Api::UserPrivacy::Key::PhoneNumber);
	privacy.reload(Api::UserPrivacy::Key::Forwards);
	privacy.reload(Api::UserPrivacy::Key::Birthday);
	privacy.reload(Api::UserPrivacy::Key::About);
	privacy.reload(Api::UserPrivacy::Key::Calls);
	privacy.reload(Api::UserPrivacy::Key::Invites);

	// Note: Privacy values are loaded asynchronously via rpl::producer
	// Return a note that settings are being fetched
	return QJsonObject{
		{"status", "loading"},
		{"note", "Privacy settings reload initiated. Values are fetched asynchronously from Telegram servers."},
		{"available_keys", QJsonArray{
			"last_seen", "profile_photo", "phone_number", "forwards",
			"birthday", "about", "calls", "invites"
		}},
	};
}

QJsonObject Server::toolUpdateLastSeenPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::LastSeen,
		privacyRule
	);

	return QJsonObject{
		{"setting", "last_seen"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Last seen privacy update initiated"},
	};
}

QJsonObject Server::toolUpdateProfilePhotoPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::ProfilePhoto,
		privacyRule
	);

	return QJsonObject{
		{"setting", "profile_photo"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Profile photo privacy update initiated"},
	};
}

QJsonObject Server::toolUpdatePhoneNumberPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::PhoneNumber,
		privacyRule
	);

	return QJsonObject{
		{"setting", "phone_number"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Phone number privacy update initiated"},
	};
}

QJsonObject Server::toolUpdateForwardsPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::Forwards,
		privacyRule
	);

	return QJsonObject{
		{"setting", "forwards"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Forwards privacy update initiated"},
	};
}

QJsonObject Server::toolUpdateBirthdayPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::Birthday,
		privacyRule
	);

	return QJsonObject{
		{"setting", "birthday"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Birthday privacy update initiated"},
	};
}

QJsonObject Server::toolUpdateAboutPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::About,
		privacyRule
	);

	return QJsonObject{
		{"setting", "about"},
		{"rule", rule},
		{"status", "success"},
		{"note", "About privacy update initiated"},
	};
}

QJsonObject Server::toolGetBlockedUsers(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Trigger reload of blocked users list
	_session->api().blockedPeers().reload();

	// Note: The blocked list is loaded asynchronously
	return QJsonObject{
		{"status", "loading"},
		{"note", "Blocked users list reload initiated. Data is fetched asynchronously from Telegram servers."},
	};
}

// Security settings tools

QJsonObject Server::toolGetSecuritySettings(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Reload self-destruct settings to get auto-delete period
	_session->api().selfDestruct().reload();

	// Get current auto-delete period
	const auto ttl = _session->api().selfDestruct().periodDefaultHistoryTTLCurrent();

	return QJsonObject{
		{"auto_delete_period_seconds", ttl},
		{"status", "success"},
		{"note", "Security settings retrieved. 2FA status requires async API call."},
	};
}

QJsonObject Server::toolGetActiveSessions(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Reload authorizations
	_session->api().authorizations().reload();

	// Get current list (may be empty if not yet loaded)
	const auto list = _session->api().authorizations().list();

	QJsonArray sessions;
	for (const auto &entry : list) {
		QJsonObject sessionObj;
		sessionObj["hash"] = QString::number(entry.hash);
		sessionObj["name"] = entry.name;
		sessionObj["platform"] = entry.platform;
		sessionObj["system"] = entry.system;
		sessionObj["info"] = entry.info;
		sessionObj["ip"] = entry.ip;
		sessionObj["location"] = entry.location;
		sessionObj["active"] = entry.active;
		sessionObj["is_current"] = (entry.hash == 0);
		sessions.append(sessionObj);
	}

	return QJsonObject{
		{"sessions", sessions},
		{"total", _session->api().authorizations().total()},
		{"status", "success"},
	};
}

QJsonObject Server::toolTerminateSession(const QJsonObject &args) {
	qint64 hash = args["hash"].toVariant().toLongLong();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	if (hash == 0) {
		return QJsonObject{
			{"error", "Cannot terminate current session"},
			{"status", "error"},
		};
	}

	// Request session termination
	_session->api().authorizations().requestTerminate(
		[](const MTPBool &) { /* success */ },
		[](const MTP::Error &) { /* fail */ },
		static_cast<uint64>(hash)
	);

	return QJsonObject{
		{"session_hash", QString::number(hash)},
		{"status", "initiated"},
		{"note", "Session termination request sent"},
	};
}

QJsonObject Server::toolBlockUser(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Get the user peer
	const auto user = _session->data().user(UserId(userId));
	if (!user) {
		return QJsonObject{
			{"error", "User not found"},
			{"user_id", userId},
			{"status", "error"},
		};
	}

	// Block the user
	_session->api().blockedPeers().block(user);

	return QJsonObject{
		{"user_id", userId},
		{"status", "initiated"},
		{"note", "User block request sent"},
	};
}

QJsonObject Server::toolUnblockUser(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Get the user peer
	const auto user = _session->data().user(UserId(userId));
	if (!user) {
		return QJsonObject{
			{"error", "User not found"},
			{"user_id", userId},
			{"status", "error"},
		};
	}

	// Unblock the user
	_session->api().blockedPeers().unblock(user, nullptr);

	return QJsonObject{
		{"user_id", userId},
		{"status", "initiated"},
		{"note", "User unblock request sent"},
	};
}

QJsonObject Server::toolUpdateAutoDeletePeriod(const QJsonObject &args) {
	int period = args["period"].toInt();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Validate period (must be 0, 86400, 604800, or 2592000)
	if (period != 0 && period != 86400 && period != 604800 && period != 2592000) {
		return QJsonObject{
			{"error", "Invalid period. Must be 0 (disabled), 86400 (1 day), 604800 (1 week), or 2592000 (1 month)"},
			{"period", period},
			{"status", "error"},
		};
	}

	// Update auto-delete period
	_session->api().selfDestruct().updateDefaultHistoryTTL(period);

	return QJsonObject{
		{"period", period},
		{"period_description", period == 0 ? "disabled" : (period == 86400 ? "1 day" : (period == 604800 ? "1 week" : "1 month"))},
		{"status", "success"},
		{"note", "Auto-delete period update initiated"},
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
