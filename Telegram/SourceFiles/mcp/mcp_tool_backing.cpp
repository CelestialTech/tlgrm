// This file is part of Telegram Desktop MCP integration.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp/mcp_tool_backing.h"

#include <QtCore/QStringList>
#include <QtCore/QSet>
#include <QtCore/QDebug>

#include <algorithm>

namespace MCP {
namespace {

// The table is generated sorted, and both the binary search below and
// VerifyToolBackings() depend on that. Checked rather than assumed, because
// a hand-edited insertion in the wrong place would otherwise degrade lookups
// into silently wrong answers instead of an obvious failure.
[[nodiscard]] bool TableIsSorted() {
	return std::is_sorted(
		kToolBackings.begin(),
		kToolBackings.end(),
		[](const ToolBackingEntry &a, const ToolBackingEntry &b) {
			return a.name < b.name;
		});
}

} // namespace

Backing ToolBackingFor(std::string_view name) {
	const auto it = std::lower_bound(
		kToolBackings.begin(),
		kToolBackings.end(),
		name,
		[](const ToolBackingEntry &entry, std::string_view value) {
			return entry.name < value;
		});
	return (it != kToolBackings.end() && it->name == name)
		? it->backing
		: Backing::Unimplemented;
}

bool ToolBackingKnown(std::string_view name) {
	const auto it = std::lower_bound(
		kToolBackings.begin(),
		kToolBackings.end(),
		name,
		[](const ToolBackingEntry &entry, std::string_view value) {
			return entry.name < value;
		});
	return (it != kToolBackings.end() && it->name == name);
}

std::string_view BackingName(Backing backing) {
	switch (backing) {
	case Backing::Mtproto: return "mtproto";
	case Backing::LiveSession: return "live-session";
	case Backing::PureCompute: return "pure-compute";
	case Backing::LocalOnly: return "local-only";
	case Backing::Unimplemented: return "unimplemented";
	}
	return "unknown";
}

void VerifyToolBackings(const QStringList &registeredToolNames) {
	if (!TableIsSorted()) {
		qWarning() << "MCP: tool backing table is not sorted by name;"
			<< "lookups will return wrong answers. Re-sort it.";
		Assert(false);
	}

	auto duplicates = QStringList();
	for (auto i = std::size_t(1); i < kToolBackings.size(); ++i) {
		if (kToolBackings[i].name == kToolBackings[i - 1].name) {
			duplicates.append(QString::fromUtf8(
				kToolBackings[i].name.data(),
				int(kToolBackings[i].name.size())));
		}
	}

	// Drift in either direction is a defect, and each direction fails in its
	// own way: a table entry with no handler advertises something uncallable,
	// while a handler with no table entry falls through to Unimplemented and
	// stops working the moment enforcement is on.
	auto tableNames = QSet<QString>();
	for (const auto &entry : kToolBackings) {
		tableNames.insert(QString::fromUtf8(
			entry.name.data(),
			int(entry.name.size())));
	}
	const auto registered = QSet<QString>(
		registeredToolNames.begin(),
		registeredToolNames.end());

	const auto missingHandler = QStringList(
		(tableNames - registered).values());
	const auto missingEntry = QStringList(
		(registered - tableNames).values());

	if (!duplicates.isEmpty()) {
		qWarning() << "MCP: duplicate tool backing entries:"
			<< duplicates.join(", ");
	}
	if (!missingHandler.isEmpty()) {
		qWarning() << "MCP: tools in the backing table with no handler:"
			<< missingHandler.join(", ");
	}
	if (!missingEntry.isEmpty()) {
		qWarning() << "MCP: registered tools missing a backing entry"
			<< "(they will be refused as unimplemented):"
			<< missingEntry.join(", ");
	}
	Assert(duplicates.isEmpty()
		&& missingHandler.isEmpty()
		&& missingEntry.isEmpty());

	auto counts = std::array<int, 5>{};
	for (const auto &entry : kToolBackings) {
		++counts[static_cast<int>(entry.backing)];
	}
	qInfo().nospace()
		<< "MCP: " << int(kToolBackings.size()) << " tools — "
		<< counts[int(Backing::Mtproto)] << " mtproto, "
		<< counts[int(Backing::LiveSession)] << " live-session, "
		<< counts[int(Backing::PureCompute)] << " pure-compute, "
		<< counts[int(Backing::LocalOnly)] << " local-only, "
		<< counts[int(Backing::Unimplemented)] << " unimplemented (refused)";
}

} // namespace MCP
