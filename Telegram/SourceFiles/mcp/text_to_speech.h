// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtSql/QSqlDatabase>

namespace MCP {

// TTS provider enum (mirrors TranscriptionProvider pattern)
enum class TTSProvider {
	PiperTTS,     // Fast ONNX-based TTS, local binary
	EspeakNG,     // Lightweight, robotic quality
	CoquiPython   // Python subprocess, supports voice cloning via XTTS-v2
};

// Synthesis result (mirrors TranscriptionResult pattern)
struct SynthesisResult {
	QByteArray audioData;      // Raw OGG Opus bytes ready for Telegram
	QString outputPath;        // Path to the generated OGG file on disk
	float durationSeconds = 0.0f;
	int sampleRate = 48000;
	QString provider;          // Provider name string
	QString voiceUsed;         // Voice model/ID used
	QDateTime generatedAt;
	bool success = false;
	QString error;
};

// Voice synthesis service
class TextToSpeech : public QObject {
	Q_OBJECT

public:
	explicit TextToSpeech(QObject *parent = nullptr);
	~TextToSpeech();

	// Initialization (mirrors VoiceTranscription::start)
	bool start(QSqlDatabase *db);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Configuration
	void setProvider(TTSProvider provider) { _provider = provider; }
	void setPiperBinaryPath(const QString &path) { _piperBinaryPath = path; }
	void setPiperModelPath(const QString &path) { _piperModelPath = path; }
	void setLanguage(const QString &language) { _language = language; }

	// Core synthesis
	SynthesisResult synthesize(
		const QString &text,
		const QString &voiceId = QString(),
		double speed = 1.0,
		double pitch = 1.0);

	// Provider-specific implementations
	SynthesisResult synthesizeWithPiper(
		const QString &text,
		const QString &voiceId,
		double speed);
	SynthesisResult synthesizeWithEspeak(
		const QString &text,
		const QString &voiceId,
		double speed,
		double pitch);
	SynthesisResult synthesizeWithCoqui(
		const QString &text,
		const QString &voiceId,
		double speed);

	// Voice cloning (Coqui XTTS-v2 only)
	SynthesisResult cloneAndSynthesize(
		const QString &text,
		const QString &speakerWavPath,
		double speed = 1.0);

	// Cache operations
	bool hasCachedAudio(const QString &cacheKey);
	SynthesisResult getCachedAudio(const QString &cacheKey);
	bool storeCachedAudio(const QString &cacheKey, const SynthesisResult &result);

	// Statistics (mirrors VoiceTranscription::TranscriptionStats)
	struct TTSStats {
		int totalSyntheses = 0;
		int successfulSyntheses = 0;
		int failedSyntheses = 0;
		int cacheHits = 0;
		float avgDurationSeconds = 0.0f;
		QDateTime lastGenerated;
	};

	[[nodiscard]] TTSStats getStats() const;

	// Utility
	static QString computeCacheKey(
		const QString &text,
		const QString &voiceId,
		double speed,
		double pitch);

Q_SIGNALS:
	void synthesisCompleted(const SynthesisResult &result);
	void synthesisFailed(const QString &error);
	void progress(int percentage);

private:
	// Audio conversion: WAV -> OGG Opus via FFmpeg
	bool convertToOpus(
		const QString &inputWavPath,
		const QString &outputOggPath);

	// Read audio file duration (via ffprobe)
	float getAudioDuration(const QString &filePath);

	// Find provider binary on system
	QString findBinary(const QString &name) const;

	// Generate a unique temp file path
	QString tempFilePath(const QString &suffix) const;

	QSqlDatabase *_db = nullptr;
	bool _isRunning = false;
	TTSProvider _provider = TTSProvider::PiperTTS;

	QString _piperBinaryPath;   // Path to piper binary
	QString _piperModelPath;    // Path to .onnx model file
	QString _language = "en";

	TTSStats _stats;
};

} // namespace MCP
