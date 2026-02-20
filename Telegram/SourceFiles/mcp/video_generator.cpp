// Video circle generator for MCP round video tools.
// Supports SadTalker (lip-synced talking head) and FFmpeg (static image + audio).
//
// This file is part of Telegram Desktop MCP integration.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "video_generator.h"
#include "text_to_speech.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUuid>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

namespace {

// Telegram round video specs (from round_video_recorder.cpp)
constexpr int kVideoSide = 400;            // 400x400 px
constexpr int kVideoBitRate = 2 * 1024 * 1024; // 2 Mbps
constexpr int kAudioBitRate = 64 * 1024;   // 64 kbps
constexpr int kAudioSampleRate = 48000;    // 48 kHz
constexpr int kMaxDurationSec = 60;        // 60 seconds max

const QStringList kSearchPaths = {
	"/usr/local/bin",
	"/opt/homebrew/bin",
	"/usr/bin",
	QDir::homePath() + "/.local/bin",
};

const QStringList kSadTalkerPaths = {
	QDir::homePath() + "/.local/share/sadtalker",
	"/opt/sadtalker",
	QDir::homePath() + "/SadTalker",
};

} // namespace

VideoGenerator::VideoGenerator(QObject *parent)
: QObject(parent) {
}

VideoGenerator::~VideoGenerator() {
	stop();
}

bool VideoGenerator::start(QSqlDatabase *db) {
	if (_isRunning) {
		return true;
	}

	_db = db;

	if (_db) {
		QSqlQuery q(*_db);
		q.exec(
			"CREATE TABLE IF NOT EXISTS video_cache ("
			"cache_key TEXT PRIMARY KEY, "
			"video_data BLOB NOT NULL, "
			"duration_seconds REAL, "
			"provider TEXT, "
			"avatar_used TEXT, "
			"output_path TEXT, "
			"created_at INTEGER"
			")"
		);
		q.exec(
			"CREATE TABLE IF NOT EXISTS video_config ("
			"key TEXT PRIMARY KEY, "
			"value TEXT"
			")"
		);
	}

	// Find FFmpeg (required for all modes)
	_ffmpegPath = findFFmpeg();
	if (_ffmpegPath.isEmpty()) {
		fprintf(stderr, "[MCP] VideoGenerator: FFmpeg not found, cannot start\n");
		fflush(stderr);
		return false;
	}

	// Auto-detect provider
	autoDetectProvider();

	_isRunning = true;
	return true;
}

void VideoGenerator::stop() {
	_isRunning = false;
}

void VideoGenerator::autoDetectProvider() {
	// Check DB for stored SadTalker path first
	if (_db && _sadTalkerPath.isEmpty()) {
		QSqlQuery q(*_db);
		q.prepare("SELECT value FROM video_config WHERE key = 'sadtalker_path'");
		if (q.exec() && q.next()) {
			QString dbPath = q.value(0).toString();
			if (QFile::exists(dbPath + "/inference.py")) {
				_sadTalkerPath = dbPath;
			}
		}
	}

	// Check environment variable
	if (_sadTalkerPath.isEmpty()) {
		QString envPath = qEnvironmentVariable("SADTALKER_PATH");
		if (!envPath.isEmpty() && QFile::exists(envPath + "/inference.py")) {
			_sadTalkerPath = envPath;
		}
	}

	// Search known paths
	if (_sadTalkerPath.isEmpty()) {
		_sadTalkerPath = findSadTalker();
	}

	// Find Python
	if (_pythonPath.isEmpty()) {
		_pythonPath = findPython();
	}

	// Set provider based on what's available
	if (!_sadTalkerPath.isEmpty() && !_pythonPath.isEmpty()) {
		_provider = VideoProvider::SadTalker;
		fprintf(stderr, "[MCP] VideoGenerator: SadTalker detected at %s\n",
			qPrintable(_sadTalkerPath));
		fflush(stderr);
	} else {
		_provider = VideoProvider::FFmpegStill;
		fprintf(stderr, "[MCP] VideoGenerator: Using FFmpegStill fallback (SadTalker not found)\n");
		fflush(stderr);
	}
}

QString VideoGenerator::findSadTalker() {
	for (const auto &path : kSadTalkerPaths) {
		if (QFile::exists(path + "/inference.py")) {
			return path;
		}
	}
	return QString();
}

QString VideoGenerator::findPython() {
	// Try python3 first, then python
	for (const auto &name : QStringList{"python3", "python"}) {
		for (const auto &dir : kSearchPaths) {
			QString fullPath = dir + "/" + name;
			if (QFile::exists(fullPath)) {
				return fullPath;
			}
		}

		// Try which
		QProcess which;
		which.start("which", QStringList{name});
		which.waitForFinished(3000);
		if (which.exitStatus() == QProcess::NormalExit && which.exitCode() == 0) {
			QString path = QString::fromUtf8(which.readAllStandardOutput()).trimmed();
			if (!path.isEmpty()) {
				return path;
			}
		}
	}
	return QString();
}

QString VideoGenerator::findFFmpeg() {
	for (const auto &dir : kSearchPaths) {
		QString fullPath = dir + "/ffmpeg";
		if (QFile::exists(fullPath)) {
			return fullPath;
		}
	}

	QProcess which;
	which.start("which", QStringList{"ffmpeg"});
	which.waitForFinished(3000);
	if (which.exitStatus() == QProcess::NormalExit && which.exitCode() == 0) {
		QString path = QString::fromUtf8(which.readAllStandardOutput()).trimmed();
		if (!path.isEmpty()) {
			return path;
		}
	}
	return QString();
}

QString VideoGenerator::tempFilePath(const QString &extension) const {
	return QDir::tempPath() + "/mcp_video_"
		+ QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)
		+ "." + extension;
}

QString VideoGenerator::computeCacheKey(const QString &text,
                                         const QString &avatarPath,
                                         const QString &voiceId,
                                         double speed) const {
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(text.toUtf8());
	hash.addData(avatarPath.toUtf8());
	hash.addData(voiceId.toUtf8());
	hash.addData(QByteArray::number(speed, 'f', 2));
	hash.addData((_provider == VideoProvider::SadTalker) ? "sadtalker" : "ffmpeg_still");
	return hash.result().toHex();
}

VideoResult VideoGenerator::generate(const QString &text,
                                      const QString &avatarImagePath,
                                      const QString &voiceId,
                                      double speed) {
	VideoResult result;

	if (!_isRunning) {
		result.success = false;
		result.error = "VideoGenerator not started";
		return result;
	}

	if (text.isEmpty()) {
		result.success = false;
		result.error = "Empty text";
		return result;
	}

	if (!QFile::exists(avatarImagePath)) {
		result.success = false;
		result.error = QString("Avatar image not found: %1").arg(avatarImagePath);
		return result;
	}

	// Check cache
	QString cacheKey = computeCacheKey(text, avatarImagePath, voiceId, speed);
	if (hasCachedVideo(cacheKey)) {
		return getCachedVideo(cacheKey);
	}

	// Step 1: Generate TTS audio from text
	if (!_tts || !_tts->isRunning()) {
		result.success = false;
		result.error = "TextToSpeech not available for audio generation";
		return result;
	}

	auto synthesis = _tts->synthesize(text, voiceId, speed, 1.0);
	if (!synthesis.success) {
		result.success = false;
		result.error = QString("TTS failed: %1").arg(synthesis.error);
		return result;
	}

	// Step 2: Generate video from audio + avatar
	result = generateFromAudio(synthesis.outputPath, avatarImagePath);

	// Step 3: Cache the result
	if (result.success) {
		result.avatarUsed = avatarImagePath;
		result.generatedAt = QDateTime::currentDateTime();
		storeCachedVideo(cacheKey, result);
	}

	return result;
}

VideoResult VideoGenerator::generateFromAudio(const QString &audioPath,
                                                const QString &avatarImagePath) {
	VideoResult result;

	if (!QFile::exists(audioPath)) {
		result.success = false;
		result.error = QString("Audio file not found: %1").arg(audioPath);
		return result;
	}

	if (!QFile::exists(avatarImagePath)) {
		result.success = false;
		result.error = QString("Avatar image not found: %1").arg(avatarImagePath);
		return result;
	}

	// Generate with selected provider
	if (_provider == VideoProvider::SadTalker
		&& !_sadTalkerPath.isEmpty()
		&& !_pythonPath.isEmpty()) {
		result = generateWithSadTalker(audioPath, avatarImagePath);
	} else {
		result = generateStill(audioPath, avatarImagePath);
	}

	return result;
}

VideoResult VideoGenerator::generateWithSadTalker(const QString &audioPath,
                                                    const QString &imagePath) {
	VideoResult result;
	QString resultDir = QDir::tempPath() + "/mcp_sadtalker_"
		+ QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
	QDir().mkpath(resultDir);

	// Build SadTalker command
	QStringList args;
	args << _sadTalkerPath + "/inference.py"
		<< "--driven_audio" << audioPath
		<< "--source_image" << imagePath
		<< "--result_dir" << resultDir
		<< "--still"
		<< "--preprocess" << "crop"
		<< "--size" << QString::number(kVideoSide);

	QProcess process;
	process.setWorkingDirectory(_sadTalkerPath);
	process.start(_pythonPath, args);

	if (!process.waitForFinished(120000)) { // 120s timeout
		process.kill();
		result.success = false;
		result.error = "SadTalker timed out (120s)";
		return result;
	}

	if (process.exitCode() != 0) {
		result.success = false;
		QString stderr = QString::fromUtf8(process.readAllStandardError());
		result.error = QString("SadTalker failed (exit %1): %2")
			.arg(process.exitCode())
			.arg(stderr.left(500));
		return result;
	}

	// Find the output MP4 in the result directory
	QDir outDir(resultDir);
	QStringList mp4Files = outDir.entryList(QStringList{"*.mp4"}, QDir::Files);
	if (mp4Files.isEmpty()) {
		// SadTalker nests output in subdirectories
		for (const auto &subDir : outDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			QDir sub(resultDir + "/" + subDir);
			mp4Files = sub.entryList(QStringList{"*.mp4"}, QDir::Files);
			if (!mp4Files.isEmpty()) {
				resultDir = sub.absolutePath();
				break;
			}
		}
	}

	if (mp4Files.isEmpty()) {
		result.success = false;
		result.error = "SadTalker produced no output video";
		return result;
	}

	QString sadTalkerOutput = resultDir + "/" + mp4Files.first();

	// Post-process to Telegram round video format
	QString finalOutput = tempFilePath("mp4");
	if (!postProcess(sadTalkerOutput, finalOutput)) {
		result.success = false;
		result.error = "FFmpeg post-processing failed";
		return result;
	}

	// Read the final video
	QFile videoFile(finalOutput);
	if (!videoFile.open(QIODevice::ReadOnly)) {
		result.success = false;
		result.error = "Failed to read output video";
		return result;
	}

	result.videoData = videoFile.readAll();
	videoFile.close();

	result.outputPath = finalOutput;
	result.durationSeconds = getVideoDuration(finalOutput);
	result.width = kVideoSide;
	result.height = kVideoSide;
	result.provider = "sadtalker";
	result.success = true;

	// Clean up SadTalker temp directory
	QDir(resultDir).removeRecursively();

	return result;
}

VideoResult VideoGenerator::generateStill(const QString &audioPath,
                                           const QString &imagePath) {
	VideoResult result;
	QString outputPath = tempFilePath("mp4");

	// FFmpeg: combine static image + audio into round video
	QStringList args;
	args << "-loop" << "1"
		<< "-i" << imagePath
		<< "-i" << audioPath
		<< "-c:v" << "libx264"
		<< "-tune" << "stillimage"
		<< "-c:a" << "aac"
		<< "-b:a" << QString::number(kAudioBitRate / 1024) + "k"
		<< "-ar" << QString::number(kAudioSampleRate)
		<< "-ac" << "1"
		<< "-vf" << QString("crop='min(iw,ih)':'min(iw,ih)',scale=%1:%1,format=yuv420p")
			.arg(kVideoSide)
		<< "-b:v" << QString::number(kVideoBitRate / 1024) + "k"
		<< "-shortest"
		<< "-t" << QString::number(kMaxDurationSec)
		<< "-movflags" << "+faststart"
		<< "-y" << outputPath;

	QProcess process;
	process.start(_ffmpegPath, args);

	if (!process.waitForFinished(30000)) { // 30s timeout
		process.kill();
		result.success = false;
		result.error = "FFmpeg timed out (30s)";
		return result;
	}

	if (process.exitCode() != 0) {
		result.success = false;
		QString stderr = QString::fromUtf8(process.readAllStandardError());
		result.error = QString("FFmpeg failed (exit %1): %2")
			.arg(process.exitCode())
			.arg(stderr.left(500));
		return result;
	}

	// Read the output
	QFile videoFile(outputPath);
	if (!videoFile.open(QIODevice::ReadOnly)) {
		result.success = false;
		result.error = "Failed to read output video";
		return result;
	}

	result.videoData = videoFile.readAll();
	videoFile.close();

	result.outputPath = outputPath;
	result.durationSeconds = getVideoDuration(outputPath);
	result.width = kVideoSide;
	result.height = kVideoSide;
	result.provider = "ffmpeg_still";
	result.success = true;

	return result;
}

bool VideoGenerator::postProcess(const QString &inputPath,
                                  const QString &outputPath) {
	QStringList args;
	args << "-i" << inputPath
		<< "-vf" << QString("crop='min(iw,ih)':'min(iw,ih)',scale=%1:%1,format=yuv420p")
			.arg(kVideoSide)
		<< "-c:v" << "libx264"
		<< "-b:v" << QString::number(kVideoBitRate / 1024) + "k"
		<< "-c:a" << "aac"
		<< "-b:a" << QString::number(kAudioBitRate / 1024) + "k"
		<< "-ar" << QString::number(kAudioSampleRate)
		<< "-ac" << "1"
		<< "-t" << QString::number(kMaxDurationSec)
		<< "-movflags" << "+faststart"
		<< "-y" << outputPath;

	QProcess process;
	process.start(_ffmpegPath, args);

	if (!process.waitForFinished(60000)) { // 60s timeout
		process.kill();
		return false;
	}

	return (process.exitCode() == 0);
}

float VideoGenerator::getVideoDuration(const QString &videoPath) {
	// Find ffprobe next to ffmpeg
	QString ffprobePath = _ffmpegPath;
	ffprobePath.replace("ffmpeg", "ffprobe");

	if (!QFile::exists(ffprobePath)) {
		// Estimate from file size (~250KB/s for our bitrate settings)
		QFileInfo fi(videoPath);
		if (fi.exists()) {
			return fi.size() / 250000.0f;
		}
		return 0.0f;
	}

	QStringList args;
	args << "-v" << "error"
		<< "-show_entries" << "format=duration"
		<< "-of" << "default=noprint_wrappers=1:nokey=1"
		<< videoPath;

	QProcess process;
	process.start(ffprobePath, args);
	process.waitForFinished(5000);

	if (process.exitCode() == 0) {
		QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
		bool ok = false;
		float duration = output.toFloat(&ok);
		if (ok) {
			return duration;
		}
	}

	return 0.0f;
}

bool VideoGenerator::hasCachedVideo(const QString &cacheKey) {
	if (!_db) return false;

	QSqlQuery q(*_db);
	q.prepare("SELECT 1 FROM video_cache WHERE cache_key = ?");
	q.addBindValue(cacheKey);
	return q.exec() && q.next();
}

VideoResult VideoGenerator::getCachedVideo(const QString &cacheKey) {
	VideoResult result;
	if (!_db) {
		result.success = false;
		result.error = "No database";
		return result;
	}

	QSqlQuery q(*_db);
	q.prepare("SELECT video_data, duration_seconds, provider, avatar_used, output_path "
	          "FROM video_cache WHERE cache_key = ?");
	q.addBindValue(cacheKey);

	if (q.exec() && q.next()) {
		result.videoData = q.value(0).toByteArray();
		result.durationSeconds = q.value(1).toFloat();
		result.provider = q.value(2).toString();
		result.avatarUsed = q.value(3).toString();
		result.outputPath = q.value(4).toString();
		result.width = kVideoSide;
		result.height = kVideoSide;
		result.generatedAt = QDateTime::currentDateTime();
		result.success = true;
	} else {
		result.success = false;
		result.error = "Cache miss";
	}

	return result;
}

void VideoGenerator::storeCachedVideo(const QString &cacheKey,
                                       const VideoResult &result) {
	if (!_db || !result.success) return;

	QSqlQuery q(*_db);
	q.prepare(
		"INSERT OR REPLACE INTO video_cache "
		"(cache_key, video_data, duration_seconds, provider, avatar_used, output_path, created_at) "
		"VALUES (?, ?, ?, ?, ?, ?, ?)"
	);
	q.addBindValue(cacheKey);
	q.addBindValue(result.videoData);
	q.addBindValue(result.durationSeconds);
	q.addBindValue(result.provider);
	q.addBindValue(result.avatarUsed);
	q.addBindValue(result.outputPath);
	q.addBindValue(QDateTime::currentSecsSinceEpoch());
	q.exec();
}

} // namespace MCP
