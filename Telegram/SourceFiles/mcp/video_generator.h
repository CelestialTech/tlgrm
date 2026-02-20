// Video circle generator for MCP round video tools.
// Supports SadTalker (lip-synced talking head) and FFmpeg (static image + audio).
//
// This file is part of Telegram Desktop MCP integration.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtSql/QSqlDatabase>

namespace MCP {

class TextToSpeech;

// Video generation backend
enum class VideoProvider {
	SadTalker,    // Python subprocess, lip-synced talking head from image + audio
	FFmpegStill,  // Static avatar image + audio combined via FFmpeg (no lip sync)
};

// Video generation result
struct VideoResult {
	QByteArray videoData;      // Raw MP4 bytes ready for Telegram
	QString outputPath;        // Path to generated MP4 on disk
	float durationSeconds = 0.0f;
	int width = 400;
	int height = 400;
	QString provider;
	QString avatarUsed;
	QDateTime generatedAt;
	bool success = false;
	QString error;
};

// Video circle generator service
class VideoGenerator : public QObject {
	Q_OBJECT

public:
	explicit VideoGenerator(QObject *parent = nullptr);
	~VideoGenerator();

	// Lifecycle
	bool start(QSqlDatabase *db);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Configuration
	void setProvider(VideoProvider provider) { _provider = provider; }
	void setSadTalkerPath(const QString &path) { _sadTalkerPath = path; }
	void setPythonPath(const QString &path) { _pythonPath = path; }
	void setTextToSpeech(TextToSpeech *tts) { _tts = tts; }

	[[nodiscard]] VideoProvider provider() const { return _provider; }
	[[nodiscard]] QString sadTalkerPath() const { return _sadTalkerPath; }

	// Full pipeline: text → TTS audio → video generation → post-process
	VideoResult generate(const QString &text,
	                     const QString &avatarImagePath,
	                     const QString &voiceId = QString(),
	                     double speed = 1.0);

	// Generate from existing audio file + avatar image
	VideoResult generateFromAudio(const QString &audioPath,
	                               const QString &avatarImagePath);

	// Provider-specific implementations
	VideoResult generateWithSadTalker(const QString &audioPath,
	                                   const QString &imagePath);
	VideoResult generateStill(const QString &audioPath,
	                           const QString &imagePath);

	// Post-process to Telegram round video specs (400x400, H.264+AAC)
	bool postProcess(const QString &inputPath, const QString &outputPath);

	// Cache
	bool hasCachedVideo(const QString &cacheKey);
	VideoResult getCachedVideo(const QString &cacheKey);
	void storeCachedVideo(const QString &cacheKey, const VideoResult &result);

	// Duration detection
	float getVideoDuration(const QString &videoPath);

Q_SIGNALS:
	void generationCompleted(const VideoResult &result);
	void generationFailed(const QString &error);
	void progress(int percentage);

private:
	// Discovery
	void autoDetectProvider();
	QString findSadTalker();
	QString findPython();
	QString findFFmpeg();

	// Helpers
	QString tempFilePath(const QString &extension) const;
	QString computeCacheKey(const QString &text,
	                        const QString &avatarPath,
	                        const QString &voiceId,
	                        double speed) const;

	QSqlDatabase *_db = nullptr;
	TextToSpeech *_tts = nullptr;  // Non-owning, set by MCP::Server

	bool _isRunning = false;
	VideoProvider _provider = VideoProvider::FFmpegStill;

	QString _sadTalkerPath;  // Path to SadTalker repo (with inference.py)
	QString _pythonPath;     // Path to python binary
	QString _ffmpegPath;     // Path to ffmpeg binary
};

} // namespace MCP
