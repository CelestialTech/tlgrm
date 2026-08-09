// Download and auto-download tools for the Telegram Desktop MCP integration.
//
// Two related surfaces the UI keeps in separate places: the downloads manager
// (what this client has fetched, and what it is fetching now) and the
// auto-download settings that decide what gets fetched without being asked.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

#include "data/data_download_manager.h"
#include "data/data_auto_download.h"
#include "main/main_session_settings.h"

namespace MCP {
namespace {

using namespace ::Data::AutoDownload;

// The names a caller uses for a source and a type. These are the wire format
// of the two auto-download tools, so they are defined once and both the
// reader and the writer go through them -- a get followed by a set has to
// round-trip, and it cannot if each side spells the enum its own way.
constexpr auto kSourceNames = std::array{
	std::pair{ Source::User, "private_chats" },
	std::pair{ Source::Group, "groups" },
	std::pair{ Source::Channel, "channels" },
};

constexpr auto kTypeNames = std::array{
	std::pair{ Type::Photo, "photo" },
	std::pair{ Type::AutoPlayVideo, "video" },
	std::pair{ Type::VoiceMessage, "voice_message" },
	std::pair{ Type::AutoPlayVideoMessage, "video_message" },
	std::pair{ Type::Music, "music" },
	std::pair{ Type::AutoPlayGIF, "gif" },
	std::pair{ Type::File, "file" },
};

[[nodiscard]] std::optional<Source> SourceByName(const QString &name) {
	for (const auto &[source, key] : kSourceNames) {
		if (name == QLatin1String(key)) {
			return source;
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Type> TypeByName(const QString &name) {
	for (const auto &[type, key] : kTypeNames) {
		if (name == QLatin1String(key)) {
			return type;
		}
	}
	return std::nullopt;
}

[[nodiscard]] QStringList SourceNames() {
	auto result = QStringList();
	for (const auto &[source, key] : kSourceNames) {
		result.append(QLatin1String(key));
	}
	return result;
}

[[nodiscard]] QStringList TypeNames() {
	auto result = QStringList();
	for (const auto &[type, key] : kTypeNames) {
		result.append(QLatin1String(key));
	}
	return result;
}

// A download entry names the message it came from. Reporting only a file path
// leaves the caller unable to go back to the message, so the origin travels
// with every row.
[[nodiscard]] QJsonObject DescribeObject(const ::Data::DownloadObject &object) {
	auto value = QJsonObject();
	const auto item = object.item;
	value["message_id"] = qint64(item->id.bare);
	value["chat_id"] = qint64(item->history()->peer->id.value);
	value["chat_title"] = item->history()->peer->name();
	value["date"] = qint64(item->date());
	if (const auto document = object.document) {
		value["kind"] = document->isVideoFile()
			? "video"
			: document->isVoiceMessage()
			? "voice_message"
			: document->isVideoMessage()
			? "video_message"
			: document->isSong()
			? "music"
			: document->isAnimation()
			? "gif"
			: "file";
		value["size"] = qint64(document->size);
		const auto name = document->filename();
		if (!name.isEmpty()) {
			value["filename"] = name;
		}
	} else if (object.photo) {
		value["kind"] = "photo";
	}
	return value;
}

} // namespace

QJsonObject Server::toolListDownloads(const QJsonObject &args) {
	auto &manager = Core::App().downloadManager();

	// The manager tracks every logged-in account at once. A tool answering
	// for "the session" must not report another account's files, so entries
	// are filtered by the session this server is attached to.
	const auto mine = [&](const ::Data::DownloadObject &object) {
		return (&object.item->history()->session() == _session);
	};

	auto loading = QJsonArray();
	for (const auto id : manager.loadingList()) {
		if (!mine(id->object)) {
			continue;
		}
		auto entry = DescribeObject(id->object);
		entry["state"] = id->done ? "finishing" : "loading";
		entry["ready"] = qint64(id->ready);
		entry["total"] = qint64(id->total);
		entry["started"] = qint64(id->started);
		if (!id->path.isEmpty()) {
			entry["path"] = id->path;
		}
		loading.append(entry);
	}

	auto loaded = QJsonArray();
	for (const auto id : manager.loadedList()) {
		if (!id->object || !mine(*id->object)) {
			continue;
		}
		auto entry = DescribeObject(*id->object);
		entry["state"] = "loaded";
		entry["path"] = id->path;
		entry["size"] = qint64(id->size);
		entry["started"] = qint64(id->started);
		entry["exists"] = QFile::exists(id->path);
		loaded.append(entry);
	}

	const auto progress = manager.loadingProgress();
	auto value = QJsonObject();
	value["success"] = true;
	value["loading_count"] = loading.size();
	value["loaded_count"] = loaded.size();
	value["loading"] = loading;
	value["loaded"] = loaded;
	value["progress_ready"] = qint64(progress.ready);
	value["progress_total"] = qint64(progress.total);
	return value;
}

QJsonObject Server::toolClearFinishedDownloads(const QJsonObject &args) {
	auto &manager = Core::App().downloadManager();

	// Counting before and after is the only way to say what happened:
	// clearIfFinished() reports nothing, and a caller told "success" without
	// a number cannot tell an empty list from a cleared one.
	const auto count = [&] {
		auto result = 0;
		for ([[maybe_unused]] const auto id : manager.loadedList()) {
			++result;
		}
		return result;
	};
	const auto before = count();
	manager.clearIfFinished();
	const auto after = count();

	auto value = QJsonObject();
	value["success"] = true;
	value["removed_from_list"] = before - after;
	value["remaining"] = after;
	value["files_deleted"] = false;
	return value;
}

QJsonObject Server::toolDeleteDownloadedFiles(const QJsonObject &args) {
	auto &manager = Core::App().downloadManager();

	// deleteAll() removes the files from disk, not just the list entries.
	// It is spelled as a separate tool from clearing the list, and it is not
	// the default, because the two are not recoverable in the same way.
	auto removed = 0;
	for (const auto id : manager.loadedList()) {
		if (id->object
			&& (&id->object->item->history()->session() == _session)) {
			++removed;
		}
	}
	manager.deleteAll();

	auto value = QJsonObject();
	value["success"] = true;
	value["files_deleted"] = true;
	value["deleted_count"] = removed;
	return value;
}

QJsonObject Server::toolGetAutoDownloadSettings(const QJsonObject &args) {
	const auto &data = _session->settings().autoDownload();

	auto sources = QJsonObject();
	for (const auto &[source, sourceKey] : kSourceNames) {
		auto types = QJsonObject();
		for (const auto &[type, typeKey] : kTypeNames) {
			const auto limit = data.bytesLimit(source, type);
			auto entry = QJsonObject();
			// A zero limit is how "off" is stored; saying so explicitly
			// saves every caller from having to know that.
			entry["enabled"] = (limit > 0);
			entry["bytes_limit"] = qint64(limit);
			types[QLatin1String(typeKey)] = entry;
		}
		auto value = QJsonObject();
		value["any_enabled"] = HasEnabledTypes(data, source);
		value["types"] = types;
		sources[QLatin1String(sourceKey)] = value;
	}

	auto value = QJsonObject();
	value["success"] = true;
	value["sources"] = sources;
	value["max_bytes_limit"] = qint64(kMaxBytesLimit);
	return value;
}

QJsonObject Server::toolSetAutoDownloadSettings(const QJsonObject &args) {
	const auto sourceName = args["source"].toString();
	const auto source = SourceByName(sourceName);
	if (!source) {
		return toolError(QString("Unknown source '%1'; expected one of: %2")
			.arg(sourceName, SourceNames().join(", ")));
	}

	auto &data = _session->settings().autoDownload();
	const auto typeName = args.value("type").toString();
	const auto enabled = args["enabled"].toBool();

	// Omitting `type` addresses the whole source at once -- the master
	// toggle the settings UI shows above the per-type rows. Turning a source
	// on restores upstream's defaults rather than inventing limits, so the
	// result matches what the same switch produces in the UI.
	if (typeName.isEmpty()) {
		if (enabled) {
			SetDefaultsForSource(data, *source);
		} else {
			SetDisabledForSource(data, *source);
		}
	} else {
		const auto type = TypeByName(typeName);
		if (!type) {
			return toolError(QString("Unknown type '%1'; expected one of: %2")
				.arg(typeName, TypeNames().join(", ")));
		}
		const auto limit = args.contains("bytes_limit")
			? std::clamp(
				args["bytes_limit"].toVariant().toLongLong(),
				int64(0),
				kMaxBytesLimit)
			: (enabled ? kMaxBytesLimit : int64(0));
		data.setBytesLimit(*source, *type, enabled ? limit : 0);
	}
	_session->saveSettingsDelayed();

	auto value = QJsonObject();
	value["success"] = true;
	value["source"] = sourceName;
	if (!typeName.isEmpty()) {
		value["type"] = typeName;
		value["bytes_limit"] = qint64(
			data.bytesLimit(*source, *TypeByName(typeName)));
	}
	value["enabled"] = enabled;
	value["any_enabled"] = HasEnabledTypes(data, *source);
	return value;
}

} // namespace MCP
