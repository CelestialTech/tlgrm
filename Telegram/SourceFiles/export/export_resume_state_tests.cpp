/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

// Tests for the gradual export's resume record.
//
// The fork's most intricate feature had no tests at all: nothing wrote a
// partial export and restarted it, and nothing checked that what the writer
// emits is what the readers understand. Three separate readers had grown their
// own idea of the format, and one of them read the counters with a call that
// returns zero for the type they are stored in -- so it silently never resumed
// anything, for as long as it had existed.
//
// These run headless: no session, no account, no network. That is deliberate.
// A test that needs a live account is a test nobody runs.

#include "export/export_resume_state.h"

#include "base/basic_types.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

#include <cstdio>
#include <limits>

namespace {

int Failures = 0;
int Checks = 0;

void Check(bool condition, const char *what) {
	++Checks;
	if (!condition) {
		++Failures;
		std::fprintf(stderr, "  FAIL  %s\n", what);
	}
}

template <typename T>
void CheckEqual(const T &actual, const T &expected, const char *what) {
	++Checks;
	if (!(actual == expected)) {
		++Failures;
		std::fprintf(stderr, "  FAIL  %s\n", what);
	}
}

[[nodiscard]] Export::ResumeState SampleState() {
	auto state = Export::ResumeState();
	state.lastMessageId = 123456;
	state.messagesWritten = 4200;
	state.messagesTotal = 9001;
	state.peerId = 0xB1A2C3D4E5F60718ULL;
	state.peerName = u"Test Chat"_q;
	state.exportDir = u"/tmp/export/"_q;
	state.updatedAt = u"2026-08-05T12:00:00Z"_q;
	state.inputPeerType = u"channel"_q;
	state.inputPeerId = 1234567890ULL;
	state.inputPeerHash = -8070450532247928832LL;
	return state;
}

// Serialize -> Parse must be the identity. Every field a resume depends on
// travels through this, so a field that fails to round-trip is a field the
// resumed export gets wrong.
void RoundTripPreservesEveryField() {
	const auto original = SampleState();
	const auto parsed = Export::ParseResumeState(
		Export::SerializeResumeState(original));
	Check(parsed.has_value(), "round trip parses");
	if (!parsed) {
		return;
	}
	CheckEqual(parsed->lastMessageId, original.lastMessageId, "lastMessageId");
	CheckEqual(parsed->messagesWritten, original.messagesWritten, "messagesWritten");
	CheckEqual(parsed->messagesTotal, original.messagesTotal, "messagesTotal");
	CheckEqual(parsed->peerId, original.peerId, "peerId survives 64 bits");
	CheckEqual(parsed->peerName, original.peerName, "peerName");
	CheckEqual(parsed->exportDir, original.exportDir, "exportDir");
	CheckEqual(parsed->updatedAt, original.updatedAt, "updatedAt");
	CheckEqual(parsed->inputPeerType, original.inputPeerType, "inputPeerType");
	CheckEqual(parsed->inputPeerId, original.inputPeerId, "inputPeerId");
	CheckEqual(parsed->inputPeerHash, original.inputPeerHash, "inputPeerHash negative access_hash");
	Check(*parsed == original, "whole record compares equal");
}

// A peer id is a full 64-bit value. Storing it as a JSON number would round it
// through a double and lose the low bits, which is why it is a string -- this
// pins that, because the failure is silent and resumes the wrong chat.
void LargeIdsDoNotLosePrecision() {
	auto state = Export::ResumeState();
	state.lastMessageId = 1;
	state.peerId = 0xFFFFFFFFFFFFFFF1ULL;
	state.inputPeerType = u"user"_q;
	state.inputPeerId = 0x7FFFFFFFFFFFFFFFULL;
	state.inputPeerHash = std::numeric_limits<int64>::min() + 1;
	const auto parsed = Export::ParseResumeState(
		Export::SerializeResumeState(state));
	Check(parsed.has_value(), "large ids parse");
	if (!parsed) {
		return;
	}
	CheckEqual(parsed->peerId, state.peerId, "peerId keeps all 64 bits");
	CheckEqual(parsed->inputPeerId, state.inputPeerId, "inputPeerId keeps all bits");
	CheckEqual(parsed->inputPeerHash, state.inputPeerHash, "inputPeerHash keeps sign");
}

// Records written before the shared parser stored the counters as JSON
// numbers. They must keep parsing: an export interrupted by an upgrade is
// exactly when the record matters.
void ParsesRecordsFromTheOlderWriter() {
	const auto legacy = QByteArray(R"({
		"last_message_id": 555,
		"messages_written": 100,
		"messages_total": 200,
		"peer_id": "9876543210",
		"peer_name": "Old Chat",
		"export_dir": "/tmp/old/",
		"updated_at": "2026-01-01T00:00:00Z"
	})");
	const auto parsed = Export::ParseResumeState(legacy);
	Check(parsed.has_value(), "legacy record parses");
	if (!parsed) {
		return;
	}
	CheckEqual(parsed->lastMessageId, int64(555), "legacy numeric last_message_id");
	CheckEqual(parsed->messagesWritten, int64(100), "legacy numeric messages_written");
	CheckEqual(parsed->messagesTotal, int64(200), "legacy numeric messages_total");
	CheckEqual(parsed->peerId, uint64(9876543210ULL), "legacy string peer_id");
	Check(parsed->resumable(), "legacy record is resumable");
}

// The counters must also parse when stored as strings, because that is what a
// reader assuming one form got wrong. Accepting both is what makes the format
// survive its own history.
void ParsesCountersStoredAsStrings() {
	const auto asStrings = QByteArray(
		R"({"last_message_id":"777","messages_written":"7","messages_total":"70"})");
	const auto parsed = Export::ParseResumeState(asStrings);
	Check(parsed.has_value(), "string counters parse");
	if (!parsed) {
		return;
	}
	CheckEqual(parsed->lastMessageId, int64(777), "string last_message_id");
	CheckEqual(parsed->messagesWritten, int64(7), "string messages_written");
}

// Garbage must be rejected rather than parsed into a record that resumes from
// somewhere arbitrary.
void RejectsInputThatIsNotARecord() {
	Check(!Export::ParseResumeState(QByteArray("not json")).has_value(),
		"rejects non-JSON");
	Check(!Export::ParseResumeState(QByteArray("[1,2,3]")).has_value(),
		"rejects a JSON array");
	Check(!Export::ParseResumeState(QByteArray("")).has_value(),
		"rejects empty input");
	// Truncated mid-write: this is what the old non-atomic writer could leave.
	Check(!Export::ParseResumeState(
		QByteArray(R"({"last_message_id": 12)")).has_value(),
		"rejects a truncated record");
}

// An export that got nowhere is information, not a parse failure. Callers must
// be able to tell "no record" from "a record saying nothing was written".
void DistinguishesUnusableFromAbsent() {
	const auto parsed = Export::ParseResumeState(
		QByteArray(R"({"last_message_id": 0, "messages_written": 0})"));
	Check(parsed.has_value(), "a zero record still parses");
	Check(parsed && !parsed->resumable(), "a zero record is not resumable");
}

void WriteThenReadFromDisk() {
	auto dir = QTemporaryDir();
	Check(dir.isValid(), "temp dir created");
	if (!dir.isValid()) {
		return;
	}
	const auto original = SampleState();
	Check(Export::WriteResumeState(dir.path(), original), "write succeeds");
	const auto read = Export::ReadResumeState(dir.path());
	Check(read.has_value(), "read finds the record");
	Check(read && *read == original, "disk round trip is the identity");
	Check(!Export::ReadResumeState(dir.path() + u"/missing"_q).has_value(),
		"absent directory reads as no record");
}

// The record is rewritten after every slice. A reader must never see a partial
// or missing file, which the previous remove-then-rename could produce.
void OverwriteNeverLeavesTheRecordMissing() {
	auto dir = QTemporaryDir();
	if (!dir.isValid()) {
		return;
	}
	auto state = SampleState();
	Export::WriteResumeState(dir.path(), state);

	for (auto i = 0; i != 50; ++i) {
		state.lastMessageId = 1000 + i;
		state.messagesWritten = i;
		Check(Export::WriteResumeState(dir.path(), state), "rewrite succeeds");
		// After every rewrite a complete, current record must be readable.
		const auto read = Export::ReadResumeState(dir.path());
		if (!read.has_value() || read->lastMessageId != state.lastMessageId) {
			Check(false, "record readable and current after each rewrite");
			return;
		}
	}
	Check(true, "record stayed readable across 50 rewrites");

	// And no temporary is left behind to be mistaken for a record later.
	Check(!QFile::exists(QDir(dir.path()).absoluteFilePath(
			Export::ResumeStateFileName() + u".tmp"_q)),
		"no leftover .tmp file");
}

// A record left by an unrelated export must not be read as this one's.
void WriterAndReaderAgreeOnTheFileName() {
	auto dir = QTemporaryDir();
	if (!dir.isValid()) {
		return;
	}
	Export::WriteResumeState(dir.path(), SampleState());
	Check(QFile::exists(QDir(dir.path()).absoluteFilePath(
			Export::ResumeStateFileName())),
		"written under the shared file name");
	CheckEqual(Export::ResumeStateFileName(), u"resume_state.json"_q,
		"file name is the one the readers look for");
}

} // namespace

int main(int argc, char *argv[]) {
	auto app = QCoreApplication(argc, argv);

	RoundTripPreservesEveryField();
	LargeIdsDoNotLosePrecision();
	ParsesRecordsFromTheOlderWriter();
	ParsesCountersStoredAsStrings();
	RejectsInputThatIsNotARecord();
	DistinguishesUnusableFromAbsent();
	WriteThenReadFromDisk();
	OverwriteNeverLeavesTheRecordMissing();
	WriterAndReaderAgreeOnTheFileName();

	std::fprintf(stderr, "\n%d checks, %d failed\n", Checks, Failures);
	return Failures ? 1 : 0;
}
