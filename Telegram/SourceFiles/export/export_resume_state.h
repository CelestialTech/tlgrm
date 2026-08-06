/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

namespace Export {

// The on-disk record a gradual export leaves behind so it can be resumed.
//
// The format used to exist only as the body of writeResumeStateJson() and was
// re-derived by three separate readers, each pulling out the fields it happened
// to need. Adding a field meant finding all four places; a reader that missed
// one silently resumed from the wrong point, which for an export means
// re-downloading or skipping messages.
//
// So the format is defined once, here. Serialize() and Parse() are pure and
// each other's inverse, which is what makes them testable without a session,
// an account, or a partially written export on disk.
struct ResumeState {
	// Highest message id already written. Resuming continues below it.
	int64 lastMessageId = 0;

	// How many messages were written, used to seed progress so a resumed
	// export does not restart its counters from zero.
	int64 messagesWritten = 0;
	int64 messagesTotal = 0;

	uint64 peerId = 0;
	QString peerName;
	QString exportDir;
	QString updatedAt;

	// The peer as MTProto addresses it, so a resume can rebuild the request
	// without the peer being loaded. Empty type means the record predates
	// this field or the peer kind was not one of the three below.
	QString inputPeerType;  // "user" | "chat" | "channel"
	uint64 inputPeerId = 0;
	int64 inputPeerHash = 0;

	// A record is only usable if it says where to continue from. Parse()
	// returns records that fail this so a caller can tell "no state" from
	// "state that cannot be resumed", which are different problems.
	[[nodiscard]] bool resumable() const {
		return (lastMessageId > 0);
	}

	friend bool operator==(
		const ResumeState &a,
		const ResumeState &b) = default;
};

// The file name a ResumeState is stored under inside an export directory.
[[nodiscard]] QString ResumeStateFileName();

[[nodiscard]] QByteArray SerializeResumeState(const ResumeState &state);

// Parses what SerializeResumeState() wrote. Returns nullopt only when the
// input is not JSON or not an object -- a well-formed record with a zero
// last_message_id parses successfully and answers false to resumable(),
// because "the export got nowhere" is information, not a parse error.
[[nodiscard]] std::optional<ResumeState> ParseResumeState(
	const QByteArray &json);

// Writes `state` into `exportDir` so that a reader either sees the previous
// record or the new one, never a partial or absent file.
//
// The previous implementation removed the target and then renamed the
// temporary over it. Between those two calls the file does not exist, and a
// crash there loses the resume point entirely -- the one thing the record is
// for. This replaces in a single step.
bool WriteResumeState(const QString &exportDir, const ResumeState &state);

// Reads the record from `exportDir`, or nullopt when there is none.
[[nodiscard]] std::optional<ResumeState> ReadResumeState(
	const QString &exportDir);

} // namespace Export
