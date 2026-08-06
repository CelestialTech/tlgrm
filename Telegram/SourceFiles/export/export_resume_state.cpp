/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_resume_state.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <cstdio>
#include <limits>
#include <unistd.h>

namespace Export {
namespace {

// Read through QVariant so both a JSON number and a JSON string parse. Ids
// are stored as strings because they exceed what a double holds exactly; the
// counters are stored as numbers. A reader that assumed one form got zero for
// the other -- which is how one of the three readers came to never find a
// resume record at all.
[[nodiscard]] int64 ReadInt(const QJsonObject &object, const char *key) {
	return object.value(QString::fromLatin1(key)).toVariant().toLongLong();
}

[[nodiscard]] uint64 ReadUint(const QJsonObject &object, const char *key) {
	return object.value(QString::fromLatin1(key)).toVariant().toULongLong();
}

} // namespace

QString ResumeStateFileName() {
	return u"resume_state.json"_q;
}

QByteArray SerializeResumeState(const ResumeState &state) {
	auto object = QJsonObject();
	// Field types are exactly what the previous writer emitted: the three
	// counters as JSON numbers, the ids as strings. Changing them would
	// silently break any reader still using toInt(), and records already on
	// disk have to keep parsing -- an export interrupted by an upgrade is
	// precisely when a resume record matters most.
	object["last_message_id"] = double(state.lastMessageId);
	object["messages_written"] = double(state.messagesWritten);
	object["messages_total"] = double(state.messagesTotal);
	object["peer_id"] = QString::number(state.peerId);
	object["peer_name"] = state.peerName;
	object["export_dir"] = state.exportDir;
	object["updated_at"] = state.updatedAt;
	if (!state.inputPeerType.isEmpty()) {
		object["input_peer_type"] = state.inputPeerType;
		object["input_peer_id"] = QString::number(state.inputPeerId);
		if (state.inputPeerType != u"chat"_q) {
			object["input_peer_hash"] = QString::number(state.inputPeerHash);
		}
	}
	return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

std::optional<ResumeState> ParseResumeState(const QByteArray &json) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();

	auto result = ResumeState();
	result.lastMessageId = ReadInt(object, "last_message_id");
	result.messagesWritten = ReadInt(object, "messages_written");
	result.messagesTotal = ReadInt(object, "messages_total");
	result.peerId = ReadUint(object, "peer_id");
	result.peerName = object.value(u"peer_name"_q).toString();
	result.exportDir = object.value(u"export_dir"_q).toString();
	result.updatedAt = object.value(u"updated_at"_q).toString();
	result.inputPeerType = object.value(u"input_peer_type"_q).toString();
	result.inputPeerId = ReadUint(object, "input_peer_id");
	result.inputPeerHash = ReadInt(object, "input_peer_hash");
	return result;
}

bool WriteResumeState(const QString &exportDir, const ResumeState &state) {
	if (exportDir.isEmpty()) {
		return false;
	}
	auto dir = QDir(exportDir);
	const auto target = dir.absoluteFilePath(ResumeStateFileName());
	const auto temporary = target + u".tmp"_q;

	{
		auto file = QFile(temporary);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			return false;
		}
		const auto json = SerializeResumeState(state);
		if (file.write(json) != json.size()) {
			file.close();
			QFile::remove(temporary);
			return false;
		}
		// The rename below only publishes what reached the disk. Without
		// this the record can be replaced by a truncated one that parses,
		// resuming from a point the export never actually reached.
		file.flush();
		::fsync(file.handle());
	}

	// rename(2) replaces the target in one step: a reader sees either the
	// previous record or this one. Qt's QFile::rename refuses an existing
	// target, which is why this used to remove first and leave a window with
	// no record at all.
	const auto from = QFile::encodeName(temporary);
	const auto to = QFile::encodeName(target);
	if (::rename(from.constData(), to.constData()) != 0) {
		QFile::remove(temporary);
		return false;
	}
	return true;
}

std::optional<ResumeState> ReadResumeState(const QString &exportDir) {
	if (exportDir.isEmpty()) {
		return std::nullopt;
	}
	auto file = QFile(QDir(exportDir).absoluteFilePath(ResumeStateFileName()));
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	return ParseResumeState(file.readAll());
}

} // namespace Export
