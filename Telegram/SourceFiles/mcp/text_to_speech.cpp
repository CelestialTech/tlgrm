// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "text_to_speech.h"

#include <QtCore/QProcess>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

TextToSpeech::TextToSpeech(QObject *parent)
	: QObject(parent) {
}

TextToSpeech::~TextToSpeech() {
	stop();
}

bool TextToSpeech::start(QSqlDatabase *db) {
	if (_isRunning) {
		return true;
	}
	_db = db;
	_isRunning = true;
	return true;
}

void TextToSpeech::stop() {
	if (!_isRunning) {
		return;
	}
	_db = nullptr;
	_isRunning = false;
}

// Main synthesis dispatch (mirrors VoiceTranscription::transcribe)
SynthesisResult TextToSpeech::synthesize(
		const QString &text,
		const QString &voiceId,
		double speed,
		double pitch) {

	SynthesisResult result;
	result.generatedAt = QDateTime::currentDateTime();

	if (text.isEmpty()) {
		result.success = false;
		result.error = "Empty text provided";
		return result;
	}

	// Check cache first
	QString cacheKey = computeCacheKey(text, voiceId, speed, pitch);
	if (hasCachedAudio(cacheKey)) {
		result = getCachedAudio(cacheKey);
		if (result.success) {
			_stats.cacheHits++;
			return result;
		}
	}

	// Dispatch to provider
	switch (_provider) {
	case TTSProvider::PiperTTS:
		result = synthesizeWithPiper(text, voiceId, speed);
		result.provider = "Piper TTS";
		break;
	case TTSProvider::EspeakNG:
		result = synthesizeWithEspeak(text, voiceId, speed, pitch);
		result.provider = "espeak-ng";
		break;
	case TTSProvider::CoquiPython:
		result = synthesizeWithCoqui(text, voiceId, speed);
		result.provider = "Coqui TTS (Python)";
		break;
	}

	// Update stats
	if (result.success) {
		_stats.successfulSyntheses++;
		storeCachedAudio(cacheKey, result);
	} else {
		_stats.failedSyntheses++;
	}
	_stats.totalSyntheses++;
	_stats.lastGenerated = QDateTime::currentDateTime();

	if (_stats.successfulSyntheses > 0) {
		_stats.avgDurationSeconds =
			(_stats.avgDurationSeconds * (_stats.successfulSyntheses - 1)
			 + result.durationSeconds) / _stats.successfulSyntheses;
	}

	if (result.success) {
		Q_EMIT synthesisCompleted(result);
	} else {
		Q_EMIT synthesisFailed(result.error);
	}

	return result;
}

// Piper TTS provider
SynthesisResult TextToSpeech::synthesizeWithPiper(
		const QString &text,
		const QString &voiceId,
		double speed) {
	SynthesisResult result;
	result.success = false;

	QString piperBin = _piperBinaryPath.isEmpty()
		? findBinary("piper")
		: _piperBinaryPath;
	if (piperBin.isEmpty()) {
		result.error = "piper binary not found. Install via: "
					   "brew install rhasspy/piper/piper "
					   "(or download from https://github.com/rhasspy/piper/releases)";
		return result;
	}

	QString modelPath = voiceId.isEmpty() ? _piperModelPath : voiceId;
	if (modelPath.isEmpty()) {
		result.error = "No Piper voice model configured. "
					   "Download models from https://github.com/rhasspy/piper/releases "
					   "and set via configure_voice_persona";
		return result;
	}

	QString wavPath = tempFilePath(".wav");
	QString oggPath = tempFilePath(".ogg");

	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);

	// piper reads from stdin, outputs to file
	QStringList args;
	args << "--model" << modelPath;
	args << "--output_file" << wavPath;
	if (speed != 1.0) {
		double lengthScale = 1.0 / speed;
		args << "--length_scale" << QString::number(lengthScale, 'f', 2);
	}

	process.start(piperBin, args);

	if (!process.waitForStarted(10000)) {
		result.error = "Failed to start piper process";
		return result;
	}

	// Write text to stdin
	process.write(text.toUtf8());
	process.closeWriteChannel();

	if (!process.waitForFinished(60000)) {
		process.kill();
		result.error = "piper process timed out";
		return result;
	}

	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		result.error = QString("piper failed (exit code %1): %2")
			.arg(process.exitCode())
			.arg(QString::fromUtf8(process.readAll()));
		return result;
	}

	QFileInfo wavInfo(wavPath);
	if (!wavInfo.exists() || wavInfo.size() == 0) {
		result.error = "piper produced no output";
		return result;
	}

	// Convert WAV -> OGG Opus
	if (!convertToOpus(wavPath, oggPath)) {
		result.error = "FFmpeg conversion to OGG Opus failed";
		QFile::remove(wavPath);
		return result;
	}

	QFile oggFile(oggPath);
	if (!oggFile.open(QIODevice::ReadOnly)) {
		result.error = "Failed to read converted OGG file";
		QFile::remove(wavPath);
		return result;
	}

	result.audioData = oggFile.readAll();
	result.outputPath = oggPath;
	result.durationSeconds = getAudioDuration(oggPath);
	result.sampleRate = 48000;
	result.voiceUsed = modelPath;
	result.success = true;

	oggFile.close();
	QFile::remove(wavPath);

	return result;
}

// espeak-ng provider
SynthesisResult TextToSpeech::synthesizeWithEspeak(
		const QString &text,
		const QString &voiceId,
		double speed,
		double pitch) {
	SynthesisResult result;
	result.success = false;

	QString espeakBin = findBinary("espeak-ng");
	if (espeakBin.isEmpty()) {
		result.error = "espeak-ng not found. Install via: brew install espeak-ng";
		return result;
	}

	QString wavPath = tempFilePath(".wav");
	QString oggPath = tempFilePath(".ogg");

	QStringList args;
	args << "-v" << (voiceId.isEmpty() ? _language : voiceId);
	if (speed != 1.0) {
		int wpm = static_cast<int>(175.0 * speed);
		args << "-s" << QString::number(wpm);
	}
	if (pitch != 1.0) {
		int pitchVal = static_cast<int>(50.0 * pitch);
		pitchVal = qBound(0, pitchVal, 99);
		args << "-p" << QString::number(pitchVal);
	}
	args << "-w" << wavPath;
	args << text;

	QProcess process;
	process.start(espeakBin, args);
	process.waitForFinished(30000);

	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		result.error = QString("espeak-ng failed: %1")
			.arg(QString::fromUtf8(process.readAllStandardError()));
		return result;
	}

	if (!convertToOpus(wavPath, oggPath)) {
		result.error = "FFmpeg conversion to OGG Opus failed";
		QFile::remove(wavPath);
		return result;
	}

	QFile oggFile(oggPath);
	if (!oggFile.open(QIODevice::ReadOnly)) {
		result.error = "Failed to read OGG file";
		QFile::remove(wavPath);
		return result;
	}

	result.audioData = oggFile.readAll();
	result.outputPath = oggPath;
	result.durationSeconds = getAudioDuration(oggPath);
	result.sampleRate = 48000;
	result.voiceUsed = voiceId.isEmpty() ? _language : voiceId;
	result.success = true;

	oggFile.close();
	QFile::remove(wavPath);
	return result;
}

// Coqui TTS provider (supports voice cloning via XTTS-v2)
SynthesisResult TextToSpeech::synthesizeWithCoqui(
		const QString &text,
		const QString &voiceId,
		double speed) {
	SynthesisResult result;
	result.success = false;

	QString wavPath = tempFilePath(".wav");
	QString oggPath = tempFilePath(".ogg");

	QString modelName = "tts_models/en/ljspeech/tacotron2-DDC";
	bool isCloneMode = false;

	// If voiceId is a path to a .wav file, use XTTS-v2 cloning mode
	if (!voiceId.isEmpty() && QFile::exists(voiceId)) {
		modelName = "tts_models/multilingual/multi-dataset/xtts_v2";
		isCloneMode = true;
	} else if (!voiceId.isEmpty()) {
		modelName = voiceId;
	}

	// Write text to a temp file to avoid shell escaping issues
	QString textFilePath = tempFilePath(".txt");
	QFile textFile(textFilePath);
	if (!textFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		result.error = "Failed to create temp text file";
		return result;
	}
	textFile.write(text.toUtf8());
	textFile.close();

	QString pythonScript;
	if (isCloneMode) {
		pythonScript = QString(
			"from TTS.api import TTS; "
			"tts = TTS('%1'); "
			"text = open('%2').read(); "
			"tts.tts_to_file(text=text, speaker_wav='%3', language='%4', "
			"file_path='%5', speed=%6)"
		).arg(modelName,
			  textFilePath,
			  voiceId,
			  _language.isEmpty() ? "en" : _language,
			  wavPath,
			  QString::number(speed, 'f', 2));
	} else {
		pythonScript = QString(
			"from TTS.api import TTS; "
			"tts = TTS('%1'); "
			"text = open('%2').read(); "
			"tts.tts_to_file(text=text, file_path='%3', speed=%4)"
		).arg(modelName,
			  textFilePath,
			  wavPath,
			  QString::number(speed, 'f', 2));
	}

	QStringList args;
	args << "-c" << pythonScript;

	QProcess process;
	process.start("python3", args);
	process.waitForFinished(120000);  // 2 min timeout for XTTS

	// Clean up temp text file
	QFile::remove(textFilePath);

	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		result.error = QString("Coqui TTS failed: %1")
			.arg(QString::fromUtf8(process.readAllStandardError()));
		return result;
	}

	QFileInfo wavInfo(wavPath);
	if (!wavInfo.exists() || wavInfo.size() == 0) {
		result.error = "Coqui TTS produced no output";
		return result;
	}

	if (!convertToOpus(wavPath, oggPath)) {
		result.error = "FFmpeg conversion to OGG Opus failed";
		QFile::remove(wavPath);
		return result;
	}

	QFile oggFile(oggPath);
	if (!oggFile.open(QIODevice::ReadOnly)) {
		result.error = "Failed to read OGG file";
		QFile::remove(wavPath);
		return result;
	}

	result.audioData = oggFile.readAll();
	result.outputPath = oggPath;
	result.durationSeconds = getAudioDuration(oggPath);
	result.sampleRate = 48000;
	result.voiceUsed = modelName;
	result.success = true;

	oggFile.close();
	QFile::remove(wavPath);
	return result;
}

// Voice cloning convenience method
SynthesisResult TextToSpeech::cloneAndSynthesize(
		const QString &text,
		const QString &speakerWavPath,
		double speed) {
	TTSProvider savedProvider = _provider;
	_provider = TTSProvider::CoquiPython;
	SynthesisResult result = synthesize(text, speakerWavPath, speed, 1.0);
	_provider = savedProvider;
	return result;
}

// WAV -> OGG Opus conversion via FFmpeg
bool TextToSpeech::convertToOpus(
		const QString &inputWavPath,
		const QString &outputOggPath) {
	QProcess ffmpeg;
	QStringList args;
	args << "-i" << inputWavPath
		 << "-c:a" << "libopus"
		 << "-b:a" << "64k"
		 << "-ar" << "48000"
		 << "-ac" << "1"
		 << "-vbr" << "on"
		 << "-compression_level" << "10"
		 << "-application" << "voip"
		 << "-y"
		 << outputOggPath;

	ffmpeg.start("ffmpeg", args);
	ffmpeg.waitForFinished(30000);

	if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
		qWarning() << "[TextToSpeech] FFmpeg conversion failed:"
					<< ffmpeg.readAllStandardError();
		return false;
	}

	return QFileInfo::exists(outputOggPath);
}

// Get audio duration via ffprobe
float TextToSpeech::getAudioDuration(const QString &filePath) {
	QProcess ffprobe;
	QStringList args;
	args << "-v" << "error"
		 << "-show_entries" << "format=duration"
		 << "-of" << "default=noprint_wrappers=1:nokey=1"
		 << filePath;

	ffprobe.start("ffprobe", args);
	ffprobe.waitForFinished(5000);

	if (ffprobe.exitStatus() == QProcess::NormalExit) {
		QString output = QString::fromUtf8(ffprobe.readAllStandardOutput()).trimmed();
		bool ok = false;
		float duration = output.toFloat(&ok);
		if (ok) {
			return duration;
		}
	}

	// Fallback: estimate from file size (~8KB/s for 64kbps Opus)
	QFileInfo info(filePath);
	return static_cast<float>(info.size()) / 8000.0f;
}

// Find a binary on the system
QString TextToSpeech::findBinary(const QString &name) const {
	QStringList searchPaths = {
		"/opt/homebrew/bin/" + name,
		"/usr/local/bin/" + name,
		"/usr/bin/" + name,
		QDir::homePath() + "/.local/bin/" + name,
	};

	for (const auto &path : searchPaths) {
		if (QFileInfo::exists(path)) {
			return path;
		}
	}

	// Try PATH via which
	QProcess which;
	which.start("which", QStringList{name});
	which.waitForFinished(3000);
	if (which.exitStatus() == QProcess::NormalExit && which.exitCode() == 0) {
		return QString::fromUtf8(which.readAllStandardOutput()).trimmed();
	}

	return QString();
}

// Generate unique temp file path
QString TextToSpeech::tempFilePath(const QString &suffix) const {
	return QDir::tempPath() + "/tg_tts_"
		   + QString::number(QDateTime::currentMSecsSinceEpoch())
		   + "_" + QString::number(QRandomGenerator::global()->bounded(10000))
		   + suffix;
}

// Compute cache key from synthesis parameters
QString TextToSpeech::computeCacheKey(
		const QString &text,
		const QString &voiceId,
		double speed,
		double pitch) {
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(text.toUtf8());
	hash.addData(voiceId.toUtf8());
	hash.addData(QByteArray::number(speed, 'f', 2));
	hash.addData(QByteArray::number(pitch, 'f', 2));
	return hash.result().toHex();
}

// Cache: check if audio exists
bool TextToSpeech::hasCachedAudio(const QString &cacheKey) {
	if (!_db || !_db->isOpen()) {
		return false;
	}
	QSqlQuery query(*_db);
	query.prepare("SELECT COUNT(*) FROM tts_cache WHERE cache_key = ?");
	query.addBindValue(cacheKey);
	if (query.exec() && query.next()) {
		return query.value(0).toInt() > 0;
	}
	return false;
}

// Cache: retrieve audio
SynthesisResult TextToSpeech::getCachedAudio(const QString &cacheKey) {
	SynthesisResult result;
	result.success = false;

	if (!_db || !_db->isOpen()) {
		return result;
	}

	QSqlQuery query(*_db);
	query.prepare("SELECT audio_data, duration_seconds, provider, voice_used, "
				  "output_path, created_at FROM tts_cache WHERE cache_key = ?");
	query.addBindValue(cacheKey);

	if (query.exec() && query.next()) {
		result.audioData = query.value(0).toByteArray();
		result.durationSeconds = query.value(1).toFloat();
		result.provider = query.value(2).toString();
		result.voiceUsed = query.value(3).toString();
		result.outputPath = query.value(4).toString();
		result.generatedAt = QDateTime::fromSecsSinceEpoch(
			query.value(5).toLongLong());
		result.sampleRate = 48000;
		result.success = !result.audioData.isEmpty();
	}

	return result;
}

// Cache: store audio
bool TextToSpeech::storeCachedAudio(
		const QString &cacheKey,
		const SynthesisResult &result) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare("INSERT OR REPLACE INTO tts_cache "
				  "(cache_key, audio_data, duration_seconds, provider, "
				  "voice_used, output_path, created_at) "
				  "VALUES (?, ?, ?, ?, ?, ?, ?)");
	query.addBindValue(cacheKey);
	query.addBindValue(result.audioData);
	query.addBindValue(result.durationSeconds);
	query.addBindValue(result.provider);
	query.addBindValue(result.voiceUsed);
	query.addBindValue(result.outputPath);
	query.addBindValue(result.generatedAt.toSecsSinceEpoch());

	return query.exec();
}

TextToSpeech::TTSStats TextToSpeech::getStats() const {
	return _stats;
}

} // namespace MCP
