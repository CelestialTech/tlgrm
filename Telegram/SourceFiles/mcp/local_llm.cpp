// Local LLM integration for MCP chatbot and translation tools.
// Supports Ollama and llama.cpp server (OpenAI-compatible API).
//
// This file is part of Telegram Desktop MCP integration.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "local_llm.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QEventLoop>
#include <QtCore/QElapsedTimer>
#include <QtCore/QProcess>
#include <QtCore/QDebug>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

LocalLLM::LocalLLM(QObject *parent)
: QObject(parent) {
}

LocalLLM::~LocalLLM() {
	stop();
}

bool LocalLLM::start(QSqlDatabase *db) {
	if (_isRunning) {
		return true;
	}

	_db = db;

	if (_db) {
		QSqlQuery q(*_db);
		q.exec(
			"CREATE TABLE IF NOT EXISTS chatbot_conversations ("
			"id INTEGER PRIMARY KEY AUTOINCREMENT, "
			"bot_name TEXT NOT NULL, "
			"role TEXT NOT NULL, "
			"content TEXT NOT NULL, "
			"created_at TEXT DEFAULT (datetime('now'))"
			")"
		);
		q.exec(
			"CREATE INDEX IF NOT EXISTS idx_chatbot_conv_bot "
			"ON chatbot_conversations(bot_name, created_at DESC)"
		);
	}

	// Auto-detect provider if no base URL set
	if (_baseUrl.isEmpty()) {
		autoDetectProvider();
	}

	_isRunning = true;
	return true;
}

void LocalLLM::stop() {
	_isRunning = false;
}

void LocalLLM::autoDetectProvider() {
	// Try Ollama first (default port 11434)
	{
		QNetworkRequest req(QUrl("http://localhost:11434/api/tags"));
		req.setTransferTimeout(3000);
		QNetworkReply *reply = _networkManager.get(req);

		QEventLoop loop;
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		loop.exec();

		if (reply->error() == QNetworkReply::NoError) {
			_provider = LLMProvider::Ollama;
			_baseUrl = "http://localhost:11434";

			// Auto-detect best model
			if (_model.isEmpty()) {
				QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
				QJsonArray models = doc.object()["models"].toArray();
				// Prefer llama3.1, then mistral, then first available
				for (const auto &m : models) {
					QString name = m.toObject()["name"].toString();
					if (name.startsWith("llama3")) {
						_model = name;
						break;
					}
				}
				if (_model.isEmpty()) {
					for (const auto &m : models) {
						QString name = m.toObject()["name"].toString();
						if (name.startsWith("mistral")) {
							_model = name;
							break;
						}
					}
				}
				if (_model.isEmpty() && !models.isEmpty()) {
					_model = models.first().toObject()["name"].toString();
				}
			}

			reply->deleteLater();
			fprintf(stderr, "[MCP] LocalLLM: Detected Ollama at %s, model: %s\n",
				qPrintable(_baseUrl), qPrintable(_model));
			fflush(stderr);
			return;
		}
		reply->deleteLater();
	}

	// Try llama.cpp server (default port 8080)
	{
		QNetworkRequest req(QUrl("http://localhost:8080/health"));
		req.setTransferTimeout(3000);
		QNetworkReply *reply = _networkManager.get(req);

		QEventLoop loop;
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		loop.exec();

		if (reply->error() == QNetworkReply::NoError) {
			_provider = LLMProvider::LlamaCpp;
			_baseUrl = "http://localhost:8080";
			if (_model.isEmpty()) {
				_model = "default";
			}
			reply->deleteLater();
			fprintf(stderr, "[MCP] LocalLLM: Detected llama.cpp server at %s\n",
				qPrintable(_baseUrl));
			fflush(stderr);
			return;
		}
		reply->deleteLater();
	}

	// No backend found - set defaults, will fail gracefully on use
	_provider = LLMProvider::Ollama;
	_baseUrl = "http://localhost:11434";
	if (_model.isEmpty()) {
		_model = "llama3.1:8b";
	}
	fprintf(stderr, "[MCP] LocalLLM: No backend detected, defaulting to Ollama (%s)\n",
		qPrintable(_model));
	fflush(stderr);
}

QString LocalLLM::apiUrl(const QString &endpoint) const {
	// Both Ollama and llama.cpp support OpenAI-compatible /v1/chat/completions
	if (_provider == LLMProvider::Ollama) {
		return _baseUrl + "/v1" + endpoint;
	}
	// llama.cpp server uses /v1 prefix too
	return _baseUrl + "/v1" + endpoint;
}

CompletionResult LocalLLM::sendRequest(const QJsonObject &requestBody) {
	CompletionResult result;
	QElapsedTimer timer;
	timer.start();

	QString url = apiUrl("/chat/completions");
	QUrl requestUrl{url};
	QNetworkRequest req{requestUrl};
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	req.setTransferTimeout(120000); // 2 min timeout for LLM generation

	QByteArray body = QJsonDocument(requestBody).toJson(QJsonDocument::Compact);
	QNetworkReply *reply = _networkManager.post(req, body);

	QEventLoop loop;
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();

	result.durationSeconds = timer.elapsed() / 1000.0f;

	if (reply->error() != QNetworkReply::NoError) {
		result.success = false;
		result.error = QString("HTTP error: %1 - %2")
			.arg(reply->error())
			.arg(reply->errorString());

		// Try to read error body for more detail
		QByteArray errorBody = reply->readAll();
		if (!errorBody.isEmpty()) {
			QJsonDocument errorDoc = QJsonDocument::fromJson(errorBody);
			if (!errorDoc.isNull()) {
				QString errorMsg = errorDoc.object()["error"].toObject()["message"].toString();
				if (!errorMsg.isEmpty()) {
					result.error = errorMsg;
				}
			}
		}

		reply->deleteLater();
		_stats.totalRequests++;
		_stats.failedRequests++;
		_stats.lastRequest = QDateTime::currentDateTime();
		return result;
	}

	QByteArray responseBody = reply->readAll();
	reply->deleteLater();

	QJsonDocument doc = QJsonDocument::fromJson(responseBody);
	if (doc.isNull()) {
		result.success = false;
		result.error = "Failed to parse JSON response";
		_stats.totalRequests++;
		_stats.failedRequests++;
		_stats.lastRequest = QDateTime::currentDateTime();
		return result;
	}

	QJsonObject responseObj = doc.object();

	// Parse OpenAI-compatible response
	QJsonArray choices = responseObj["choices"].toArray();
	if (!choices.isEmpty()) {
		QJsonObject firstChoice = choices.first().toObject();
		QJsonObject message = firstChoice["message"].toObject();
		result.text = message["content"].toString().trimmed();
	}

	result.model = responseObj["model"].toString();

	// Parse usage stats
	QJsonObject usage = responseObj["usage"].toObject();
	result.promptTokens = usage["prompt_tokens"].toInt();
	result.completionTokens = usage["completion_tokens"].toInt();

	result.success = !result.text.isEmpty();
	if (!result.success && result.error.isEmpty()) {
		result.error = "Empty response from model";
	}

	// Update stats
	_stats.totalRequests++;
	if (result.success) {
		_stats.successfulRequests++;
		_stats.totalPromptTokens += result.promptTokens;
		_stats.totalCompletionTokens += result.completionTokens;
		// Running average response time
		float totalTime = _stats.avgResponseTime * (_stats.successfulRequests - 1)
			+ result.durationSeconds;
		_stats.avgResponseTime = totalTime / _stats.successfulRequests;
	} else {
		_stats.failedRequests++;
	}
	_stats.lastRequest = QDateTime::currentDateTime();

	return result;
}

CompletionResult LocalLLM::complete(const QList<ChatMessage> &messages) {
	if (!_isRunning) {
		CompletionResult r;
		r.success = false;
		r.error = "LocalLLM not started";
		return r;
	}

	QJsonArray messagesArray;
	for (const auto &msg : messages) {
		QJsonObject m;
		m["role"] = msg.role;
		m["content"] = msg.content;
		messagesArray.append(m);
	}

	QJsonObject requestBody;
	requestBody["model"] = _model;
	requestBody["messages"] = messagesArray;
	requestBody["temperature"] = _temperature;
	requestBody["max_tokens"] = _maxTokens;
	requestBody["stream"] = false;

	return sendRequest(requestBody);
}

CompletionResult LocalLLM::chat(const QString &userMessage,
                                 const QString &systemPrompt) {
	QList<ChatMessage> messages;

	if (!systemPrompt.isEmpty()) {
		messages.append({"system", systemPrompt});
	}
	messages.append({"user", userMessage});

	return complete(messages);
}

CompletionResult LocalLLM::translate(const QString &text,
                                      const QString &targetLanguage,
                                      const QString &sourceLanguage) {
	QString systemPrompt =
		"You are a professional translator. Translate the following text "
		"accurately and naturally. Output ONLY the translation, nothing else. "
		"Do not add explanations, notes, or quotation marks around the translation.";

	QString userMsg;
	if (!sourceLanguage.isEmpty()) {
		userMsg = QString("Translate from %1 to %2:\n\n%3")
			.arg(sourceLanguage, targetLanguage, text);
	} else {
		userMsg = QString("Translate to %1:\n\n%2")
			.arg(targetLanguage, text);
	}

	// Use lower temperature for translation (more deterministic)
	double savedTemp = _temperature;
	_temperature = 0.3;
	auto result = chat(userMsg, systemPrompt);
	_temperature = savedTemp;

	return result;
}

CompletionResult LocalLLM::chatbot(const QString &input,
                                    const QString &personality,
                                    const QString &responseStyle,
                                    const QJsonArray &trainingExamples) {
	QList<ChatMessage> messages;

	// Build system prompt from personality + style + training data
	QString systemPrompt = QString(
		"You are a chatbot assistant with the following personality: %1\n"
		"Response style: %2\n"
	).arg(personality, responseStyle);

	// Add training examples as few-shot context
	if (!trainingExamples.isEmpty()) {
		systemPrompt += "\nHere are some example conversations to guide your responses:\n";
		int exampleCount = 0;
		for (const auto &example : trainingExamples) {
			QJsonObject ex = example.toObject();
			QString exInput = ex["input"].toString();
			QString exOutput = ex["output"].toString();
			if (!exInput.isEmpty() && !exOutput.isEmpty()) {
				systemPrompt += QString("User: %1\nAssistant: %2\n\n")
					.arg(exInput, exOutput);
				exampleCount++;
				if (exampleCount >= 10) break; // Cap at 10 examples
			}
		}
	}

	systemPrompt += "\nRespond naturally and stay in character.";

	messages.append({"system", systemPrompt});
	messages.append({"user", input});

	return complete(messages);
}

bool LocalLLM::healthCheck() {
	if (_baseUrl.isEmpty()) {
		return false;
	}

	QString healthUrl;
	if (_provider == LLMProvider::Ollama) {
		healthUrl = _baseUrl + "/api/tags";
	} else {
		healthUrl = _baseUrl + "/health";
	}

	QUrl healthReqUrl{healthUrl};
	QNetworkRequest req{healthReqUrl};
	req.setTransferTimeout(5000);
	QNetworkReply *reply = _networkManager.get(req);

	QEventLoop loop;
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();

	bool ok = (reply->error() == QNetworkReply::NoError);
	reply->deleteLater();
	return ok;
}

QJsonArray LocalLLM::listModels() {
	QJsonArray result;

	QString modelsUrl;
	if (_provider == LLMProvider::Ollama) {
		modelsUrl = _baseUrl + "/api/tags";
	} else {
		modelsUrl = _baseUrl + "/v1/models";
	}

	QUrl modelsReqUrl{modelsUrl};
	QNetworkRequest req{modelsReqUrl};
	req.setTransferTimeout(5000);
	QNetworkReply *reply = _networkManager.get(req);

	QEventLoop loop;
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();

	if (reply->error() == QNetworkReply::NoError) {
		QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
		QJsonObject obj = doc.object();

		if (_provider == LLMProvider::Ollama) {
			QJsonArray models = obj["models"].toArray();
			for (const auto &m : models) {
				QJsonObject model = m.toObject();
				QJsonObject info;
				info["name"] = model["name"].toString();
				info["size"] = model["size"].toDouble();
				info["modified_at"] = model["modified_at"].toString();
				QString details;
				QJsonObject d = model["details"].toObject();
				if (!d.isEmpty()) {
					info["family"] = d["family"].toString();
					info["parameter_size"] = d["parameter_size"].toString();
					info["quantization_level"] = d["quantization_level"].toString();
				}
				result.append(info);
			}
		} else {
			// llama.cpp /v1/models format
			QJsonArray data = obj["data"].toArray();
			for (const auto &m : data) {
				QJsonObject model = m.toObject();
				QJsonObject info;
				info["name"] = model["id"].toString();
				result.append(info);
			}
		}
	}

	reply->deleteLater();
	return result;
}

void LocalLLM::storeConversation(const QString &botName,
                                  const QString &userInput,
                                  const QString &botResponse) {
	if (!_db) return;

	QSqlQuery q(*_db);
	q.prepare(
		"INSERT INTO chatbot_conversations (bot_name, role, content) "
		"VALUES (?, 'user', ?)"
	);
	q.addBindValue(botName);
	q.addBindValue(userInput);
	q.exec();
	q.finish();

	q.prepare(
		"INSERT INTO chatbot_conversations (bot_name, role, content) "
		"VALUES (?, 'assistant', ?)"
	);
	q.addBindValue(botName);
	q.addBindValue(botResponse);
	q.exec();
}

QList<ChatMessage> LocalLLM::getRecentConversation(const QString &botName,
                                                     int limit) {
	QList<ChatMessage> messages;
	if (!_db) return messages;

	QSqlQuery q(*_db);
	q.prepare(
		"SELECT role, content FROM ("
		"  SELECT role, content, created_at "
		"  FROM chatbot_conversations "
		"  WHERE bot_name = ? "
		"  ORDER BY created_at DESC "
		"  LIMIT ?"
		") sub ORDER BY created_at ASC"
	);
	q.addBindValue(botName);
	q.addBindValue(limit * 2); // limit pairs -> limit*2 messages

	if (q.exec()) {
		while (q.next()) {
			ChatMessage msg;
			msg.role = q.value(0).toString();
			msg.content = q.value(1).toString();
			messages.append(msg);
		}
	}

	return messages;
}

} // namespace MCP
