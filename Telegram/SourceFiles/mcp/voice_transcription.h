// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtSql/QSqlDatabase>

namespace MCP {

// Transcription provider
enum class TranscriptionProvider {
	OpenAI,        // OpenAI Whisper API (cloud)
	WhisperCpp,    // Local whisper.cpp
	Python         // Python subprocess (faster-whisper)
};

// Whisper model size
enum class WhisperModelSize {
	Tiny,    // ~39M params, fastest
	Base,    // ~74M params
	Small,   // ~244M params
	Medium,  // ~769M params
	Large    // ~1550M params, most accurate
};

// Transcription result
struct TranscriptionResult {
	QString text;
	QString language;
	float confidence;
	float durationSeconds;
	QString modelUsed;
	QDateTime transcribedAt;
	QString provider;
	bool success;
	QString error;
};

// Voice transcription service
class VoiceTranscription : public QObject {
	Q_OBJECT

public:
	explicit VoiceTranscription(QObject *parent = nullptr);
	~VoiceTranscription();

	// Initialization
	bool start(QSqlDatabase *db);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Configuration
	void setProvider(TranscriptionProvider provider) { _provider = provider; }
	void setModelSize(WhisperModelSize size) { _modelSize = size; }
	void setOpenAIKey(const QString &apiKey) { _openaiApiKey = apiKey; }
	void setWhisperModelPath(const QString &path) { _whisperModelPath = path; }
	void setLanguage(const QString &language) { _language = language; }

	// Transcription
	TranscriptionResult transcribe(const QString &audioFilePath);
	TranscriptionResult transcribeAsync(const QString &audioFilePath);

	// Provider-specific implementations
	TranscriptionResult transcribeWithOpenAI(const QString &audioFilePath);
	TranscriptionResult transcribeWithWhisperCpp(const QString &audioFilePath);
	TranscriptionResult transcribeWithPython(const QString &audioFilePath);

	// Storage
	bool storeTranscription(qint64 messageId, qint64 chatId, const TranscriptionResult &result);
	TranscriptionResult getStoredTranscription(qint64 messageId);
	bool hasTranscription(qint64 messageId);

	// Statistics
	struct TranscriptionStats {
		int totalTranscriptions = 0;
		int successfulTranscriptions = 0;
		int failedTranscriptions = 0;
		float avgDuration = 0.0f;
		QMap<QString, int> languageDistribution;
		QDateTime lastTranscribed;
	};

	[[nodiscard]] TranscriptionStats getStats() const;

	// Export
	QJsonObject exportTranscription(const TranscriptionResult &result);

Q_SIGNALS:
	void transcriptionCompleted(const TranscriptionResult &result);
	void transcriptionFailed(const QString &error);
	void progress(int percentage);

private:
	// Helper functions
	QString prepareAudioFile(const QString &inputPath);
	QString getModelName(WhisperModelSize size) const;
	float estimateConfidence(const QString &text) const;

	// OpenAI API helpers
	QJsonObject callOpenAIWhisperAPI(const QString &audioFilePath);

	// Whisper.cpp helpers
	QString executeWhisperCpp(const QString &audioPath, const QString &modelPath);

	// Python subprocess helpers
	QString executePythonWhisper(const QString &audioPath);

	QSqlDatabase *_db = nullptr;
	QNetworkAccessManager *_networkManager = nullptr;

	bool _isRunning = false;
	TranscriptionProvider _provider = TranscriptionProvider::OpenAI;
	WhisperModelSize _modelSize = WhisperModelSize::Base;

	QString _openaiApiKey;
	QString _whisperModelPath;
	QString _language;  // Force specific language (empty = auto-detect)

	TranscriptionStats _stats;
};

} // namespace MCP
