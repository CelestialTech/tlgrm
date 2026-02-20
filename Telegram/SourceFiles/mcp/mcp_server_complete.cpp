// MCP Server - Core infrastructure (constructor, session, transport, dispatch)
//
// This file is part of Telegram Desktop MCP integration.
// Tool implementations are split into modular files (see file list at bottom).
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"
#include <rpl/rpl.h>

namespace MCP {

Server::Server(QObject *parent)
	: QObject(parent) {
	fprintf(stderr, "[MCP] Server object created\n");
	fflush(stderr);
	initializeCapabilities();
	registerTools();
	registerResources();
	registerPrompts();
	initializeToolHandlers();
}

Server::~Server() {
	_lifetime = nullptr;  // Unsubscribe from session events
	stop();
}

QJsonObject Server::callTool(const QString &toolName, const QJsonObject &args) {
	// Look up tool in handlers
	auto it = _toolHandlers.find(toolName);
	if (it != _toolHandlers.end()) {
		return it.value()(args);
	}

	// Tool not found
	QJsonObject error;
	error["error"] = "tool_not_found";
	error["message"] = QString("Tool '%1' not found in handler table").arg(toolName);
	return error;
}

void Server::initializeCapabilities() {
	_serverInfo.capabilities = QJsonObject{
		{"tools", QJsonObject{{"listChanged", true}}},
		{"resources", QJsonObject{{"listChanged", true}}},
		{"prompts", QJsonObject{{"listChanged", true}}},
	};
}


// registerTools() moved to mcp_tool_registry.cpp

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
		Resource{
			"telegram://archive/stats",
			"Archive Statistics",
			"Statistics about archived data",
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
		Prompt{
			"analyze_trends",
			"Analyze activity trends in a chat",
			QJsonArray{
				QJsonObject{
					{"name", "chat_id"},
					{"description", "Chat ID to analyze"},
					{"required", true}
				}
			}
		},
	};
}

bool Server::start(TransportType transport) {
	fprintf(stderr, "[MCP] Server::start() called, initialized=%d\n", _initialized);
	fflush(stderr);

	if (_initialized) {
		return true;
	}

	_transport = transport;

	// Set database path
	_databasePath = QDir::home().filePath("telegram_mcp.db");

	// Initialize database
	_db = QSqlDatabase::addDatabase("QSQLITE", "mcp_main");
	_db.setDatabaseName(_databasePath);
	if (!_db.open()) {
		qWarning() << "MCP: Failed to open database:" << _db.lastError().text();
		return false;
	}

	fprintf(stderr, "[MCP] Database initialized successfully\n");
	fflush(stderr);

	// Create MCP-specific tables
	{
		QSqlQuery q(_db);
		q.exec("CREATE TABLE IF NOT EXISTS message_tags (chat_id INTEGER, message_id INTEGER, tag_name TEXT, color TEXT, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS translation_cache (chat_id INTEGER, message_id INTEGER, original_text TEXT, translated_text TEXT, source_language TEXT, target_language TEXT, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS auto_translate_config (chat_id INTEGER PRIMARY KEY, target_language TEXT, enabled INTEGER, updated_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS chat_rules (id INTEGER PRIMARY KEY AUTOINCREMENT, chat_id INTEGER, rule_name TEXT, rule_type TEXT, conditions TEXT, actions TEXT, enabled INTEGER DEFAULT 1, priority INTEGER DEFAULT 5, times_triggered INTEGER DEFAULT 0, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS tasks (id INTEGER PRIMARY KEY AUTOINCREMENT, chat_id INTEGER, message_id INTEGER, title TEXT, status TEXT DEFAULT 'pending', priority INTEGER DEFAULT 2, due_date TEXT, created_at TEXT, completed_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS ad_filter_config (id INTEGER PRIMARY KEY, enabled INTEGER, keywords TEXT, exclude_chats TEXT, ads_blocked INTEGER DEFAULT 0, last_blocked_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS quick_replies (id INTEGER PRIMARY KEY AUTOINCREMENT, shortcut TEXT UNIQUE, text TEXT, category TEXT DEFAULT 'general', usage_count INTEGER DEFAULT 0, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS greeting_config (id INTEGER PRIMARY KEY, enabled INTEGER, message TEXT, trigger_chats TEXT, delay_seconds INTEGER DEFAULT 0, greetings_sent INTEGER DEFAULT 0, updated_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS away_config (id INTEGER PRIMARY KEY, enabled INTEGER, message TEXT, start_time TEXT, end_time TEXT, away_sent INTEGER DEFAULT 0, updated_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS business_hours (id INTEGER PRIMARY KEY, enabled INTEGER, schedule TEXT, timezone TEXT DEFAULT 'UTC', updated_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS business_location (id INTEGER PRIMARY KEY, address TEXT, latitude REAL, longitude REAL, updated_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS chatbot_config (id INTEGER PRIMARY KEY, enabled INTEGER, name TEXT, personality TEXT, trigger_keywords TEXT, response_style TEXT, messages_handled INTEGER DEFAULT 0, updated_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS chatbot_training (id INTEGER PRIMARY KEY AUTOINCREMENT, input TEXT NOT NULL, output TEXT NOT NULL, category TEXT DEFAULT 'general', created_at TEXT DEFAULT (datetime('now')))");
		q.exec("CREATE TABLE IF NOT EXISTS voice_persona (name TEXT PRIMARY KEY, voice_id TEXT, pitch REAL DEFAULT 1.0, speed REAL DEFAULT 1.0, created_at TEXT)");
		// TTS cache for generated audio (content-addressable by SHA256 of text+voice+speed+pitch)
		q.exec("CREATE TABLE IF NOT EXISTS tts_cache (cache_key TEXT PRIMARY KEY, audio_data BLOB NOT NULL, duration_seconds REAL, provider TEXT, voice_used TEXT, output_path TEXT, created_at INTEGER)");
		q.exec("CREATE TABLE IF NOT EXISTS voice_clone_samples (name TEXT PRIMARY KEY, sample_path TEXT NOT NULL, provider TEXT DEFAULT 'coqui', language TEXT DEFAULT 'en', created_at TEXT DEFAULT (datetime('now')))");
		// Migration: add provider/sample_path columns to voice_persona (ignore if already exist)
		q.exec("ALTER TABLE voice_persona ADD COLUMN provider TEXT DEFAULT 'piper'");
		q.exec("ALTER TABLE voice_persona ADD COLUMN sample_path TEXT");
		q.exec("CREATE TABLE IF NOT EXISTS video_avatar (name TEXT PRIMARY KEY, source_path TEXT, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS wallet_budgets (id INTEGER PRIMARY KEY, balance REAL DEFAULT 0, daily_limit REAL, weekly_limit REAL, monthly_limit REAL, last_updated TEXT, updated_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS wallet_spending (id INTEGER PRIMARY KEY AUTOINCREMENT, date TEXT, amount REAL, category TEXT, description TEXT, peer_id INTEGER)");
		q.exec("CREATE TABLE IF NOT EXISTS miniapp_budgets (miniapp_id TEXT PRIMARY KEY, approved_amount REAL, spent_amount REAL DEFAULT 0, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS portfolio (gift_type TEXT PRIMARY KEY, quantity INTEGER DEFAULT 0, avg_price REAL DEFAULT 0, current_value REAL DEFAULT 0)");
		q.exec("CREATE TABLE IF NOT EXISTS price_history (id INTEGER PRIMARY KEY AUTOINCREMENT, gift_type TEXT, date TEXT, price REAL)");
		q.exec("CREATE TABLE IF NOT EXISTS price_alerts (id INTEGER PRIMARY KEY AUTOINCREMENT, gift_type TEXT, target_price REAL, direction TEXT, triggered INTEGER DEFAULT 0, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS star_reactions (id INTEGER PRIMARY KEY AUTOINCREMENT, chat_id INTEGER, message_id INTEGER, stars_count INTEGER, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS paid_content (id INTEGER PRIMARY KEY AUTOINCREMENT, chat_id INTEGER, content TEXT, price INTEGER, preview_text TEXT, unlocks INTEGER DEFAULT 0, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS gift_collections (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, description TEXT, is_public INTEGER DEFAULT 0, created_at TEXT)");
		q.exec("CREATE TABLE IF NOT EXISTS collection_items (id INTEGER PRIMARY KEY AUTOINCREMENT, collection_id INTEGER NOT NULL, gift_id TEXT NOT NULL, added_at TEXT DEFAULT (datetime('now')), UNIQUE(collection_id, gift_id))");
		q.exec("CREATE TABLE IF NOT EXISTS auctions (id TEXT PRIMARY KEY, gift_id TEXT NOT NULL, starting_bid INTEGER, current_bid INTEGER, bidder_count INTEGER DEFAULT 0, status TEXT DEFAULT 'active', ends_at TEXT, created_at TEXT DEFAULT (datetime('now')))");
		q.exec("CREATE TABLE IF NOT EXISTS gift_transfers (id INTEGER PRIMARY KEY AUTOINCREMENT, gift_id TEXT, direction TEXT, peer_id INTEGER, stars_amount INTEGER DEFAULT 0, created_at TEXT DEFAULT (datetime('now')))");
		q.exec("CREATE TABLE IF NOT EXISTS giveaways (id INTEGER PRIMARY KEY AUTOINCREMENT, type TEXT, stars_amount INTEGER, winner_count INTEGER DEFAULT 0, channel_id INTEGER, status TEXT DEFAULT 'active', created_at TEXT DEFAULT (datetime('now')))");
		q.exec("CREATE TABLE IF NOT EXISTS marketplace_listings (id TEXT PRIMARY KEY, gift_id TEXT NOT NULL, price INTEGER NOT NULL, category TEXT, status TEXT DEFAULT 'active', created_at TEXT DEFAULT (datetime('now')))");
		q.exec("CREATE TABLE IF NOT EXISTS paid_message_config (chat_id INTEGER PRIMARY KEY, price INTEGER DEFAULT 0, enabled INTEGER DEFAULT 1, updated_at TEXT)");
	}

	// Initialize session-independent components only
	_auditLogger.reset(new AuditLogger(this));
	_auditLogger->start(&_db, QDir::home().filePath("telegram_mcp_audit.log"));

	_rbac.reset(new RBAC(this));
	_rbac->start(&_db);

	fprintf(stderr, "[MCP] Session-independent components initialized (AuditLogger, RBAC)\n");
	fflush(stderr);

	// Start transport (this allows JSON-RPC to work even without session)
	switch (_transport) {
	case TransportType::Stdio:
		startStdioTransport();
		break;
	case TransportType::HTTP:
		startHttpTransport();
		break;
	case TransportType::IPC:
		// IPC mode: Don't start stdin polling, just initialize
		// The Bridge will handle IPC via Unix socket
		fprintf(stderr, "[MCP] IPC transport mode - Bridge will handle socket communication\n");
		fflush(stderr);
		break;
	default:
		qWarning() << "MCP: Unsupported transport type";
		return false;
	}

	_initialized = true;

	_auditLogger->logSystemEvent("server_start", "MCP Server started (session-dependent components will initialize when session available)");

	const char* transportName = "unknown";
	switch (_transport) {
	case TransportType::Stdio: transportName = "stdio"; break;
	case TransportType::HTTP: transportName = "http"; break;
	case TransportType::IPC: transportName = "ipc"; break;
	default: break;
	}

	fprintf(stderr, "[MCP] ========================================\n");
	fprintf(stderr, "[MCP] SERVER STARTED SUCCESSFULLY\n");
	fprintf(stderr, "[MCP] Transport: %s\n", transportName);
	fprintf(stderr, "[MCP] Session-dependent components will initialize when session is set\n");
	fprintf(stderr, "[MCP] Ready to receive requests\n");
	fprintf(stderr, "[MCP] ========================================\n");
	fflush(stderr);

	qInfo() << "MCP Server started (transport:" << transportName << ") - awaiting session";

	return true;
}

void Server::stop() {
	if (!_initialized) {
		return;
	}

	_auditLogger->logSystemEvent("server_stop", "MCP Server stopping");

	// Cleanup components (using reset() for unique_ptr members)
	if (_archiver) {
		_archiver->stop();
		_archiver.reset();
	}

	if (_ephemeralArchiver) {
		_ephemeralArchiver->stop();
		_ephemeralArchiver.reset();
	}

	_analytics.reset();
	_semanticSearch.reset();
	_batchOps.reset();

	if (_scheduler) {
		_scheduler->stop();
		_scheduler.reset();
	}

	if (_auditLogger) {
		_auditLogger->stop();
		_auditLogger.reset();
	}

	if (_rbac) {
		_rbac->stop();
		_rbac.reset();
	}

	_db.close();

	_stdin.reset();
	_stdout.reset();
	_httpServer.reset();

	_initialized = false;
	qInfo() << "MCP Server stopped";
}

void Server::setSession(Main::Session *session) {
	_session = session;

	fprintf(stderr, "[MCP] setSession() called with session=%p\n", (void*)session);
	fflush(stderr);

	if (!_session) {
		qWarning() << "MCP: setSession() called with null session";
		return;
	}

	// Initialize session-dependent components
	fprintf(stderr, "[MCP] Initializing session-dependent components...\n");
	fflush(stderr);

	// CacheManager - initialize first so other components can use it
	_cache.reset(new CacheManager(this));
	_cache->setMaxSize(50);  // 50 MB cache
	_cache->setDefaultTTL(300);  // 5 minutes TTL
	fprintf(stderr, "[MCP] CacheManager initialized (50MB, 300s TTL)\n");
	fflush(stderr);

	// ChatArchiver - requires database
	_archiver.reset(new ChatArchiver(this));
	if (!_archiver->start(_databasePath)) {
		qWarning() << "MCP: Failed to start ChatArchiver";
		fprintf(stderr, "[MCP] WARNING: ChatArchiver failed to start\n");
		fflush(stderr);
		// Don't return - continue with other components
	} else {
		fprintf(stderr, "[MCP] ChatArchiver initialized\n");
		fflush(stderr);
	}

	// EphemeralArchiver - depends on ChatArchiver
	if (_archiver) {
		_ephemeralArchiver.reset(new EphemeralArchiver(this));
		_ephemeralArchiver->start(_archiver.get());
		fprintf(stderr, "[MCP] EphemeralArchiver initialized\n");
		fflush(stderr);
	}

	// Analytics - requires session data
	_analytics.reset(new Analytics(this));
	_analytics->start(&_session->data(), _archiver.get());
	fprintf(stderr, "[MCP] Analytics initialized\n");
	fflush(stderr);

	// SemanticSearch - depends on ChatArchiver
	if (_archiver) {
		_semanticSearch.reset(new SemanticSearch(_archiver.get(), this));
		_semanticSearch->initialize();
		fprintf(stderr, "[MCP] SemanticSearch initialized\n");
		fflush(stderr);
	}

	// BatchOperations - requires session
	_batchOps.reset(new BatchOperations(this));
	_batchOps->start(_session);
	fprintf(stderr, "[MCP] BatchOperations initialized\n");
	fflush(stderr);

	// MessageScheduler - requires session
	_scheduler.reset(new MessageScheduler(this));
	_scheduler->start(_session);
	fprintf(stderr, "[MCP] MessageScheduler initialized\n");
	fflush(stderr);

	// GradualArchiver - depends on ChatArchiver and Session
	_gradualArchiver = std::make_unique<GradualArchiver>(this);
	_gradualArchiver->setMainSession(_session);
	_gradualArchiver->setDataSession(&_session->data());
	if (_archiver) {
		_gradualArchiver->setArchiver(_archiver.get());
	}
	fprintf(stderr, "[MCP] GradualArchiver initialized\n");
	fflush(stderr);

	// LocalLLM - local LLM via Ollama/llama.cpp for chatbot + translation
	_localLLM = std::make_unique<LocalLLM>(this);
	if (_localLLM->start(&_db)) {
		fprintf(stderr, "[MCP] LocalLLM initialized (provider: %s, model: %s)\n",
			(_localLLM->provider() == LLMProvider::Ollama ? "Ollama" : "llama.cpp"),
			qPrintable(_localLLM->model()));
		fflush(stderr);
	} else {
		qWarning() << "MCP: Failed to start LocalLLM";
		fprintf(stderr, "[MCP] WARNING: LocalLLM failed to start\n");
		fflush(stderr);
	}

	// TextToSpeech - local TTS via Piper/espeak-ng/Coqui
	_textToSpeech = std::make_unique<TextToSpeech>(this);
	if (_textToSpeech->start(&_db)) {
		// Auto-detect best available TTS provider
		QProcess piperCheck;
		piperCheck.start("which", QStringList{"piper"});
		piperCheck.waitForFinished(3000);
		if (piperCheck.exitStatus() == QProcess::NormalExit && piperCheck.exitCode() == 0) {
			_textToSpeech->setProvider(TTSProvider::PiperTTS);
		} else {
			QProcess espeakCheck;
			espeakCheck.start("which", QStringList{"espeak-ng"});
			espeakCheck.waitForFinished(3000);
			if (espeakCheck.exitStatus() == QProcess::NormalExit && espeakCheck.exitCode() == 0) {
				_textToSpeech->setProvider(TTSProvider::EspeakNG);
			}
		}
		fprintf(stderr, "[MCP] TextToSpeech initialized\n");
		fflush(stderr);
	} else {
		qWarning() << "MCP: Failed to start TextToSpeech";
		fprintf(stderr, "[MCP] WARNING: TextToSpeech failed to start\n");
		fflush(stderr);
	}

	// BotManager - depends on all other components
	if (_archiver && _analytics && _semanticSearch && _scheduler && _auditLogger && _rbac) {
		_botManager.reset(new BotManager(this));
		_botManager->initialize(
			_archiver.get(),
			_analytics.get(),
			_semanticSearch.get(),
			_scheduler.get(),
			_auditLogger.get(),
			_rbac.get()
		);

		// Load and register built-in bots
		_botManager->discoverBots();

		// Register and start the Context Assistant Bot (example)
		auto *contextBot = new ContextAssistantBot(_botManager.get());
		_botManager->registerBot(contextBot);
		_botManager->startBot("context_assistant");

		fprintf(stderr, "[MCP] BotManager initialized and bots started\n");
		fflush(stderr);
	}

	// Wire BotManager to receive new message events from the session
	if (_botManager && _botManager->isEventDispatchEnabled()) {
		_lifetime = std::make_unique<rpl::lifetime>();
		_session->data().newItemAdded(
		) | rpl::start_with_next([this](not_null<HistoryItem*> item) {
			if (!_botManager || !_botManager->isEventDispatchEnabled()) {
				return;
			}
			Message msg;
			msg.id = item->id.bare;
			msg.chatId = item->history()->peer->id.value;
			msg.text = item->originalText().text;
			msg.timestamp = item->date();
			auto from = item->from();
			if (from) {
				msg.userId = from->id.value;
				msg.username = from->name();
			}
			msg.messageType = "text";
			msg.reactionCount = 0;
			msg.isThreadStart = false;
			msg.threadReplyCount = 0;
			_botManager->dispatchMessage(msg);
		}, *_lifetime);
		fprintf(stderr, "[MCP] BotManager wired to session newItemAdded events\n");
		fflush(stderr);
	}

	if (_auditLogger) {
		_auditLogger->logSystemEvent("session_connected", "MCP Server session-dependent components initialized successfully");
	}

	fprintf(stderr, "[MCP] ========================================\n");
	fprintf(stderr, "[MCP] SESSION CONNECTED SUCCESSFULLY\n");
	fprintf(stderr, "[MCP] All components initialized and ready\n");
	fprintf(stderr, "[MCP] Live Telegram data access enabled\n");
	fprintf(stderr, "[MCP] ========================================\n");
	fflush(stderr);

	qInfo() << "MCP: Session set, live data access enabled";
}

void Server::startStdioTransport() {
	_stdin.reset(new QTextStream(stdin));
	_stdout.reset(new QTextStream(stdout));

	fprintf(stderr, "[MCP] Stdio transport started, polling stdin every 100ms\n");
	fflush(stderr);

	// Use a timer to poll stdin
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

		fprintf(stderr, "[MCP] Received input: %s\n", line.toUtf8().constData());
		fflush(stderr);

		// Parse JSON-RPC request
		QJsonParseError error;
		QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);

		if (error.error != QJsonParseError::NoError) {
			fprintf(stderr, "[MCP] JSON parse error: %s\n", error.errorString().toUtf8().constData());
			fflush(stderr);
			return;
		}

		QJsonObject request = doc.object();
		QJsonObject response = handleRequest(request);

		// Write response to stdout
		QByteArray responseBytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
		fprintf(stderr, "[MCP] Sending response: %s\n", responseBytes.constData());
		fflush(stderr);

		*_stdout << responseBytes;
		*_stdout << "\n";
		_stdout->flush();
	}
}

void Server::startHttpTransport(int port) {
	_httpServer = std::make_unique<QTcpServer>(this);

	connect(_httpServer.get(), &QTcpServer::newConnection, this, [this]() {
		while (auto *socket = _httpServer->nextPendingConnection()) {
			connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
				// Accumulate data until we have full HTTP request
				QByteArray data = socket->property("_buffer").toByteArray();
				data.append(socket->readAll());
				socket->setProperty("_buffer", data);

				// Check if we have complete headers
				int headerEnd = data.indexOf("\r\n\r\n");
				if (headerEnd < 0) return;

				// Parse Content-Length
				int contentLength = 0;
				QByteArray headers = data.left(headerEnd);
				for (const auto &line : headers.split('\n')) {
					if (line.toLower().trimmed().startsWith("content-length:")) {
						contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
					}
				}

				// Check if we have the complete body
				QByteArray body = data.mid(headerEnd + 4);
				if (body.size() < contentLength) return;
				body = body.left(contentLength);

				// Parse method and path from first line
				QByteArray firstLine = headers.left(headers.indexOf('\n')).trimmed();
				bool isPost = firstLine.startsWith("POST");
				bool isOptions = firstLine.startsWith("OPTIONS");

				// CORS headers for all responses
				QByteArray cors =
					"Access-Control-Allow-Origin: *\r\n"
					"Access-Control-Allow-Methods: POST, OPTIONS\r\n"
					"Access-Control-Allow-Headers: Content-Type\r\n";

				if (isOptions) {
					QByteArray response = "HTTP/1.1 204 No Content\r\n" + cors + "\r\n";
					socket->write(response);
					socket->flush();
					socket->disconnectFromHost();
					return;
				}

				if (!isPost) {
					QByteArray errorBody = R"({"error":"Only POST requests accepted"})";
					QByteArray response = "HTTP/1.1 405 Method Not Allowed\r\n"
						+ cors
						+ "Content-Type: application/json\r\n"
						+ "Content-Length: " + QByteArray::number(errorBody.size()) + "\r\n"
						+ "\r\n" + errorBody;
					socket->write(response);
					socket->flush();
					socket->disconnectFromHost();
					return;
				}

				// Parse JSON-RPC request from body
				QJsonParseError parseError;
				QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);

				QByteArray responseBody;
				if (parseError.error != QJsonParseError::NoError) {
					QJsonObject err;
					err["jsonrpc"] = "2.0";
					err["id"] = QJsonValue();
					QJsonObject errObj;
					errObj["code"] = -32700;
					errObj["message"] = "Parse error";
					err["error"] = errObj;
					responseBody = QJsonDocument(err).toJson(QJsonDocument::Compact);
				} else {
					QJsonObject request = doc.object();
					QJsonObject result = handleRequest(request);
					responseBody = QJsonDocument(result).toJson(QJsonDocument::Compact);
				}

				QByteArray httpResponse = "HTTP/1.1 200 OK\r\n"
					+ cors
					+ "Content-Type: application/json\r\n"
					+ "Content-Length: " + QByteArray::number(responseBody.size()) + "\r\n"
					+ "\r\n" + responseBody;
				socket->write(httpResponse);
				socket->flush();
				socket->disconnectFromHost();
			});

			connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
		}
	});

	if (_httpServer->listen(QHostAddress::LocalHost, port)) {
		fprintf(stderr, "[MCP] HTTP transport started on http://127.0.0.1:%d\n", port);
		fflush(stderr);
	} else {
		fprintf(stderr, "[MCP] HTTP transport failed to start on port %d: %s\n",
			port, _httpServer->errorString().toUtf8().constData());
		fflush(stderr);
		_httpServer.reset();
	}
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

	_auditLogger->logSystemEvent("initialize", "Client initialized");

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
	QString toolName = params["name"].toString();
	QJsonObject arguments = params["arguments"].toObject();

	if (_auditLogger) {
		_auditLogger->logToolInvoked(toolName, arguments);
	}

	QJsonObject result;

	// Dispatch via O(1) hash lookup — all ~430 tools registered in initializeToolHandlers()
	auto handler = _toolHandlers.find(toolName);
	if (handler != _toolHandlers.end()) {
		result = (*handler)(arguments);
	} else {
		result["error"] = "Unknown tool: " + toolName;
		if (_auditLogger) {
			_auditLogger->logError("Unknown tool: " + toolName, "tool_call");
		}
	}

	// Log tool completion via audit logger
	if (_auditLogger) {
		bool hasError = result.contains("error");
		QString errorMsg = hasError ? result["error"].toString() : QString();
		_auditLogger->logToolCompleted(toolName, hasError ? "error" : "success", 0, errorMsg);
	}

	// Build response object
	QJsonObject response;
	QJsonArray contentArray;
	QJsonObject textContent;
	textContent["type"] = "text";
	textContent["text"] = QString(QJsonDocument(result).toJson(QJsonDocument::Compact));
	contentArray.append(textContent);
	response["content"] = contentArray;
	return response;
}

// ===== HELPER METHODS =====

bool Server::validateRequired(
	const QJsonObject &args,
	const QStringList &requiredFields,
	QString &errorMessage
) {
	for (const QString &field : requiredFields) {
		if (!args.contains(field)) {
			errorMessage = QString("Missing required field: %1").arg(field);
			return false;
		}
		// Check for null/undefined values
		if (args[field].isNull() || args[field].isUndefined()) {
			errorMessage = QString("Field '%1' cannot be null").arg(field);
			return false;
		}
	}
	return true;
}

QJsonObject Server::toolError(const QString &message, const QJsonObject &context) {
	QJsonObject result;
	result["error"] = message;
	// Merge context fields
	for (auto it = context.begin(); it != context.end(); ++it) {
		result[it.key()] = it.value();
	}
	return result;
}

QJsonObject Server::extractMessageJson(HistoryItem *item) {
	QJsonObject msg;
	if (!item) {
		return msg;
	}

	msg["message_id"] = QString::number(item->id.bare);
	msg["date"] = static_cast<qint64>(item->date());

	// Get message text
	const auto &text = item->originalText();
	msg["text"] = text.text;

	// Get sender information
	auto from = item->from();
	if (from) {
		QJsonObject fromUser;
		fromUser["id"] = QString::number(from->id.value);
		fromUser["name"] = from->name();
		if (!from->username().isEmpty()) {
			fromUser["username"] = from->username();
		}
		msg["from_user"] = fromUser;
	}

	// Add optional fields
	if (item->out()) {
		msg["is_outgoing"] = true;
	}
	if (item->isPinned()) {
		msg["is_pinned"] = true;
	}

	// Add reply information if present
	if (item->replyToId()) {
		QJsonObject reply;
		reply["message_id"] = QString::number(item->replyToId().bare);
		msg["reply_to"] = reply;
	}

	return msg;
}

void Server::initializeToolHandlers() {
	// CORE TOOLS
	_toolHandlers["list_chats"] = [this](const QJsonObject &args) { return toolListChats(args); };
	_toolHandlers["get_chat_info"] = [this](const QJsonObject &args) { return toolGetChatInfo(args); };
	_toolHandlers["read_messages"] = [this](const QJsonObject &args) { return toolReadMessages(args); };
	_toolHandlers["send_message"] = [this](const QJsonObject &args) { return toolSendMessage(args); };
	_toolHandlers["search_messages"] = [this](const QJsonObject &args) { return toolSearchMessages(args); };
	_toolHandlers["get_user_info"] = [this](const QJsonObject &args) { return toolGetUserInfo(args); };

	// ARCHIVE TOOLS
	_toolHandlers["archive_chat"] = [this](const QJsonObject &args) { return toolArchiveChat(args); };
	_toolHandlers["export_chat"] = [this](const QJsonObject &args) { return toolExportChat(args); };
	_toolHandlers["list_archived_chats"] = [this](const QJsonObject &args) { return toolListArchivedChats(args); };
	_toolHandlers["get_archive_stats"] = [this](const QJsonObject &args) { return toolGetArchiveStats(args); };
	_toolHandlers["configure_ephemeral_capture"] = [this](const QJsonObject &args) { return toolConfigureEphemeralCapture(args); };
	_toolHandlers["get_ephemeral_stats"] = [this](const QJsonObject &args) { return toolGetEphemeralStats(args); };
	_toolHandlers["get_ephemeral_messages"] = [this](const QJsonObject &args) { return toolGetEphemeralMessages(args); };
	_toolHandlers["search_archive"] = [this](const QJsonObject &args) { return toolSearchArchive(args); };
	_toolHandlers["purge_archive"] = [this](const QJsonObject &args) { return toolPurgeArchive(args); };

	// GRADUAL EXPORT TOOLS
	_toolHandlers["start_gradual_export"] = [this](const QJsonObject &args) { return toolStartGradualExport(args); };
	_toolHandlers["get_gradual_export_status"] = [this](const QJsonObject &args) { return toolGetGradualExportStatus(args); };
	_toolHandlers["pause_gradual_export"] = [this](const QJsonObject &args) { return toolPauseGradualExport(args); };
	_toolHandlers["resume_gradual_export"] = [this](const QJsonObject &args) { return toolResumeGradualExport(args); };
	_toolHandlers["cancel_gradual_export"] = [this](const QJsonObject &args) { return toolCancelGradualExport(args); };
	_toolHandlers["get_gradual_export_config"] = [this](const QJsonObject &args) { return toolGetGradualExportConfig(args); };
	_toolHandlers["set_gradual_export_config"] = [this](const QJsonObject &args) { return toolSetGradualExportConfig(args); };
	_toolHandlers["queue_gradual_export"] = [this](const QJsonObject &args) { return toolQueueGradualExport(args); };
	_toolHandlers["get_gradual_export_queue"] = [this](const QJsonObject &args) { return toolGetGradualExportQueue(args); };

	// ANALYTICS TOOLS
	_toolHandlers["get_message_stats"] = [this](const QJsonObject &args) { return toolGetMessageStats(args); };
	_toolHandlers["get_user_activity"] = [this](const QJsonObject &args) { return toolGetUserActivity(args); };
	_toolHandlers["get_chat_activity"] = [this](const QJsonObject &args) { return toolGetChatActivity(args); };
	_toolHandlers["get_time_series"] = [this](const QJsonObject &args) { return toolGetTimeSeries(args); };
	_toolHandlers["get_top_users"] = [this](const QJsonObject &args) { return toolGetTopUsers(args); };
	_toolHandlers["get_top_words"] = [this](const QJsonObject &args) { return toolGetTopWords(args); };
	_toolHandlers["export_analytics"] = [this](const QJsonObject &args) { return toolExportAnalytics(args); };
	_toolHandlers["get_trends"] = [this](const QJsonObject &args) { return toolGetTrends(args); };

	// SEMANTIC SEARCH TOOLS
	_toolHandlers["semantic_search"] = [this](const QJsonObject &args) { return toolSemanticSearch(args); };
	_toolHandlers["index_messages"] = [this](const QJsonObject &args) { return toolIndexMessages(args); };
	_toolHandlers["semantic_index_messages"] = [this](const QJsonObject &args) { return toolIndexMessages(args); }; // alias
	_toolHandlers["detect_topics"] = [this](const QJsonObject &args) { return toolDetectTopics(args); };
	_toolHandlers["classify_intent"] = [this](const QJsonObject &args) { return toolClassifyIntent(args); };
	_toolHandlers["extract_entities"] = [this](const QJsonObject &args) { return toolExtractEntities(args); };

	// MESSAGE OPERATIONS
	_toolHandlers["edit_message"] = [this](const QJsonObject &args) { return toolEditMessage(args); };
	_toolHandlers["delete_message"] = [this](const QJsonObject &args) { return toolDeleteMessage(args); };
	_toolHandlers["forward_message"] = [this](const QJsonObject &args) { return toolForwardMessage(args); };
	_toolHandlers["pin_message"] = [this](const QJsonObject &args) { return toolPinMessage(args); };
	_toolHandlers["unpin_message"] = [this](const QJsonObject &args) { return toolUnpinMessage(args); };
	_toolHandlers["add_reaction"] = [this](const QJsonObject &args) { return toolAddReaction(args); };

	// BATCH OPERATIONS
	_toolHandlers["batch_send"] = [this](const QJsonObject &args) { return toolBatchSend(args); };
	_toolHandlers["batch_delete"] = [this](const QJsonObject &args) { return toolBatchDelete(args); };
	_toolHandlers["batch_forward"] = [this](const QJsonObject &args) { return toolBatchForward(args); };
	_toolHandlers["batch_pin"] = [this](const QJsonObject &args) { return toolBatchPin(args); };
	_toolHandlers["batch_reaction"] = [this](const QJsonObject &args) { return toolBatchReaction(args); };

	// SCHEDULER TOOLS
	_toolHandlers["schedule_message"] = [this](const QJsonObject &args) { return toolScheduleMessage(args); };
	_toolHandlers["cancel_scheduled"] = [this](const QJsonObject &args) { return toolCancelScheduled(args); };
	_toolHandlers["list_scheduled"] = [this](const QJsonObject &args) { return toolListScheduled(args); };
	_toolHandlers["update_scheduled"] = [this](const QJsonObject &args) { return toolUpdateScheduled(args); };

	// SYSTEM TOOLS
	_toolHandlers["get_cache_stats"] = [this](const QJsonObject &args) { return toolGetCacheStats(args); };
	_toolHandlers["get_server_info"] = [this](const QJsonObject &args) { return toolGetServerInfo(args); };
	_toolHandlers["get_audit_log"] = [this](const QJsonObject &args) { return toolGetAuditLog(args); };
	_toolHandlers["health_check"] = [this](const QJsonObject &args) { return toolHealthCheck(args); };

	// VOICE TOOLS
	_toolHandlers["transcribe_voice"] = [this](const QJsonObject &args) { return toolTranscribeVoice(args); };
	_toolHandlers["get_transcription"] = [this](const QJsonObject &args) { return toolGetTranscription(args); };

	// BOT FRAMEWORK TOOLS
	_toolHandlers["list_bots"] = [this](const QJsonObject &args) { return toolListBots(args); };
	_toolHandlers["get_bot_info"] = [this](const QJsonObject &args) { return toolGetBotInfo(args); };
	_toolHandlers["start_bot"] = [this](const QJsonObject &args) { return toolStartBot(args); };
	_toolHandlers["stop_bot"] = [this](const QJsonObject &args) { return toolStopBot(args); };
	_toolHandlers["configure_bot"] = [this](const QJsonObject &args) { return toolConfigureBot(args); };
	_toolHandlers["get_bot_stats"] = [this](const QJsonObject &args) { return toolGetBotStats(args); };
	_toolHandlers["send_bot_command"] = [this](const QJsonObject &args) { return toolSendBotCommand(args); };
	_toolHandlers["get_bot_suggestions"] = [this](const QJsonObject &args) { return toolGetBotSuggestions(args); };

	// PROFILE SETTINGS TOOLS
	_toolHandlers["get_profile_settings"] = [this](const QJsonObject &args) { return toolGetProfileSettings(args); };
	_toolHandlers["update_profile_name"] = [this](const QJsonObject &args) { return toolUpdateProfileName(args); };
	_toolHandlers["update_profile_bio"] = [this](const QJsonObject &args) { return toolUpdateProfileBio(args); };
	_toolHandlers["update_profile_username"] = [this](const QJsonObject &args) { return toolUpdateProfileUsername(args); };
	_toolHandlers["update_profile_phone"] = [this](const QJsonObject &args) { return toolUpdateProfilePhone(args); };

	// PRIVACY SETTINGS TOOLS
	_toolHandlers["get_privacy_settings"] = [this](const QJsonObject &args) { return toolGetPrivacySettings(args); };
	_toolHandlers["update_last_seen_privacy"] = [this](const QJsonObject &args) { return toolUpdateLastSeenPrivacy(args); };
	_toolHandlers["update_profile_photo_privacy"] = [this](const QJsonObject &args) { return toolUpdateProfilePhotoPrivacy(args); };
	_toolHandlers["update_phone_number_privacy"] = [this](const QJsonObject &args) { return toolUpdatePhoneNumberPrivacy(args); };
	_toolHandlers["update_forwards_privacy"] = [this](const QJsonObject &args) { return toolUpdateForwardsPrivacy(args); };
	_toolHandlers["update_birthday_privacy"] = [this](const QJsonObject &args) { return toolUpdateBirthdayPrivacy(args); };
	_toolHandlers["update_about_privacy"] = [this](const QJsonObject &args) { return toolUpdateAboutPrivacy(args); };
	_toolHandlers["get_blocked_users"] = [this](const QJsonObject &args) { return toolGetBlockedUsers(args); };

	// SECURITY SETTINGS TOOLS
	_toolHandlers["get_security_settings"] = [this](const QJsonObject &args) { return toolGetSecuritySettings(args); };
	_toolHandlers["get_active_sessions"] = [this](const QJsonObject &args) { return toolGetActiveSessions(args); };
	_toolHandlers["terminate_session"] = [this](const QJsonObject &args) { return toolTerminateSession(args); };
	_toolHandlers["block_user"] = [this](const QJsonObject &args) { return toolBlockUser(args); };
	_toolHandlers["unblock_user"] = [this](const QJsonObject &args) { return toolUnblockUser(args); };
	_toolHandlers["update_auto_delete_period"] = [this](const QJsonObject &args) { return toolUpdateAutoDeletePeriod(args); };

	// PREMIUM FEATURES - Voice-to-Text
	_toolHandlers["transcribe_voice_message"] = [this](const QJsonObject &args) { return toolTranscribeVoiceMessage(args); };
	_toolHandlers["get_transcription_status"] = [this](const QJsonObject &args) { return toolGetTranscriptionStatus(args); };

	// PREMIUM FEATURES - Translation
	_toolHandlers["translate_messages"] = [this](const QJsonObject &args) { return toolTranslateMessages(args); };
	_toolHandlers["auto_translate_chat"] = [this](const QJsonObject &args) { return toolAutoTranslateChat(args); };
	_toolHandlers["get_translation_languages"] = [this](const QJsonObject &args) { return toolGetTranslationLanguages(args); };

	// PREMIUM FEATURES - Message Tags
	_toolHandlers["tag_message"] = [this](const QJsonObject &args) { return toolAddMessageTag(args); };
	_toolHandlers["get_tagged_messages"] = [this](const QJsonObject &args) { return toolSearchByTag(args); };
	_toolHandlers["list_tags"] = [this](const QJsonObject &args) { return toolGetMessageTags(args); };
	_toolHandlers["delete_tag"] = [this](const QJsonObject &args) { return toolRemoveMessageTag(args); };
	_toolHandlers["add_message_tag"] = [this](const QJsonObject &args) { return toolAddMessageTag(args); };
	_toolHandlers["get_message_tags"] = [this](const QJsonObject &args) { return toolGetMessageTags(args); };
	_toolHandlers["remove_message_tag"] = [this](const QJsonObject &args) { return toolRemoveMessageTag(args); };
	_toolHandlers["search_by_tag"] = [this](const QJsonObject &args) { return toolSearchByTag(args); };
	_toolHandlers["get_tag_suggestions"] = [this](const QJsonObject &args) { return toolGetTagSuggestions(args); };

	// PREMIUM FEATURES - Ad Filtering
	_toolHandlers["configure_ad_filter"] = [this](const QJsonObject &args) { return toolConfigureAdFilter(args); };
	_toolHandlers["get_filtered_ads"] = [this](const QJsonObject &args) { return toolGetFilteredAds(args); };

	// PREMIUM FEATURES - Chat Rules
	_toolHandlers["create_chat_rule"] = [this](const QJsonObject &args) { return toolCreateChatRule(args); };
	_toolHandlers["list_chat_rules"] = [this](const QJsonObject &args) { return toolListChatRules(args); };
	_toolHandlers["execute_chat_rules"] = [this](const QJsonObject &args) { return toolExecuteChatRules(args); };
	_toolHandlers["delete_chat_rule"] = [this](const QJsonObject &args) { return toolDeleteChatRule(args); };

	// PREMIUM FEATURES - Tasks
	_toolHandlers["create_task"] = [this](const QJsonObject &args) { return toolCreateTask(args); };
	_toolHandlers["list_tasks"] = [this](const QJsonObject &args) { return toolListTasks(args); };

	// BUSINESS FEATURES - Quick Replies
	_toolHandlers["create_quick_reply"] = [this](const QJsonObject &args) { return toolCreateQuickReply(args); };
	_toolHandlers["list_quick_replies"] = [this](const QJsonObject &args) { return toolListQuickReplies(args); };
	_toolHandlers["send_quick_reply"] = [this](const QJsonObject &args) { return toolSendQuickReply(args); };
	_toolHandlers["edit_quick_reply"] = [this](const QJsonObject &args) { return toolEditQuickReply(args); };
	_toolHandlers["delete_quick_reply"] = [this](const QJsonObject &args) { return toolDeleteQuickReply(args); };

	// BUSINESS FEATURES - Greeting Messages
	_toolHandlers["configure_greeting"] = [this](const QJsonObject &args) { return toolConfigureGreeting(args); };
	_toolHandlers["get_greeting_config"] = [this](const QJsonObject &args) { return toolGetGreetingConfig(args); };
	_toolHandlers["test_greeting"] = [this](const QJsonObject &args) { return toolTestGreeting(args); };
	_toolHandlers["get_greeting_stats"] = [this](const QJsonObject &args) { return toolGetGreetingStats(args); };

	// BUSINESS FEATURES - Away Messages
	_toolHandlers["configure_away_message"] = [this](const QJsonObject &args) { return toolConfigureAwayMessage(args); };
	_toolHandlers["get_away_config"] = [this](const QJsonObject &args) { return toolGetAwayConfig(args); };
	_toolHandlers["set_away_now"] = [this](const QJsonObject &args) { return toolSetAwayNow(args); };
	_toolHandlers["disable_away"] = [this](const QJsonObject &args) { return toolDisableAway(args); };
	_toolHandlers["get_away_stats"] = [this](const QJsonObject &args) { return toolGetAwayStats(args); };

	// BUSINESS FEATURES - Business Hours
	_toolHandlers["set_business_hours"] = [this](const QJsonObject &args) { return toolSetBusinessHours(args); };
	_toolHandlers["get_business_hours"] = [this](const QJsonObject &args) { return toolGetBusinessHours(args); };
	_toolHandlers["is_open_now"] = [this](const QJsonObject &args) { return toolIsOpenNow(args); };

	// BUSINESS FEATURES - Business Location
	_toolHandlers["set_business_location"] = [this](const QJsonObject &args) { return toolSetBusinessLocation(args); };
	_toolHandlers["get_business_location"] = [this](const QJsonObject &args) { return toolGetBusinessLocation(args); };

	// BUSINESS FEATURES - AI Chatbot
	_toolHandlers["configure_ai_chatbot"] = [this](const QJsonObject &args) { return toolConfigureAiChatbot(args); };
	_toolHandlers["get_chatbot_config"] = [this](const QJsonObject &args) { return toolGetChatbotConfig(args); };
	_toolHandlers["pause_chatbot"] = [this](const QJsonObject &args) { return toolPauseChatbot(args); };
	_toolHandlers["resume_chatbot"] = [this](const QJsonObject &args) { return toolResumeChatbot(args); };
	_toolHandlers["set_chatbot_prompt"] = [this](const QJsonObject &args) { return toolSetChatbotPrompt(args); };
	_toolHandlers["get_chatbot_stats"] = [this](const QJsonObject &args) { return toolGetChatbotStats(args); };
	_toolHandlers["train_chatbot"] = [this](const QJsonObject &args) { return toolTrainChatbot(args); };

	// BUSINESS FEATURES - AI Voice (TTS)
	_toolHandlers["configure_voice_persona"] = [this](const QJsonObject &args) { return toolConfigureVoicePersona(args); };
	_toolHandlers["generate_voice_message"] = [this](const QJsonObject &args) { return toolGenerateVoiceMessage(args); };
	_toolHandlers["send_voice_reply"] = [this](const QJsonObject &args) { return toolSendVoiceReply(args); };
	_toolHandlers["list_voice_presets"] = [this](const QJsonObject &args) { return toolListVoicePresets(args); };
	_toolHandlers["clone_voice"] = [this](const QJsonObject &args) { return toolCloneVoice(args); };

	// BUSINESS FEATURES - AI Video Circles (TTV)
	_toolHandlers["configure_video_avatar"] = [this](const QJsonObject &args) { return toolConfigureVideoAvatar(args); };
	_toolHandlers["generate_video_circle"] = [this](const QJsonObject &args) { return toolGenerateVideoCircle(args); };
	_toolHandlers["send_video_reply"] = [this](const QJsonObject &args) { return toolSendVideoReply(args); };
	_toolHandlers["upload_avatar_source"] = [this](const QJsonObject &args) { return toolUploadAvatarSource(args); };
	_toolHandlers["list_avatar_presets"] = [this](const QJsonObject &args) { return toolListAvatarPresets(args); };

	// WALLET FEATURES - Balance & Analytics
	_toolHandlers["get_wallet_balance"] = [this](const QJsonObject &args) { return toolGetWalletBalance(args); };
	_toolHandlers["get_balance_history"] = [this](const QJsonObject &args) { return toolGetBalanceHistory(args); };
	_toolHandlers["get_spending_analytics"] = [this](const QJsonObject &args) { return toolGetSpendingAnalytics(args); };
	_toolHandlers["get_income_analytics"] = [this](const QJsonObject &args) { return toolGetIncomeAnalytics(args); };

	// WALLET FEATURES - Transactions
	_toolHandlers["get_transactions"] = [this](const QJsonObject &args) { return toolGetTransactions(args); };
	_toolHandlers["get_transaction_details"] = [this](const QJsonObject &args) { return toolGetTransactionDetails(args); };
	_toolHandlers["export_transactions"] = [this](const QJsonObject &args) { return toolExportTransactions(args); };
	_toolHandlers["search_transactions"] = [this](const QJsonObject &args) { return toolSearchTransactions(args); };

	// WALLET FEATURES - Gifts
	_toolHandlers["list_gifts"] = [this](const QJsonObject &args) { return toolListGifts(args); };
	_toolHandlers["get_gift_details"] = [this](const QJsonObject &args) { return toolGetGiftDetails(args); };
	_toolHandlers["get_gift_analytics"] = [this](const QJsonObject &args) { return toolGetGiftAnalytics(args); };
	_toolHandlers["send_stars"] = [this](const QJsonObject &args) { return toolSendStars(args); };

	// WALLET FEATURES - Subscriptions
	_toolHandlers["list_subscriptions"] = [this](const QJsonObject &args) { return toolListSubscriptions(args); };
	_toolHandlers["get_subscription_alerts"] = [this](const QJsonObject &args) { return toolGetSubscriptionAlerts(args); };
	_toolHandlers["cancel_subscription"] = [this](const QJsonObject &args) { return toolCancelSubscription(args); };

	// WALLET FEATURES - Monetization
	_toolHandlers["get_channel_earnings"] = [this](const QJsonObject &args) { return toolGetChannelEarnings(args); };
	_toolHandlers["get_all_channels_earnings"] = [this](const QJsonObject &args) { return toolGetAllChannelsEarnings(args); };
	_toolHandlers["get_earnings_chart"] = [this](const QJsonObject &args) { return toolGetEarningsChart(args); };
	_toolHandlers["get_reaction_stats"] = [this](const QJsonObject &args) { return toolGetReactionStats(args); };
	_toolHandlers["get_paid_content_earnings"] = [this](const QJsonObject &args) { return toolGetPaidContentEarnings(args); };

	// WALLET FEATURES - Giveaways
	_toolHandlers["get_giveaway_options"] = [this](const QJsonObject &args) { return toolGetGiveawayOptions(args); };
	_toolHandlers["list_giveaways"] = [this](const QJsonObject &args) { return toolListGiveaways(args); };
	_toolHandlers["get_giveaway_stats"] = [this](const QJsonObject &args) { return toolGetGiveawayStats(args); };

	// WALLET FEATURES - Advanced
	_toolHandlers["get_topup_options"] = [this](const QJsonObject &args) { return toolGetTopupOptions(args); };
	_toolHandlers["get_star_rating"] = [this](const QJsonObject &args) { return toolGetStarRating(args); };
	_toolHandlers["get_withdrawal_status"] = [this](const QJsonObject &args) { return toolGetWithdrawalStatus(args); };
	_toolHandlers["create_crypto_payment"] = [this](const QJsonObject &args) { return toolCreateCryptoPayment(args); };

	// WALLET FEATURES - Budget & Reporting
	_toolHandlers["set_wallet_budget"] = [this](const QJsonObject &args) { return toolSetWalletBudget(args); };
	_toolHandlers["get_budget_status"] = [this](const QJsonObject &args) { return toolGetBudgetStatus(args); };
	_toolHandlers["configure_wallet_alerts"] = [this](const QJsonObject &args) { return toolConfigureWalletAlerts(args); };
	_toolHandlers["generate_financial_report"] = [this](const QJsonObject &args) { return toolGenerateFinancialReport(args); };
	_toolHandlers["get_tax_summary"] = [this](const QJsonObject &args) { return toolGetTaxSummary(args); };

	// STARS FEATURES - Star Gifts Management
	_toolHandlers["list_star_gifts"] = [this](const QJsonObject &args) { return toolListStarGifts(args); };
	_toolHandlers["get_star_gift_details"] = [this](const QJsonObject &args) { return toolGetStarGiftDetails(args); };
	_toolHandlers["get_unique_gift_analytics"] = [this](const QJsonObject &args) { return toolGetUniqueGiftAnalytics(args); };
	_toolHandlers["get_collectibles_portfolio"] = [this](const QJsonObject &args) { return toolGetCollectiblesPortfolio(args); };
	_toolHandlers["send_star_gift"] = [this](const QJsonObject &args) { return toolSendStarGift(args); };
	_toolHandlers["get_gift_transfer_history"] = [this](const QJsonObject &args) { return toolGetGiftTransferHistory(args); };
	_toolHandlers["get_upgrade_options"] = [this](const QJsonObject &args) { return toolGetUpgradeOptions(args); };
	_toolHandlers["transfer_gift"] = [this](const QJsonObject &args) { return toolTransferGift(args); };

	// STARS FEATURES - Gift Collections
	_toolHandlers["list_gift_collections"] = [this](const QJsonObject &args) { return toolListGiftCollections(args); };
	_toolHandlers["get_collection_details"] = [this](const QJsonObject &args) { return toolGetCollectionDetails(args); };
	_toolHandlers["get_collection_completion"] = [this](const QJsonObject &args) { return toolGetCollectionCompletion(args); };

	// STARS FEATURES - Auctions
	_toolHandlers["list_active_auctions"] = [this](const QJsonObject &args) { return toolListActiveAuctions(args); };
	_toolHandlers["get_auction_details"] = [this](const QJsonObject &args) { return toolGetAuctionDetails(args); };
	_toolHandlers["get_auction_alerts"] = [this](const QJsonObject &args) { return toolGetAuctionAlerts(args); };
	_toolHandlers["place_auction_bid"] = [this](const QJsonObject &args) { return toolPlaceAuctionBid(args); };
	_toolHandlers["get_auction_history"] = [this](const QJsonObject &args) { return toolGetAuctionHistory(args); };

	// STARS FEATURES - Marketplace
	_toolHandlers["browse_gift_marketplace"] = [this](const QJsonObject &args) { return toolBrowseGiftMarketplace(args); };
	_toolHandlers["get_market_trends"] = [this](const QJsonObject &args) { return toolGetMarketTrends(args); };
	_toolHandlers["list_gift_for_sale"] = [this](const QJsonObject &args) { return toolListGiftForSale(args); };
	_toolHandlers["update_listing"] = [this](const QJsonObject &args) { return toolUpdateListing(args); };
	_toolHandlers["cancel_listing"] = [this](const QJsonObject &args) { return toolCancelListing(args); };

	// STARS FEATURES - Star Reactions
	_toolHandlers["get_star_reactions_received"] = [this](const QJsonObject &args) { return toolGetStarReactionsReceived(args); };
	_toolHandlers["get_star_reactions_sent"] = [this](const QJsonObject &args) { return toolGetStarReactionsSent(args); };
	_toolHandlers["get_top_supporters"] = [this](const QJsonObject &args) { return toolGetTopSupporters(args); };

	// STARS FEATURES - Paid Content
	_toolHandlers["get_paid_messages_stats"] = [this](const QJsonObject &args) { return toolGetPaidMessagesStats(args); };
	_toolHandlers["configure_paid_messages"] = [this](const QJsonObject &args) { return toolConfigurePaidMessages(args); };
	_toolHandlers["get_paid_media_stats"] = [this](const QJsonObject &args) { return toolGetPaidMediaStats(args); };
	_toolHandlers["get_unlocked_content"] = [this](const QJsonObject &args) { return toolGetUnlockedContent(args); };

	// STARS FEATURES - Mini Apps
	_toolHandlers["get_miniapp_spending"] = [this](const QJsonObject &args) { return toolGetMiniappSpending(args); };
	_toolHandlers["get_miniapp_history"] = [this](const QJsonObject &args) { return toolGetMiniappHistory(args); };
	_toolHandlers["set_miniapp_budget"] = [this](const QJsonObject &args) { return toolSetMiniappBudget(args); };

	// STARS FEATURES - Star Rating
	_toolHandlers["get_star_rating_details"] = [this](const QJsonObject &args) { return toolGetStarRatingDetails(args); };
	_toolHandlers["get_rating_history"] = [this](const QJsonObject &args) { return toolGetRatingHistory(args); };
	_toolHandlers["simulate_rating_change"] = [this](const QJsonObject &args) { return toolSimulateRatingChange(args); };

	// STARS FEATURES - Profile Display
	_toolHandlers["get_profile_gifts"] = [this](const QJsonObject &args) { return toolGetProfileGifts(args); };
	_toolHandlers["update_gift_display"] = [this](const QJsonObject &args) { return toolUpdateGiftDisplay(args); };
	_toolHandlers["reorder_profile_gifts"] = [this](const QJsonObject &args) { return toolReorderProfileGifts(args); };
	_toolHandlers["toggle_gift_notifications"] = [this](const QJsonObject &args) { return toolToggleGiftNotifications(args); };

	// STARS FEATURES - AI & Analytics
	_toolHandlers["get_gift_investment_advice"] = [this](const QJsonObject &args) { return toolGetGiftInvestmentAdvice(args); };
	_toolHandlers["backtest_strategy"] = [this](const QJsonObject &args) { return toolBacktestStrategy(args); };
	_toolHandlers["get_portfolio_performance"] = [this](const QJsonObject &args) { return toolGetPortfolioPerformance(args); };
	_toolHandlers["create_price_alert"] = [this](const QJsonObject &args) { return toolCreatePriceAlert(args); };
	_toolHandlers["create_auction_alert"] = [this](const QJsonObject &args) { return toolCreateAuctionAlert(args); };
	_toolHandlers["get_fragment_listings"] = [this](const QJsonObject &args) { return toolGetFragmentListings(args); };
	_toolHandlers["export_portfolio_report"] = [this](const QJsonObject &args) { return toolExportPortfolioReport(args); };

	// ADDITIONAL PREMIUM TOOLS
	_toolHandlers["get_voice_transcription"] = [this](const QJsonObject &args) { return toolGetVoiceTranscription(args); };
	_toolHandlers["translate_message"] = [this](const QJsonObject &args) { return toolTranslateMessage(args); };
	_toolHandlers["get_translation_history"] = [this](const QJsonObject &args) { return toolGetTranslationHistory(args); };
	_toolHandlers["add_message_tag"] = [this](const QJsonObject &args) { return toolAddMessageTag(args); };
	_toolHandlers["get_message_tags"] = [this](const QJsonObject &args) { return toolGetMessageTags(args); };
	_toolHandlers["remove_message_tag"] = [this](const QJsonObject &args) { return toolRemoveMessageTag(args); };
	_toolHandlers["search_by_tag"] = [this](const QJsonObject &args) { return toolSearchByTag(args); };
	_toolHandlers["get_tag_suggestions"] = [this](const QJsonObject &args) { return toolGetTagSuggestions(args); };
	_toolHandlers["get_ad_filter_stats"] = [this](const QJsonObject &args) { return toolGetAdFilterStats(args); };
	_toolHandlers["set_chat_rules"] = [this](const QJsonObject &args) { return toolSetChatRules(args); };
	_toolHandlers["get_chat_rules"] = [this](const QJsonObject &args) { return toolGetChatRules(args); };
	_toolHandlers["test_chat_rules"] = [this](const QJsonObject &args) { return toolTestChatRules(args); };
	_toolHandlers["create_task_from_message"] = [this](const QJsonObject &args) { return toolCreateTaskFromMessage(args); };
	_toolHandlers["update_task"] = [this](const QJsonObject &args) { return toolUpdateTask(args); };

	// ADDITIONAL BUSINESS TOOLS
	_toolHandlers["update_quick_reply"] = [this](const QJsonObject &args) { return toolUpdateQuickReply(args); };
	_toolHandlers["use_quick_reply"] = [this](const QJsonObject &args) { return toolUseQuickReply(args); };
	_toolHandlers["set_greeting_message"] = [this](const QJsonObject &args) { return toolSetGreetingMessage(args); };
	_toolHandlers["get_greeting_message"] = [this](const QJsonObject &args) { return toolGetGreetingMessage(args); };
	_toolHandlers["disable_greeting"] = [this](const QJsonObject &args) { return toolDisableGreeting(args); };
	_toolHandlers["set_away_message"] = [this](const QJsonObject &args) { return toolSetAwayMessage(args); };
	_toolHandlers["get_away_message"] = [this](const QJsonObject &args) { return toolGetAwayMessage(args); };
	_toolHandlers["get_next_available_slot"] = [this](const QJsonObject &args) { return toolGetNextAvailableSlot(args); };
	_toolHandlers["check_business_status"] = [this](const QJsonObject &args) { return toolCheckBusinessStatus(args); };
	_toolHandlers["configure_chatbot"] = [this](const QJsonObject &args) { return toolConfigureChatbot(args); };
	_toolHandlers["get_chatbot_analytics"] = [this](const QJsonObject &args) { return toolGetChatbotAnalytics(args); };
	_toolHandlers["test_chatbot"] = [this](const QJsonObject &args) { return toolTestChatbot(args); };
	_toolHandlers["create_auto_reply_rule"] = [this](const QJsonObject &args) { return toolCreateAutoReplyRule(args); };
	_toolHandlers["list_auto_reply_rules"] = [this](const QJsonObject &args) { return toolListAutoReplyRules(args); };
	_toolHandlers["update_auto_reply_rule"] = [this](const QJsonObject &args) { return toolUpdateAutoReplyRule(args); };
	_toolHandlers["delete_auto_reply_rule"] = [this](const QJsonObject &args) { return toolDeleteAutoReplyRule(args); };
	_toolHandlers["test_auto_reply_rule"] = [this](const QJsonObject &args) { return toolTestAutoReplyRule(args); };
	_toolHandlers["get_auto_reply_stats"] = [this](const QJsonObject &args) { return toolGetAutoReplyStats(args); };

	// VOICE/VIDEO TOOLS
	_toolHandlers["list_voice_personas"] = [this](const QJsonObject &args) { return toolListVoicePersonas(args); };
	_toolHandlers["text_to_speech"] = [this](const QJsonObject &args) { return toolTextToSpeech(args); };
	_toolHandlers["text_to_video"] = [this](const QJsonObject &args) { return toolTextToVideo(args); };

	// ADDITIONAL WALLET TOOLS
	_toolHandlers["categorize_transaction"] = [this](const QJsonObject &args) { return toolCategorizeTransaction(args); };
	_toolHandlers["send_gift"] = [this](const QJsonObject &args) { return toolSendGift(args); };
	_toolHandlers["buy_gift"] = [this](const QJsonObject &args) { return toolBuyGift(args); };
	_toolHandlers["get_gift_history"] = [this](const QJsonObject &args) { return toolGetGiftHistory(args); };
	_toolHandlers["get_gift_suggestions"] = [this](const QJsonObject &args) { return toolGetGiftSuggestions(args); };
	_toolHandlers["get_subscription_stats"] = [this](const QJsonObject &args) { return toolGetSubscriptionStats(args); };
	_toolHandlers["get_subscriber_analytics"] = [this](const QJsonObject &args) { return toolGetSubscriberAnalytics(args); };
	_toolHandlers["get_monetization_analytics"] = [this](const QJsonObject &args) { return toolGetMonetizationAnalytics(args); };
	_toolHandlers["set_monetization_rules"] = [this](const QJsonObject &args) { return toolSetMonetizationRules(args); };
	_toolHandlers["get_earnings"] = [this](const QJsonObject &args) { return toolGetEarnings(args); };
	_toolHandlers["withdraw_earnings"] = [this](const QJsonObject &args) { return toolWithdrawEarnings(args); };
	_toolHandlers["set_spending_budget"] = [this](const QJsonObject &args) { return toolSetSpendingBudget(args); };
	_toolHandlers["set_budget_alert"] = [this](const QJsonObject &args) { return toolSetBudgetAlert(args); };
	_toolHandlers["request_stars"] = [this](const QJsonObject &args) { return toolRequestStars(args); };
	_toolHandlers["get_stars_history"] = [this](const QJsonObject &args) { return toolGetStarsHistory(args); };
	_toolHandlers["convert_stars"] = [this](const QJsonObject &args) { return toolConvertStars(args); };
	_toolHandlers["get_stars_rate"] = [this](const QJsonObject &args) { return toolGetStarsRate(args); };

	// ADDITIONAL STARS TOOLS
	_toolHandlers["create_gift_collection"] = [this](const QJsonObject &args) { return toolCreateGiftCollection(args); };
	_toolHandlers["add_to_collection"] = [this](const QJsonObject &args) { return toolAddToCollection(args); };
	_toolHandlers["remove_from_collection"] = [this](const QJsonObject &args) { return toolRemoveFromCollection(args); };
	_toolHandlers["share_collection"] = [this](const QJsonObject &args) { return toolShareCollection(args); };
	_toolHandlers["create_gift_auction"] = [this](const QJsonObject &args) { return toolCreateGiftAuction(args); };
	_toolHandlers["list_auctions"] = [this](const QJsonObject &args) { return toolListAuctions(args); };
	_toolHandlers["place_bid"] = [this](const QJsonObject &args) { return toolPlaceBid(args); };
	_toolHandlers["cancel_auction"] = [this](const QJsonObject &args) { return toolCancelAuction(args); };
	_toolHandlers["get_auction_status"] = [this](const QJsonObject &args) { return toolGetAuctionStatus(args); };
	_toolHandlers["list_marketplace"] = [this](const QJsonObject &args) { return toolListMarketplace(args); };
	_toolHandlers["delist_gift"] = [this](const QJsonObject &args) { return toolDelistGift(args); };
	_toolHandlers["list_available_gifts"] = [this](const QJsonObject &args) { return toolListAvailableGifts(args); };
	_toolHandlers["get_gift_price_history"] = [this](const QJsonObject &args) { return toolGetGiftPriceHistory(args); };
	_toolHandlers["get_price_predictions"] = [this](const QJsonObject &args) { return toolGetPricePredictions(args); };
	_toolHandlers["send_star_reaction"] = [this](const QJsonObject &args) { return toolSendStarReaction(args); };
	_toolHandlers["get_star_reactions"] = [this](const QJsonObject &args) { return toolGetStarReactions(args); };
	_toolHandlers["get_reaction_analytics"] = [this](const QJsonObject &args) { return toolGetReactionAnalytics(args); };
	_toolHandlers["set_reaction_price"] = [this](const QJsonObject &args) { return toolSetReactionPrice(args); };
	_toolHandlers["get_top_reacted"] = [this](const QJsonObject &args) { return toolGetTopReacted(args); };
	_toolHandlers["create_paid_post"] = [this](const QJsonObject &args) { return toolCreatePaidPost(args); };
	_toolHandlers["set_content_price"] = [this](const QJsonObject &args) { return toolSetContentPrice(args); };
	_toolHandlers["get_paid_content_stats"] = [this](const QJsonObject &args) { return toolGetPaidContentStats(args); };
	_toolHandlers["list_purchased_content"] = [this](const QJsonObject &args) { return toolListPurchasedContent(args); };
	_toolHandlers["unlock_content"] = [this](const QJsonObject &args) { return toolUnlockContent(args); };
	_toolHandlers["refund_content"] = [this](const QJsonObject &args) { return toolRefundContent(args); };
	_toolHandlers["get_portfolio"] = [this](const QJsonObject &args) { return toolGetPortfolio(args); };
	_toolHandlers["get_portfolio_history"] = [this](const QJsonObject &args) { return toolGetPortfolioHistory(args); };
	_toolHandlers["get_portfolio_value"] = [this](const QJsonObject &args) { return toolGetPortfolioValue(args); };
	_toolHandlers["set_price_alert"] = [this](const QJsonObject &args) { return toolSetPriceAlert(args); };
	_toolHandlers["list_achievements"] = [this](const QJsonObject &args) { return toolListAchievements(args); };
	_toolHandlers["get_achievement_progress"] = [this](const QJsonObject &args) { return toolGetAchievementProgress(args); };
	_toolHandlers["claim_achievement_reward"] = [this](const QJsonObject &args) { return toolClaimAchievementReward(args); };
	_toolHandlers["get_leaderboard"] = [this](const QJsonObject &args) { return toolGetLeaderboard(args); };
	_toolHandlers["share_achievement"] = [this](const QJsonObject &args) { return toolShareAchievement(args); };
	_toolHandlers["get_achievement_suggestions"] = [this](const QJsonObject &args) { return toolGetAchievementSuggestions(args); };
	_toolHandlers["create_exclusive_content"] = [this](const QJsonObject &args) { return toolCreateExclusiveContent(args); };
	_toolHandlers["set_subscriber_tiers"] = [this](const QJsonObject &args) { return toolSetSubscriberTiers(args); };
	_toolHandlers["send_subscriber_message"] = [this](const QJsonObject &args) { return toolSendSubscriberMessage(args); };
	_toolHandlers["get_creator_dashboard"] = [this](const QJsonObject &args) { return toolGetCreatorDashboard(args); };
	_toolHandlers["get_stars_leaderboard"] = [this](const QJsonObject &args) { return toolGetStarsLeaderboard(args); };

	// SUBSCRIPTION TOOLS
	_toolHandlers["subscribe_to_channel"] = [this](const QJsonObject &args) { return toolSubscribeToChannel(args); };
	_toolHandlers["unsubscribe_from_channel"] = [this](const QJsonObject &args) { return toolUnsubscribeFromChannel(args); };
	_toolHandlers["create_giveaway"] = [this](const QJsonObject &args) { return toolCreateGiveaway(args); };

	// MINIAPP TOOLS
	_toolHandlers["list_miniapp_permissions"] = [this](const QJsonObject &args) { return toolListMiniappPermissions(args); };
	_toolHandlers["approve_miniapp_spend"] = [this](const QJsonObject &args) { return toolApproveMiniappSpend(args); };
	_toolHandlers["revoke_miniapp_permission"] = [this](const QJsonObject &args) { return toolRevokeMiniappPermission(args); };

	// TESTING TOOLS
	_toolHandlers["test_away"] = [this](const QJsonObject &args) { return toolTestAway(args); };
}


// Tool implementations split into modular files:
// mcp_core_tools.cpp        - Core messaging (list_chats, send_message, etc.)
// mcp_archive_tools.cpp     - Archive & export tools
// mcp_analytics_tools.cpp   - Analytics tools
// mcp_search_tools.cpp      - Semantic search tools
// mcp_message_ops.cpp       - Message operations (edit, delete, forward, etc.)
// mcp_batch_tools.cpp       - Batch operations
// mcp_scheduler_tools.cpp   - Scheduler tools
// mcp_system_tools.cpp      - System & voice tools
// mcp_resource_handlers.cpp - Resource/prompt/response handlers
// mcp_bot_tools.cpp         - Bot framework & ephemeral capture
// mcp_premium_tools.cpp     - Premium equivalent features
// mcp_business_tools.cpp    - Business equivalent features
// mcp_wallet_tools.cpp      - Wallet features
// mcp_stars_tools.cpp       - Stars features
// mcp_settings_tools.cpp    - Profile/privacy/security settings
// mcp_gradual_tools.cpp     - Gradual export tools

} // namespace MCP
