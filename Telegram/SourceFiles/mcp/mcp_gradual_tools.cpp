// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ============================================================
// GRADUAL EXPORT TOOLS IMPLEMENTATION
// ============================================================

QJsonObject Server::toolStartGradualExport(const QJsonObject &args) {
	if (!_gradualArchiver) {
		_gradualArchiver = std::make_unique<GradualArchiver>(this);
		if (_session) {
			_gradualArchiver->setMainSession(_session);
			_gradualArchiver->setDataSession(&_session->data());
		}
		if (_archiver) {
			_gradualArchiver->setArchiver(_archiver.get());
		}
	}

	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	if (chatId == 0) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "chat_id is required";
		return result;
	}

	GradualArchiveConfig config;

	// Apply optional parameters from args
	if (args.contains("min_delay_ms")) {
		config.minDelayMs = args.value("min_delay_ms").toInt();
	}
	if (args.contains("max_delay_ms")) {
		config.maxDelayMs = args.value("max_delay_ms").toInt();
	}
	if (args.contains("min_batch_size")) {
		config.minBatchSize = args.value("min_batch_size").toInt();
	}
	if (args.contains("max_batch_size")) {
		config.maxBatchSize = args.value("max_batch_size").toInt();
	}
	if (args.contains("export_format")) {
		config.exportFormat = args.value("export_format").toString();
	}
	if (args.contains("export_path")) {
		config.exportPath = args.value("export_path").toString();
	}

	bool started = _gradualArchiver->startGradualArchive(chatId, config);

	QJsonObject result;
	result["success"] = started;
	if (started) {
		result["message"] = "Gradual export started";
		result["chat_id"] = QString::number(chatId);
	} else {
		result["error"] = "Failed to start gradual export - another export may be in progress";
	}
	return result;
}

QJsonObject Server::toolGetGradualExportStatus(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		QJsonObject result;
		result["success"] = true;
		result["state"] = "idle";
		result["message"] = "No gradual export in progress";
		return result;
	}

	return _gradualArchiver->statusJson();
}

QJsonObject Server::toolPauseGradualExport(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "No gradual export in progress";
		return result;
	}

	_gradualArchiver->pause();

	QJsonObject result;
	result["success"] = true;
	result["message"] = "Gradual export paused";
	result["status"] = _gradualArchiver->statusJson();
	return result;
}

QJsonObject Server::toolResumeGradualExport(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "No gradual export to resume";
		return result;
	}

	_gradualArchiver->resume();

	QJsonObject result;
	result["success"] = true;
	result["message"] = "Gradual export resumed";
	result["status"] = _gradualArchiver->statusJson();
	return result;
}

QJsonObject Server::toolCancelGradualExport(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "No gradual export to cancel";
		return result;
	}

	_gradualArchiver->cancel();

	QJsonObject result;
	result["success"] = true;
	result["message"] = "Gradual export cancelled";
	return result;
}

QJsonObject Server::toolGetGradualExportConfig(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		// Return default config
		GradualArchiveConfig defaultConfig;
		QJsonObject result;
		result["success"] = true;
		result["config"] = QJsonObject{
			{"min_delay_ms", defaultConfig.minDelayMs},
			{"max_delay_ms", defaultConfig.maxDelayMs},
			{"burst_pause_ms", defaultConfig.burstPauseMs},
			{"long_pause_ms", defaultConfig.longPauseMs},
			{"min_batch_size", defaultConfig.minBatchSize},
			{"max_batch_size", defaultConfig.maxBatchSize},
			{"batches_before_pause", defaultConfig.batchesBeforePause},
			{"batches_before_long_pause", defaultConfig.batchesBeforeLongPause},
			{"randomize_order", defaultConfig.randomizeOrder},
			{"simulate_reading", defaultConfig.simulateReading},
			{"respect_active_hours", defaultConfig.respectActiveHours},
			{"active_hour_start", defaultConfig.activeHourStart},
			{"active_hour_end", defaultConfig.activeHourEnd},
			{"max_messages_per_day", defaultConfig.maxMessagesPerDay},
			{"max_messages_per_hour", defaultConfig.maxMessagesPerHour},
			{"stop_on_flood_wait", defaultConfig.stopOnFloodWait},
			{"export_format", defaultConfig.exportFormat}
		};
		return result;
	}

	return _gradualArchiver->configJson();
}

QJsonObject Server::toolSetGradualExportConfig(const QJsonObject &args) {
	if (!_gradualArchiver) {
		_gradualArchiver = std::make_unique<GradualArchiver>(this);
		if (_session) {
			_gradualArchiver->setMainSession(_session);
			_gradualArchiver->setDataSession(&_session->data());
		}
		if (_archiver) {
			_gradualArchiver->setArchiver(_archiver.get());
		}
	}

	bool success = _gradualArchiver->loadConfigFromJson(args);

	QJsonObject result;
	result["success"] = success;
	if (success) {
		result["message"] = "Configuration updated";
		result["config"] = _gradualArchiver->configJson();
	} else {
		result["error"] = "Failed to apply configuration";
	}
	return result;
}

QJsonObject Server::toolQueueGradualExport(const QJsonObject &args) {
	if (!_gradualArchiver) {
		_gradualArchiver = std::make_unique<GradualArchiver>(this);
		if (_session) {
			_gradualArchiver->setMainSession(_session);
			_gradualArchiver->setDataSession(&_session->data());
		}
		if (_archiver) {
			_gradualArchiver->setArchiver(_archiver.get());
		}
	}

	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	if (chatId == 0) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "chat_id is required";
		return result;
	}

	GradualArchiveConfig config;
	// Use current config as base
	config = _gradualArchiver->config();

	bool queued = _gradualArchiver->queueChat(chatId, config);

	QJsonObject result;
	result["success"] = queued;
	if (queued) {
		result["message"] = "Chat added to export queue";
		result["chat_id"] = QString::number(chatId);
		result["queue"] = _gradualArchiver->getQueue();
	} else {
		result["error"] = "Failed to queue chat";
	}
	return result;
}

QJsonObject Server::toolGetGradualExportQueue(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_gradualArchiver) {
		QJsonObject result;
		result["success"] = true;
		result["queue"] = QJsonArray();
		result["count"] = 0;
		return result;
	}

	QJsonArray queue = _gradualArchiver->getQueue();

	QJsonObject result;
	result["success"] = true;
	result["queue"] = queue;
	result["count"] = queue.size();
	return result;
}


} // namespace MCP
