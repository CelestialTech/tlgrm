// Rich message tools for the Telegram Desktop MCP integration.
//
// A rich message carries a multi-block page (headings, media groups, tables)
// instead of plain text with entities. The client can find them in a history
// and write one out as a self-contained HTML folder; both are exposed here,
// because from the outside a rich message is indistinguishable from an
// ordinary one and read_messages says nothing about the page.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

#include "iv/iv_instance.h"
#include "iv/iv_rich_page.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace MCP {
namespace {

// Blocks nest -- a block, and each of its list items, holds blocks of its own
// -- so a flat count of the top level would understate a page badly. The
// recursion stops at a depth no real page reaches, so a malformed page cannot
// spin. Table cells hold text rather than blocks, so they are not descended
// into.
constexpr auto kMaxBlockDepth = 32;

[[nodiscard]] int CountBlocks(
		const std::vector<Iv::RichPage::Block> &blocks,
		int depth = 0) {
	if (depth >= kMaxBlockDepth) {
		return 0;
	}
	auto result = 0;
	for (const auto &block : blocks) {
		++result;
		result += CountBlocks(block.blocks, depth + 1);
		for (const auto &item : block.listItems) {
			result += CountBlocks(item.blocks, depth + 1);
		}
	}
	return result;
}

} // namespace

QJsonObject Server::toolListRichMessages(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto history = resolveHistory(chatId);
	if (!history) {
		return toolError(QString(
			"No chat %1 in this client's data").arg(chatId));
	}
	const auto limit = clampLimit(args.value("limit").toInt(50), 50, 500);

	// Reads what this client has loaded, and says so: a chat whose history
	// has not been scrolled back holds few messages locally, and an empty
	// answer would otherwise read as "this chat has no rich messages".
	auto items = QJsonArray();
	auto scanned = 0;
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			const auto item = view->data();
			++scanned;
			const auto page = item->richPage();
			if (!page) {
				continue;
			}
			auto entry = QJsonObject();
			entry["message_id"] = qint64(item->id.bare);
			entry["date"] = qint64(item->date());
			entry["block_count"] = CountBlocks(page->blocks);
			entry["partial"] = page->part;
			if (!page->url.isEmpty()) {
				entry["url"] = page->url;
			}
			if (const auto from = item->from()) {
				entry["from_id"] = qint64(from->id.value);
				entry["from"] = from->name();
			}
			items.append(entry);
			if (items.size() >= limit) {
				break;
			}
		}
		if (items.size() >= limit) {
			break;
		}
	}

	auto value = QJsonObject();
	value["success"] = true;
	value["chat_id"] = chatId;
	value["count"] = items.size();
	value["messages_scanned"] = scanned;
	value["source"] = "locally loaded history only";
	value["rich_messages"] = items;
	return value;
}

QJsonObject Server::toolSaveRichMessageHtml(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto messageId = args["message_id"].toVariant().toLongLong();
	const auto history = resolveHistory(chatId);
	if (!history) {
		return toolError(QString(
			"No chat %1 in this client's data").arg(chatId));
	}
	const auto itemId = FullMsgId(history->peer->id, MsgId(messageId));
	const auto item = _session->data().message(itemId);
	if (!item) {
		return toolError(QString(
			"Message %1 is not loaded in chat %2").arg(messageId).arg(chatId));
	}
	if (!item->richPage() && !item->fullRichPage()) {
		return toolError(QString(
			"Message %1 is not a rich message, so there is no page to write"
			).arg(messageId));
	}

	const auto path = sanitizePath(
		args.value("path").toString(),
		defaultExportDir());
	if (path.isEmpty()) {
		return toolError("path is not a writable location");
	}
	if (!QDir().mkpath(path)) {
		return toolError(QString("Could not create '%1'").arg(path));
	}

	// The export needs a window controller: it reports progress through the
	// downloads manager and shows a toast when it settles. Without an open
	// window there is nothing to attach that to, so the tool refuses rather
	// than starting work whose outcome nobody would see.
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller || &controller->session() != _session) {
		return toolError("No open window for this account; the HTML export "
			"reports progress through one, so it cannot run headless");
	}

	Core::App().iv().exportRichMessageHtml(controller, itemId, path);

	// Media is fetched and copied in the background. Saying "written" here
	// would be a guess about work that has only just started.
	auto value = QJsonObject();
	value["success"] = true;
	value["started"] = true;
	value["chat_id"] = chatId;
	value["message_id"] = messageId;
	value["path"] = path;
	value["note"] = "Export runs in the background: media is downloaded and "
		"copied next to the HTML. Watch it with list_downloads.";
	return value;
}

} // namespace MCP
