// Bot Framework Settings Panel - Implementation
// This file is part of Telegram Desktop MCP integration.
// Licensed under GPLv3 with OpenSSL exception.

#include "settings_bots.h"

#include "mcp/bot_manager.h"
#include "mcp/bot_base.h"
#include "boxes/bot_config_box.h"
#include "info/bot_statistics_widget.h"
#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

namespace Settings {

Bots::Bots(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller) {
	setupContent();
}

Bots::~Bots() = default;

rpl::producer<QString> Bots::title() {
	return rpl::single(QString("Bot Framework"));
}

void Bots::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	// Header
	auto header = object_ptr<Ui::FlatLabel>(
		content,
		QString("🤖 Bot Framework\nEnhance Telegram with intelligent bots"),
		st::defaultFlatLabel);
	// Note: setMargin not available, using default padding
	content->add(std::move(header));

	// Divider
	content->add(object_ptr<Ui::BoxContentDivider>(content));

	// Bot list container
	_botListContainer = content->add(
		object_ptr<Ui::VerticalLayout>(content));

	setupBotList(_botListContainer);

	// Global actions
	setupGlobalActions(content);

	Ui::ResizeFitChild(this, content);
}

void Bots::setupBotList(not_null<Ui::VerticalLayout*> container) {
	// TODO: Get BotManager from application
	// For now, create placeholder

	container->add(object_ptr<Ui::FlatLabel>(
		container,
		QString("Registered Bots"),
		st::defaultFlatLabel));

	// Example bot items (will be dynamic)
	const auto addExampleBot = [&](
			const QString &name,
			const QString &description,
			bool isPremium,
			bool isEnabled) {

		const auto wrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));

		const auto inner = wrap->entity();

		// Bot item layout
		const auto button = inner->add(object_ptr<Ui::SettingsButton>(
			inner,
			rpl::single(name),
			st::settingsButton));

		// Description
		inner->add(object_ptr<Ui::FlatLabel>(
			inner,
			description,
			st::defaultFlatLabel));

		// Configure button
		button->setClickedCallback([=] {
			// Open configuration
			showBotConfig(name);
		});

		if (isPremium) {
			inner->add(object_ptr<Ui::FlatLabel>(
				inner,
				QString("⚠️ Premium feature - Enable Pro"),
				st::defaultFlatLabel));
		}

		inner->add(object_ptr<Ui::BoxContentDivider>(inner));
	};

	// Add example bots
	addExampleBot(
		"Context-Aware AI Assistant",
		"Proactively offers help based on conversation context",
		false,
		true);

	addExampleBot(
		"Smart Message Scheduler",
		"Optimize send timing based on recipient activity",
		true,
		false);

	addExampleBot(
		"Advanced Search",
		"Semantic search with AI-powered understanding",
		false,
		true);

	addExampleBot(
		"Analytics Bot",
		"Privacy-preserving communication insights",
		false,
		true);

	addExampleBot(
		"Ephemeral Capture",
		"Save self-destructing messages",
		true,
		false);

	addExampleBot(
		"Multi-Chat Coordinator",
		"Smart message forwarding and digests",
		false,
		false);
}

void Bots::setupGlobalActions(not_null<Ui::VerticalLayout*> container) {
	container->add(object_ptr<Ui::BoxContentDivider>(container));

	// Action buttons row
	const auto buttons = container->add(
		object_ptr<Ui::VerticalLayout>(container));

	// Install Bot button
	const auto installButton = buttons->add(object_ptr<Ui::SettingsButton>(
		buttons,
		rpl::single(QString("📥 Install Bot...")),
		st::settingsButton));

	installButton->setClickedCallback([=] {
		installBot();
	});

	// Global Settings button
	const auto settingsButton = buttons->add(object_ptr<Ui::SettingsButton>(
		buttons,
		rpl::single(QString("⚙️ Global Settings")),
		st::settingsButton));

	settingsButton->setClickedCallback([=] {
		// Open global bot settings
		// TODO: Implement
	});

	// Statistics button
	const auto statsButton = buttons->add(object_ptr<Ui::SettingsButton>(
		buttons,
		rpl::single(QString("📊 Statistics")),
		st::settingsButton));

	statsButton->setClickedCallback([=] {
		showBotStats();
	});
}

void Bots::showBotConfig(const QString &botId) {
	// Open bot configuration dialog
	// TODO: Implement BotConfigBox
	/*
	_controller->show(Box<BotConfigBox>(
		_controller,
		botId,
		_botManager));
	*/
}

void Bots::showBotStats() {
	// Open bot statistics widget
	// TODO: Implement BotStatisticsWidget
	/*
	_controller->showSection(
		Info::BotStatistics::Make(_controller, _botManager));
	*/
}

void Bots::installBot() {
	// Open bot installation dialog
	// TODO: Implement bot marketplace/installation
}

void Bots::refreshBotList() {
	if (!_botManager) {
		return;
	}

	// Clear existing list
	// Re-populate from BotManager
	// TODO: Implement dynamic bot list
}

} // namespace Settings
