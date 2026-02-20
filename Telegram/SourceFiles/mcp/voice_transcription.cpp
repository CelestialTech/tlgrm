// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "voice_transcription.h"

#include <QtCore/QProcess>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QRegularExpression>
#include <QtConcurrent/QtConcurrent>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QHttpMultiPart>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

VoiceTranscription::VoiceTranscription(QObject *parent)
	: QObject(parent)
	, _networkManager(new QNetworkAccessManager(this)) {
}

VoiceTranscription::~VoiceTranscription() {
	stop();
}

bool VoiceTranscription::start(QSqlDatabase *db) {
	if (_isRunning) {
		return true;
	}

	_db = db;
	_isRunning = true;

	return true;
}

void VoiceTranscription::stop() {
	if (!_isRunning) {
		return;
	}

	_db = nullptr;
	_isRunning = false;
}

// Main transcription function
TranscriptionResult VoiceTranscription::transcribe(const QString &audioFilePath) {
	auto startTime = QDateTime::currentDateTime();

	TranscriptionResult result;
	result.transcribedAt = startTime;

	// Prepare audio file (convert if necessary)
	QString preparedFile = prepareAudioFile(audioFilePath);
	if (preparedFile.isEmpty()) {
		result.success = false;
		result.error = "Failed to prepare audio file";
		return result;
	}

	// Call appropriate provider
	switch (_provider) {
	case TranscriptionProvider::OpenAI:
		result = transcribeWithOpenAI(preparedFile);
		result.provider = "OpenAI Whisper API";
		break;

	case TranscriptionProvider::WhisperCpp:
		result = transcribeWithWhisperCpp(preparedFile);
		result.provider = "whisper.cpp";
		break;

	case TranscriptionProvider::Python:
		result = transcribeWithPython(preparedFile);
		result.provider = "Python (faster-whisper)";
		break;
	}

	// Update statistics
	if (result.success) {
		_stats.successfulTranscriptions++;
		_stats.languageDistribution[result.language]++;
	} else {
		_stats.failedTranscriptions++;
	}
	_stats.totalTranscriptions++;
	_stats.lastTranscribed = QDateTime::currentDateTime();

	// Calculate average duration
	if (_stats.successfulTranscriptions > 0) {
		_stats.avgDuration = (_stats.avgDuration * (_stats.successfulTranscriptions - 1) +
		                      result.durationSeconds) / _stats.successfulTranscriptions;
	}

	Q_EMIT transcriptionCompleted(result);

	return result;
}

// Async transcription — runs synchronous version on a worker thread
TranscriptionResult VoiceTranscription::transcribeAsync(const QString &audioFilePath) {
	// Launch transcription on a background thread via QtConcurrent
	QFuture<TranscriptionResult> future = QtConcurrent::run([this, audioFilePath]() {
		return transcribe(audioFilePath);
	});

	// Return an in-progress result; callers should connect to transcriptionCompleted signal
	TranscriptionResult pending;
	pending.success = false;
	pending.error = "Transcription in progress (async)";
	return pending;
}

// OpenAI Whisper API implementation
TranscriptionResult VoiceTranscription::transcribeWithOpenAI(const QString &audioFilePath) {
	TranscriptionResult result;
	result.success = false;
	result.modelUsed = "whisper-1";

	if (_openaiApiKey.isEmpty()) {
		result.error = "OpenAI API key not configured";
		return result;
	}

	QFile audioFile(audioFilePath);
	if (!audioFile.open(QIODevice::ReadOnly)) {
		result.error = "Failed to open audio file";
		return result;
	}

	// Create multipart form data
	QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

	// Add file
	QHttpPart filePart;
	filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("audio/ogg"));
	filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
	                   QVariant("form-data; name=\"file\"; filename=\"audio.ogg\""));
	filePart.setBody(audioFile.readAll());
	multiPart->append(filePart);

	audioFile.close();

	// Add model
	QHttpPart modelPart;
	modelPart.setHeader(QNetworkRequest::ContentDispositionHeader,
	                    QVariant("form-data; name=\"model\""));
	modelPart.setBody("whisper-1");
	multiPart->append(modelPart);

	// Add language if specified
	if (!_language.isEmpty()) {
		QHttpPart langPart;
		langPart.setHeader(QNetworkRequest::ContentDispositionHeader,
		                   QVariant("form-data; name=\"language\""));
		langPart.setBody(_language.toUtf8());
		multiPart->append(langPart);
	}

	// Create request
	QNetworkRequest request(QUrl("https://api.openai.com/v1/audio/transcriptions"));
	request.setRawHeader("Authorization", QString("Bearer %1").arg(_openaiApiKey).toUtf8());

	// Send request
	QNetworkReply *reply = _networkManager->post(request, multiPart);
	multiPart->setParent(reply);

	// Wait for response (blocking)
	QEventLoop loop;
	connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();

	if (reply->error() != QNetworkReply::NoError) {
		result.error = "API request failed: " + reply->errorString();
		reply->deleteLater();
		return result;
	}

	// Parse response
	QByteArray response = reply->readAll();
	reply->deleteLater();

	QJsonDocument doc = QJsonDocument::fromJson(response);
	if (!doc.isObject()) {
		result.error = "Invalid API response";
		return result;
	}

	QJsonObject obj = doc.object();
	result.text = obj["text"].toString();
	result.language = obj.value("language").toString("unknown");
	result.confidence = estimateConfidence(result.text);
	result.durationSeconds = 0.0f;  // OpenAI doesn't return duration
	result.success = !result.text.isEmpty();

	return result;
}

// Whisper.cpp implementation
TranscriptionResult VoiceTranscription::transcribeWithWhisperCpp(const QString &audioFilePath) {
	TranscriptionResult result;
	result.success = false;

	if (_whisperModelPath.isEmpty()) {
		result.error = "Whisper model path not configured";
		return result;
	}

	QString modelName = getModelName(_modelSize);
	result.modelUsed = "whisper.cpp (" + modelName + ")";

	// Execute whisper.cpp
	QString output = executeWhisperCpp(audioFilePath, _whisperModelPath);

	if (output.isEmpty()) {
		result.error = "whisper.cpp execution failed";
		return result;
	}

	// Parse output (whisper.cpp outputs plain text + metadata)
	// Format: "[LANGUAGE: en] Transcribed text here"
	QRegularExpression langRegex(R"(\[LANGUAGE:\s*(\w+)\])");
	QRegularExpressionMatch match = langRegex.match(output);

	if (match.hasMatch()) {
		result.language = match.captured(1);
		result.text = output.mid(match.capturedEnd()).trimmed();
	} else {
		result.language = _language.isEmpty() ? "unknown" : _language;
		result.text = output.trimmed();
	}

	result.confidence = estimateConfidence(result.text);
	result.success = !result.text.isEmpty();

	return result;
}

// Python subprocess implementation
TranscriptionResult VoiceTranscription::transcribeWithPython(const QString &audioFilePath) {
	TranscriptionResult result;
	result.success = false;

	QString modelName = getModelName(_modelSize);
	result.modelUsed = "faster-whisper (" + modelName + ")";

	// Execute Python script
	QString output = executePythonWhisper(audioFilePath);

	if (output.isEmpty()) {
		result.error = "Python whisper execution failed";
		return result;
	}

	// Parse JSON output from Python script
	QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8());
	if (!doc.isObject()) {
		result.error = "Invalid Python output";
		return result;
	}

	QJsonObject obj = doc.object();
	result.text = obj["text"].toString();
	result.language = obj["language"].toString();
	result.confidence = obj["confidence"].toDouble();
	result.durationSeconds = obj["duration"].toDouble();
	result.success = obj["success"].toBool();

	if (!result.success) {
		result.error = obj["error"].toString();
	}

	return result;
}

// Store transcription in database
bool VoiceTranscription::storeTranscription(
		qint64 messageId,
		qint64 chatId,
		const TranscriptionResult &result) {

	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare(R"(
		INSERT OR REPLACE INTO voice_transcriptions (
			message_id, chat_id, transcription_text, language,
			confidence, duration_seconds, model, created_at
		) VALUES (
			:message_id, :chat_id, :text, :language,
			:confidence, :duration, :model, :created_at
		)
	)");

	query.bindValue(":message_id", messageId);
	query.bindValue(":chat_id", chatId);
	query.bindValue(":text", result.text);
	query.bindValue(":language", result.language);
	query.bindValue(":confidence", result.confidence);
	query.bindValue(":duration", result.durationSeconds);
	query.bindValue(":model", result.modelUsed);
	query.bindValue(":created_at", result.transcribedAt.toSecsSinceEpoch());

	return query.exec();
}

// Get stored transcription
TranscriptionResult VoiceTranscription::getStoredTranscription(qint64 messageId) {
	TranscriptionResult result;
	result.success = false;

	if (!_db || !_db->isOpen()) {
		return result;
	}

	QSqlQuery query(*_db);
	query.prepare("SELECT * FROM voice_transcriptions WHERE message_id = :message_id");
	query.bindValue(":message_id", messageId);

	if (query.exec() && query.next()) {
		result.text = query.value("transcription_text").toString();
		result.language = query.value("language").toString();
		result.confidence = query.value("confidence").toFloat();
		result.durationSeconds = query.value("duration_seconds").toFloat();
		result.modelUsed = query.value("model").toString();
		result.transcribedAt = QDateTime::fromSecsSinceEpoch(query.value("created_at").toLongLong());
		result.success = true;
	}

	return result;
}

// Check if transcription exists
bool VoiceTranscription::hasTranscription(qint64 messageId) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare("SELECT COUNT(*) FROM voice_transcriptions WHERE message_id = :message_id");
	query.bindValue(":message_id", messageId);

	if (query.exec() && query.next()) {
		return query.value(0).toInt() > 0;
	}

	return false;
}

// Get statistics
VoiceTranscription::TranscriptionStats VoiceTranscription::getStats() const {
	return _stats;
}

// Export transcription
QJsonObject VoiceTranscription::exportTranscription(const TranscriptionResult &result) {
	QJsonObject json;
	json["text"] = result.text;
	json["language"] = result.language;
	json["confidence"] = result.confidence;
	json["duration_seconds"] = result.durationSeconds;
	json["model"] = result.modelUsed;
	json["provider"] = result.provider;
	json["transcribed_at"] = result.transcribedAt.toString(Qt::ISODate);
	json["success"] = result.success;

	if (!result.error.isEmpty()) {
		json["error"] = result.error;
	}

	return json;
}

// Helper functions
QString VoiceTranscription::prepareAudioFile(const QString &inputPath) {
	QFileInfo fileInfo(inputPath);
	if (!fileInfo.exists()) {
		return QString();
	}

	// Whisper accepts wav, mp3, ogg, flac, m4a directly
	static const QStringList directFormats = { "wav", "mp3", "ogg", "flac", "m4a", "webm" };
	QString suffix = fileInfo.suffix().toLower();
	if (directFormats.contains(suffix)) {
		return inputPath;
	}

	// For other formats (e.g., Telegram's .oga opus files), convert to WAV via FFmpeg
	QString outputPath = fileInfo.absolutePath() + "/" + fileInfo.completeBaseName() + "_converted.wav";

	QProcess ffmpeg;
	QStringList args;
	args << "-i" << inputPath
	     << "-ar" << "16000"   // 16kHz sample rate for Whisper
	     << "-ac" << "1"       // mono
	     << "-y"               // overwrite
	     << outputPath;

	ffmpeg.start("ffmpeg", args);
	ffmpeg.waitForFinished(30000); // 30s timeout

	if (ffmpeg.exitStatus() == QProcess::NormalExit && ffmpeg.exitCode() == 0) {
		return outputPath;
	}

	// FFmpeg not available or failed — try with original file anyway
	qWarning() << "[VoiceTranscription] FFmpeg conversion failed, using original:" << inputPath;
	return inputPath;
}

QString VoiceTranscription::getModelName(WhisperModelSize size) const {
	switch (size) {
	case WhisperModelSize::Tiny: return "tiny";
	case WhisperModelSize::Base: return "base";
	case WhisperModelSize::Small: return "small";
	case WhisperModelSize::Medium: return "medium";
	case WhisperModelSize::Large: return "large";
	default: return "base";
	}
}

float VoiceTranscription::estimateConfidence(const QString &text) const {
	if (text.isEmpty()) return 0.0f;

	float confidence = 0.5f;

	// Length factor: longer transcriptions are generally more reliable
	int len = text.length();
	if (len >= 50) confidence += 0.15f;
	else if (len >= 20) confidence += 0.1f;

	// Word count factor: multiple words suggest coherent speech
	int wordCount = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
	if (wordCount >= 5) confidence += 0.1f;
	if (wordCount >= 15) confidence += 0.05f;

	// Punctuation and capitalization suggest structured output
	if (text.contains('.') || text.contains(',') || text.contains('!') || text.contains('?')) {
		confidence += 0.05f;
	}

	// Penalize very short single-word results (likely noise)
	if (wordCount <= 1 && len < 5) {
		confidence -= 0.2f;
	}

	// Penalize if the text is all-uppercase (might be noise or [BLANK_AUDIO])
	if (text == text.toUpper() && len > 3) {
		confidence -= 0.15f;
	}

	return qBound(0.0f, confidence, 1.0f);
}

// Whisper.cpp helper
QString VoiceTranscription::executeWhisperCpp(const QString &audioPath, const QString &modelPath) {
	QProcess process;

	QStringList args;
	args << "-m" << modelPath;
	args << "-f" << audioPath;
	args << "--output-txt";

	if (!_language.isEmpty()) {
		args << "-l" << _language;
	}

	process.start("whisper", args);
	process.waitForFinished(60000);  // 60 second timeout

	if (process.exitStatus() != QProcess::NormalExit) {
		return QString();
	}

	return QString::fromUtf8(process.readAllStandardOutput());
}

// Python subprocess helper
QString VoiceTranscription::executePythonWhisper(const QString &audioPath) {
	QProcess process;

	QString modelName = getModelName(_modelSize);

	QStringList args;
	args << "-c";
	args << QString(R"(
import json
from faster_whisper import WhisperModel

model = WhisperModel("%1", device="cpu")
segments, info = model.transcribe("%2", language="%3" if "%3" else None)

text = " ".join([segment.text for segment in segments])

result = {
    "text": text,
    "language": info.language,
    "confidence": info.language_probability,
    "duration": info.duration,
    "success": True
}

print(json.dumps(result))
	)").arg(modelName, audioPath, _language);

	process.start("python3", args);
	process.waitForFinished(60000);  // 60 second timeout

	if (process.exitStatus() != QProcess::NormalExit) {
		return QString();
	}

	return QString::fromUtf8(process.readAllStandardOutput());
}

} // namespace MCP
