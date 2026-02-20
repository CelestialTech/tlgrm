// Local LLM integration for MCP chatbot and translation tools.
// Supports Ollama and llama.cpp server (OpenAI-compatible API).
//
// This file is part of Telegram Desktop MCP integration.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtNetwork/QNetworkAccessManager>
#include <QtSql/QSqlDatabase>

namespace MCP {

// LLM backend provider
enum class LLMProvider {
	Ollama,        // Ollama daemon (localhost:11434)
	LlamaCpp,      // llama.cpp server (localhost:8080)
};

// A single message in chat format
struct ChatMessage {
	QString role;     // "system", "user", "assistant"
	QString content;
};

// LLM completion result
struct CompletionResult {
	QString text;
	QString model;
	int promptTokens = 0;
	int completionTokens = 0;
	float durationSeconds = 0.0f;
	bool success = false;
	QString error;
};

// Local LLM service
class LocalLLM : public QObject {
	Q_OBJECT

public:
	explicit LocalLLM(QObject *parent = nullptr);
	~LocalLLM();

	// Lifecycle
	bool start(QSqlDatabase *db);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Configuration
	void setProvider(LLMProvider provider) { _provider = provider; }
	void setBaseUrl(const QString &url) { _baseUrl = url; }
	void setModel(const QString &model) { _model = model; }
	void setTemperature(double temp) { _temperature = temp; }
	void setMaxTokens(int tokens) { _maxTokens = tokens; }

	[[nodiscard]] LLMProvider provider() const { return _provider; }
	[[nodiscard]] QString model() const { return _model; }
	[[nodiscard]] QString baseUrl() const { return _baseUrl; }

	// Chat completion (synchronous, blocking)
	CompletionResult complete(const QList<ChatMessage> &messages);

	// Convenience: single prompt with optional system message
	CompletionResult chat(const QString &userMessage,
	                      const QString &systemPrompt = QString());

	// Translation
	CompletionResult translate(const QString &text,
	                           const QString &targetLanguage,
	                           const QString &sourceLanguage = QString());

	// Chatbot with personality + training data context
	CompletionResult chatbot(const QString &input,
	                         const QString &personality,
	                         const QString &responseStyle,
	                         const QJsonArray &trainingExamples = QJsonArray());

	// Health check - verify backend is reachable
	bool healthCheck();

	// List available models from the backend
	QJsonArray listModels();

	// Conversation history management
	void storeConversation(const QString &botName,
	                       const QString &userInput,
	                       const QString &botResponse);
	QList<ChatMessage> getRecentConversation(const QString &botName,
	                                          int limit = 10);

	// Statistics
	struct LLMStats {
		int totalRequests = 0;
		int successfulRequests = 0;
		int failedRequests = 0;
		int totalPromptTokens = 0;
		int totalCompletionTokens = 0;
		float avgResponseTime = 0.0f;
		QDateTime lastRequest;
	};

	[[nodiscard]] LLMStats getStats() const { return _stats; }

Q_SIGNALS:
	void completionFinished(const CompletionResult &result);
	void completionFailed(const QString &error);

private:
	// HTTP request to OpenAI-compatible endpoint
	CompletionResult sendRequest(const QJsonObject &requestBody);

	// Build the base URL for API calls
	QString apiUrl(const QString &endpoint) const;

	// Auto-detect available backend
	void autoDetectProvider();

	QSqlDatabase *_db = nullptr;
	QNetworkAccessManager _networkManager;

	bool _isRunning = false;
	LLMProvider _provider = LLMProvider::Ollama;
	QString _baseUrl;         // Empty = auto-detect
	QString _model;           // Empty = auto-detect
	double _temperature = 0.7;
	int _maxTokens = 1024;

	LLMStats _stats;
};

} // namespace MCP
