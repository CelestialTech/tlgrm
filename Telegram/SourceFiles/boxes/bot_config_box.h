// Bot Configuration Dialog
// This file is part of Telegram Desktop MCP integration.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include "boxes/abstract_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
// Note: checkbox, input_fields, slider headers may not exist
// Using base widget includes instead

namespace MCP {
class BotManager;
class BotBase;
} // namespace MCP

namespace Window {
class SessionController;
} // namespace Window

class BotConfigBox : public Ui::BoxContent {
public:
	BotConfigBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		const QString &botId,
		MCP::BotManager *botManager);

protected:
	void prepare() override;

	void setInnerFocus() override;

private:
	void setupGeneralSettings();
	void setupContextSettings();
	void setupMutedChats();
	void setupPermissions();

	void loadConfig();
	void saveConfig();
	void resetToDefaults();

	not_null<Window::SessionController*> _controller;
	QString _botId;
	MCP::BotManager *_botManager;
	MCP::BotBase *_bot = nullptr;

	// General settings
	// TODO: Ui::Checkbox type doesn't exist in this tdesktop version
	// Need to use appropriate widget types when implementing UI
	// Ui::Checkbox *_enableBot = nullptr;
	// Ui::Checkbox *_enableProactiveHelp = nullptr;
	// Ui::Checkbox *_enableCrossChat = nullptr;

	// Context settings
	// TODO: Ui::MediaSlider type doesn't exist in this tdesktop version
	// Ui::MediaSlider *_maxMessagesSlider = nullptr;
	Ui::FlatLabel *_maxMessagesLabel = nullptr;

	// Ui::MediaSlider *_timeoutSlider = nullptr;
	Ui::FlatLabel *_timeoutLabel = nullptr;

	// Ui::MediaSlider *_confidenceSlider = nullptr;
	Ui::FlatLabel *_confidenceLabel = nullptr;

	// Config storage
	QJsonObject _currentConfig;
};
