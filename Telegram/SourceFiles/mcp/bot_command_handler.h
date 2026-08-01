// Bot Command Handler
// This file is part of Telegram Desktop MCP integration.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace MCP {

class BotManager;

// Handles bot management commands sent in chats
class BotCommandHandler : public base::has_weak_ptr {
public:
	explicit BotCommandHandler(not_null<Main::Session*> session);
	~BotCommandHandler();

	// Process a potential bot command
	// Returns true if the command was handled
	[[nodiscard]] bool processCommand(const QString &text);

private:
	void handleListCommand();
	void handleEnableCommand(const QString &botId);
	void handleDisableCommand(const QString &botId);
	void handleStatsCommand();
	void handleHelpCommand();

	void sendResponse(const QString &text);

	not_null<Main::Session*> _session;
	BotManager *_botManager = nullptr;

	// Command patterns
	static const QString kCommandPrefix;
	static const QStringList kValidCommands;
};

} // namespace MCP
