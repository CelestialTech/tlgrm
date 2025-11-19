// Bot Framework Settings Panel
// This file is part of Telegram Desktop MCP integration.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include "settings/settings_common_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"

namespace MCP {
class BotManager;
class BotBase;
} // namespace MCP

namespace Settings {

class Bots : public Section<Bots> {
public:
	Bots(QWidget *parent, not_null<Window::SessionController*> controller);
	~Bots();

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
	void setupBotList(not_null<Ui::VerticalLayout*> container);
	void setupGlobalActions(not_null<Ui::VerticalLayout*> container);

	void addBotWidget(
		not_null<Ui::VerticalLayout*> container,
		MCP::BotBase *bot);

	void showBotConfig(const QString &botId);
	void showBotStats();
	void installBot();

	void refreshBotList();

	not_null<Window::SessionController*> _controller;
	MCP::BotManager *_botManager = nullptr;

	// UI elements
	Ui::VerticalLayout *_botListContainer = nullptr;
};

} // namespace Settings
