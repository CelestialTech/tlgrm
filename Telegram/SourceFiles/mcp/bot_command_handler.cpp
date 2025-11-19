// Bot Command Handler - Implementation
// This file is part of Telegram Desktop MCP integration.
// Licensed under GPLv3 with OpenSSL exception.

#include "bot_command_handler.h"

#include "mcp/bot_manager.h"
#include "mcp/bot_base.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"

namespace MCP {

const QString BotCommandHandler::kCommandPrefix = "/bot";
const QStringList BotCommandHandler::kValidCommands = {
	"list",
	"enable",
	"disable",
	"stats",
	"help"
};

BotCommandHandler::BotCommandHandler(not_null<Main::Session*> session)
: _session(session) {
	// TODO: Get BotManager from session or application
	// _botManager = &_session->botManager();
}

BotCommandHandler::~BotCommandHandler() = default;

bool BotCommandHandler::processCommand(const QString &text) {
	if (!text.startsWith(kCommandPrefix)) {
		return false;
	}

	// Parse command: /bot <action> [args...]
	const auto parts = text.split(' ', Qt::SkipEmptyParts);
	if (parts.size() < 2) {
		handleHelpCommand();
		return true;
	}

	const auto action = parts[1].toLower();

	if (action == "list") {
		handleListCommand();
		return true;
	}

	if (action == "enable" && parts.size() >= 3) {
		handleEnableCommand(parts[2]);
		return true;
	}

	if (action == "disable" && parts.size() >= 3) {
		handleDisableCommand(parts[2]);
		return true;
	}

	if (action == "stats") {
		handleStatsCommand();
		return true;
	}

	if (action == "help") {
		handleHelpCommand();
		return true;
	}

	// Unknown command
	sendResponse(QString("Unknown command: %1\nType /bot help for available commands.").arg(action));
	return true;
}

void BotCommandHandler::handleListCommand() {
	if (!_botManager) {
		sendResponse("Bot Manager not available.");
		return;
	}

	auto allBots = _botManager->getAllBots();
	if (allBots.isEmpty()) {
		sendResponse("No bots registered.");
		return;
	}

	QString response = "🤖 Registered Bots:\n\n";
	for (const auto *bot : allBots) {
		auto info = bot->info();
		const auto status = bot->isEnabled() ? "✅ Enabled" : "❌ Disabled";
		response += QString("%1. %2 - %3\n").arg(info.id, info.name, status);
	}

	sendResponse(response);
}

void BotCommandHandler::handleEnableCommand(const QString &botId) {
	if (!_botManager) {
		sendResponse("Bot Manager not available.");
		return;
	}

	auto *bot = _botManager->getBot(botId);
	if (!bot) {
		sendResponse(QString("Bot not found: %1").arg(botId));
		return;
	}

	if (_botManager->enableBot(botId)) {
		sendResponse(QString("✅ Bot enabled: %1").arg(botId));
	} else {
		sendResponse(QString("Failed to enable bot: %1").arg(botId));
	}
}

void BotCommandHandler::handleDisableCommand(const QString &botId) {
	if (!_botManager) {
		sendResponse("Bot Manager not available.");
		return;
	}

	auto *bot = _botManager->getBot(botId);
	if (!bot) {
		sendResponse(QString("Bot not found: %1").arg(botId));
		return;
	}

	if (_botManager->disableBot(botId)) {
		sendResponse(QString("❌ Bot disabled: %1").arg(botId));
	} else {
		sendResponse(QString("Failed to disable bot: %1").arg(botId));
	}
}

void BotCommandHandler::handleStatsCommand() {
	if (!_botManager) {
		sendResponse("Bot Manager not available.");
		return;
	}

	auto allStats = _botManager->getAllStats();
	if (allStats.isEmpty()) {
		sendResponse("No statistics available.");
		return;
	}

	QString response = "📊 Bot Statistics:\n\n";
	for (const auto &stats : allStats) {
		response += QString("%1:\n").arg(stats.botId);
		response += QString("  Messages: %1\n").arg(stats.messagesProcessed);
		response += QString("  Avg Response Time: %1ms\n").arg(stats.averageResponseTime(), 0, 'f', 1);
		response += QString("  Errors: %1\n\n").arg(stats.errorCount);
	}

	sendResponse(response);
}

void BotCommandHandler::handleHelpCommand() {
	const QString help = R"(🤖 Bot Framework Commands:

/bot list - List all registered bots
/bot enable <bot_id> - Enable a specific bot
/bot disable <bot_id> - Disable a specific bot
/bot stats - Show bot statistics
/bot help - Show this help message

Example:
/bot enable context_assistant
/bot disable analytics_bot

For more advanced configuration, open Settings → Advanced → Bot Framework)";

	sendResponse(help);
}

void BotCommandHandler::sendResponse(const QString &text) {
	// Show response as a toast notification
	Ui::Toast::Show(text);
}

} // namespace MCP
