// Bot Configuration Dialog - Implementation
// This file is part of Telegram Desktop MCP integration.
// Licensed under GPLv3 with OpenSSL exception.

#include "bot_config_box.h"

#include "mcp/bot_manager.h"
#include "mcp/bot_base.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
// Note: checkbox.h and slider.h don't exist in this tdesktop version
// #include "ui/widgets/checkbox.h"
// #include "ui/widgets/slider.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

BotConfigBox::BotConfigBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	const QString &botId,
	MCP::BotManager *botManager)
: _controller(controller)
, _botId(botId)
, _botManager(botManager) {

	if (_botManager) {
		_bot = _botManager->getBot(_botId);
	}
}

void BotConfigBox::prepare() {
	setTitle(rpl::single(QString("Configure: ") + _botId));

	loadConfig();

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	// General Settings
	content->add(object_ptr<Ui::FlatLabel>(
		content,
		QString("General Settings"),
		st::defaultFlatLabel));

	setupGeneralSettings();

	content->add(object_ptr<Ui::BoxContentDivider>(content));

	// Context Settings (bot-specific)
	if (_botId == "context_assistant") {
		content->add(object_ptr<Ui::FlatLabel>(
			content,
			QString("Context Settings"),
			st::defaultFlatLabel));

		setupContextSettings();

		content->add(object_ptr<Ui::BoxContentDivider>(content));
	}

	// Permissions
	content->add(object_ptr<Ui::FlatLabel>(
		content,
		QString("Permissions"),
		st::defaultFlatLabel));

	setupPermissions();

	// Buttons
	addButton(rpl::single(QString("Save")), [=] {
		saveConfig();
		closeBox();
	});

	addButton(rpl::single(QString("Reset to Defaults")), [=] {
		resetToDefaults();
	});

	addButton(rpl::single(QString("Cancel")), [=] {
		closeBox();
	});

	// TODO: Find correct box width constant
	// setDimensionsToContent(st::boxWideWidth, content);
}

void BotConfigBox::setupGeneralSettings() {
	// TODO: Implement when Checkbox widget type is available
	// const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	// Enable bot checkbox
	// _enableBot = content->add(object_ptr<Ui::Checkbox>(
	// 	content,
	// 	QString("Enable bot"),
	// 	_currentConfig.value("enabled").toBool(true),
	// 	st::defaultCheckbox));

	// Enable proactive help
	// _enableProactiveHelp = content->add(object_ptr<Ui::Checkbox>(
	// 	content,
	// 	QString("Enable proactive help"),
	// 	_currentConfig.value("enable_proactive_help").toBool(true),
	// 	st::defaultCheckbox));

	// Cross-chat analysis
	// _enableCrossChat = content->add(object_ptr<Ui::Checkbox>(
	// 	content,
	// 	QString("Cross-chat analysis"),
	// 	_currentConfig.value("enable_cross_chat").toBool(true),
	// 	st::defaultCheckbox));
}

void BotConfigBox::setupContextSettings() {
	// TODO: Implement when MediaSlider widget type is available
	// const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	// Max context messages slider
	// const auto maxMessagesWrap = content->add(
	// 	object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
	// 		content,
	// 		object_ptr<Ui::VerticalLayout>(content)));

	// const auto maxMessagesInner = maxMessagesWrap->entity();

	// _maxMessagesLabel = maxMessagesInner->add(object_ptr<Ui::FlatLabel>(
	// 	maxMessagesInner,
	// 	QString("Max context messages: 10"),
	// 	st::settingsAbout));

	// _maxMessagesSlider = maxMessagesInner->add(object_ptr<Ui::MediaSlider>(
	// 	maxMessagesInner,
	// 	st::defaultContinuousSlider));

	// _maxMessagesSlider->resize(st::boxWidth - 2 * st::boxPadding.left(), st::defaultContinuousSlider.seekSize.height());
	// _maxMessagesSlider->setPseudoDiscrete(
	// 	20,  // Max value
	// 	[](int val) { return val + 1; },  // Map to 1-20
	// 	_currentConfig.value("max_context_messages").toInt(10),
	// 	[=](int val) {
	// 		_maxMessagesLabel->setText(QString("Max context messages: %1").arg(val));
	// 	});

	// Timeout slider
	// const auto timeoutWrap = content->add(
	// 	object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
	// 		content,
	// 		object_ptr<Ui::VerticalLayout>(content)));

	// const auto timeoutInner = timeoutWrap->entity();

	// _timeoutLabel = timeoutInner->add(object_ptr<Ui::FlatLabel>(
	// 	timeoutInner,
	// 	QString("Context timeout: 30 min"),
	// 	st::settingsAbout));

	// _timeoutSlider = timeoutInner->add(object_ptr<Ui::MediaSlider>(
	// 	timeoutInner,
	// 	st::defaultContinuousSlider));

	// _timeoutSlider->resize(st::boxWidth - 2 * st::boxPadding.left(), st::defaultContinuousSlider.seekSize.height());
	// _timeoutSlider->setPseudoDiscrete(
	// 	60,  // Max 60 minutes
	// 	[](int val) { return (val + 1) * 5; },  // 5-min increments
	// 	_currentConfig.value("context_timeout_minutes").toInt(30) / 5,
	// 	[=](int val) {
	// 		_timeoutLabel->setText(QString("Context timeout: %1 min").arg(val));
	// 	});

	// Confidence slider
	// const auto confidenceWrap = content->add(
	// 	object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
	// 		content,
	// 		object_ptr<Ui::VerticalLayout>(content)));

	// const auto confidenceInner = confidenceWrap->entity();

	// _confidenceLabel = confidenceInner->add(object_ptr<Ui::FlatLabel>(
	// 	confidenceInner,
	// 	QString("Min confidence: 70%"),
	// 	st::settingsAbout));

	// _confidenceSlider = confidenceInner->add(object_ptr<Ui::MediaSlider>(
	// 	confidenceInner,
	// 	st::defaultContinuousSlider));

	// _confidenceSlider->resize(st::boxWidth - 2 * st::boxPadding.left(), st::defaultContinuousSlider.seekSize.height());
	// _confidenceSlider->setPseudoDiscrete(
	// 	100,  // 0-100%
	// 	[](int val) { return val; },
	// 	static_cast<int>(_currentConfig.value("min_confidence").toDouble(0.7) * 100),
	// 	[=](int val) {
	// 		_confidenceLabel->setText(QString("Min confidence: %1%").arg(val));
	// 	});
}

void BotConfigBox::setupMutedChats() {
	// TODO: Implement muted chats list
}

void BotConfigBox::setupPermissions() {
	// TODO: Implement when Checkbox widget type is available
	// const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	// if (!_bot) {
	// 	return;
	// }

	// QStringList permissions = _bot->requiredPermissions();

	// for (const QString &perm : permissions) {
	// 	bool granted = _bot->hasPermission(perm);

	// 	auto checkbox = content->add(object_ptr<Ui::Checkbox>(
	// 		content,
	// 		perm,
	// 		granted,
	// 		st::defaultCheckbox));

	// 	checkbox->setDisabled(true);  // Permissions are read-only
	// }
}

void BotConfigBox::loadConfig() {
	if (_bot) {
		_currentConfig = _bot->config();
	} else {
		// Default config
		_currentConfig = QJsonObject{
			{"enabled", true},
			{"enable_proactive_help", true},
			{"enable_cross_chat", true},
			{"max_context_messages", 10},
			{"context_timeout_minutes", 30},
			{"min_confidence", 0.7}
		};
	}
}

void BotConfigBox::saveConfig() {
	// TODO: Implement when widget types are available
	// QJsonObject config;

	// // General settings
	// config["enabled"] = _enableBot->checked();
	// config["enable_proactive_help"] = _enableProactiveHelp->checked();
	// config["enable_cross_chat"] = _enableCrossChat->checked();

	// // Context settings (if applicable)
	// if (_maxMessagesSlider) {
	// 	config["max_context_messages"] = _maxMessagesSlider->value();
	// }
	// if (_timeoutSlider) {
	// 	config["context_timeout_minutes"] = _timeoutSlider->value();
	// }
	// if (_confidenceSlider) {
	// 	config["min_confidence"] = _confidenceSlider->value() / 100.0;
	// }

	// // Save to bot manager
	// if (_botManager) {
	// 	_botManager->saveBotConfig(_botId, config);
	// }
}

void BotConfigBox::resetToDefaults() {
	// TODO: Implement when widget types are available
	// if (_bot) {
	// 	QJsonObject defaults = _bot->defaultConfig();
	// 	_currentConfig = defaults;

	// 	// Update UI
	// 	if (_enableBot) _enableBot->setChecked(defaults.value("enabled").toBool(true));
	// 	if (_enableProactiveHelp) _enableProactiveHelp->setChecked(defaults.value("enable_proactive_help").toBool(true));
	// 	if (_enableCrossChat) _enableCrossChat->setChecked(defaults.value("enable_cross_chat").toBool(true));

	// 	// Update sliders
	// 	if (_maxMessagesSlider) {
	// 		_maxMessagesSlider->setActiveValue(defaults.value("max_context_messages").toInt(10));
	// 	}
	// 	if (_timeoutSlider) {
	// 		_timeoutSlider->setActiveValue(defaults.value("context_timeout_minutes").toInt(30) / 5);
	// 	}
	// 	if (_confidenceSlider) {
	// 		_confidenceSlider->setActiveValue(static_cast<int>(defaults.value("min_confidence").toDouble(0.7) * 100));
	// 	}
	// }
}

void BotConfigBox::setInnerFocus() {
	// TODO: Implement when widget types are available
	// if (_enableBot) {
	// 	_enableBot->setFocus();
	// }
}
