/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

/**
 * @file export_view_settings.cpp
 * @brief Export settings UI widget implementation for Telegram Desktop.
 *
 * This file implements the SettingsWidget class which provides the user interface
 * for configuring data export options. It supports two modes:
 *
 * 1. **Full Export Mode** (Settings → Privacy → Export Data):
 *    - Exports all Telegram data (chats, contacts, sessions, profile, etc.)
 *    - Uses radio buttons for single-format selection
 *    - Standard Telegram functionality
 *
 * 2. **Single Peer Export Mode** (Chat menu → Export chat):
 *    - Exports only one specific conversation
 *    - Custom enhancements in this fork:
 *      - Multi-format selection via checkboxes (HTML, JSON, Markdown)
 *      - "Unrestricted mode" option (pre-selected, bypasses rate limits)
 *      - Date range selection for partial exports
 *      - Panel height optimized to 620px (no scrollbar)
 *
 * Key Custom Functions:
 * - ChooseFormatBox(): Multi-select format dialog with checkboxes
 * - addSinglePeerFormatLabel(): Clickable format display with multi-format support
 * - addSinglePeerPathLabel(): Clickable download path display
 * - addUnrestrictedModeCheckbox(): Pre-selected gradual mode option
 *
 * Format Combinations Supported:
 * - Single: Html, Json, Markdown
 * - Pairs: HtmlAndJson, HtmlAndMarkdown, JsonAndMarkdown
 * - All: Html + Json + Markdown
 *
 * @see export_view_settings.h for class declarations
 * @see export.style for panel dimensions (exportSinglePeerPanelSize)
 * @see export_view_panel_controller.cpp for panel lifecycle management
 */

#include "export/view/export_view_settings.h"

#include "export/output/export_output_abstract.h"
#include "export/view/export_view_panel_controller.h"
#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/boxes/calendar_box.h"
#include "ui/boxes/choose_time.h"
#include "platform/platform_specific.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "styles/style_widgets.h"
#include "styles/style_export.h"
#include "styles/style_layers.h"

namespace Export {
namespace View {
namespace {

// =============================================================================
// CONSTANTS
// =============================================================================

// Size conversion constant: 1 megabyte = 1024 * 1024 bytes
// Used for media file size limit calculations in the export settings slider
constexpr auto kMegabyte = int64(1024) * 1024;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Extracts a PeerId from an MTPInputPeer object.
 *
 * This function handles all possible MTPInputPeer types returned by Telegram's
 * MTProto API and converts them to a unified PeerId format.
 *
 * @param session The current user's session (needed for resolving self peer)
 * @param data The MTProto InputPeer object to parse
 * @return The extracted PeerId, or 0 if the peer is empty
 *
 * Peer types handled:
 * - inputPeerUser / inputPeerUserFromMessage: Regular users
 * - inputPeerChat: Legacy group chats
 * - inputPeerChannel / inputPeerChannelFromMessage: Supergroups and channels
 * - inputPeerSelf: The current user's own peer ID
 * - inputPeerEmpty: Returns 0 (no peer selected)
 */
[[nodiscard]] PeerId ReadPeerId(
		not_null<Main::Session*> session,
		const MTPInputPeer &data) {
	return data.match([](const MTPDinputPeerUser &data) {
		return peerFromUser(data.vuser_id().v);
	}, [](const MTPDinputPeerUserFromMessage &data) {
		return peerFromUser(data.vuser_id().v);
	}, [](const MTPDinputPeerChat &data) {
		return peerFromChat(data.vchat_id().v);
	}, [](const MTPDinputPeerChannel &data) {
		return peerFromChannel(data.vchannel_id().v);
	}, [](const MTPDinputPeerChannelFromMessage &data) {
		return peerFromChannel(data.vchannel_id().v);
	}, [&](const MTPDinputPeerSelf &data) {
		return session->userPeerId();
	}, [](const MTPDinputPeerEmpty &data) {
		return PeerId(0);
	});
}

/**
 * @brief Multi-select format chooser dialog box.
 *
 * This is a custom enhancement over the original Telegram export format selection.
 * Instead of radio buttons (single selection), this dialog uses checkboxes to allow
 * users to export in multiple formats simultaneously (e.g., HTML + JSON + Markdown).
 *
 * @param box The GenericBox container that hosts this dialog
 * @param format The currently selected format(s) - used to initialize checkbox states
 * @param done Callback function invoked with the selected Format when user clicks Save
 *
 * Features:
 * - Three independent checkboxes: HTML, JSON, Markdown
 * - Prevents unchecking all boxes (at least one must remain selected)
 * - Combines selections into appropriate Format enum value (e.g., Format::All)
 *
 * Format enum values supported:
 * - Single: Html, Json, Markdown
 * - Pairs: HtmlAndJson, HtmlAndMarkdown, JsonAndMarkdown
 * - All three: All
 */
void ChooseFormatBox(
		not_null<Ui::GenericBox*> box,
		Output::Format format,
		Fn<void(Output::Format)> done) {
	using Format = Output::Format;

	// -----------------------------------------------------------------
	// Helper lambdas to decompose a Format enum into individual flags.
	// These check whether a combined format includes a specific type.
	// -----------------------------------------------------------------
	const auto hasHtml = [](Format f) {
		return f == Format::Html || f == Format::HtmlAndJson
			|| f == Format::HtmlAndMarkdown || f == Format::All;
	};
	const auto hasJson = [](Format f) {
		return f == Format::Json || f == Format::HtmlAndJson
			|| f == Format::JsonAndMarkdown || f == Format::All;
	};
	const auto hasMarkdown = [](Format f) {
		return f == Format::Markdown || f == Format::HtmlAndMarkdown
			|| f == Format::JsonAndMarkdown || f == Format::All;
	};

	// Track individual checkbox states
	const auto htmlChecked = std::make_shared<bool>(hasHtml(format));
	const auto jsonChecked = std::make_shared<bool>(hasJson(format));
	const auto markdownChecked = std::make_shared<bool>(hasMarkdown(format));

	// Store checkbox pointers
	const auto htmlBox = std::make_shared<Ui::Checkbox*>(nullptr);
	const auto jsonBox = std::make_shared<Ui::Checkbox*>(nullptr);
	const auto markdownBox = std::make_shared<Ui::Checkbox*>(nullptr);

	// Helper to count checked boxes
	const auto countChecked = [=]() {
		return (*htmlChecked ? 1 : 0) + (*jsonChecked ? 1 : 0) + (*markdownChecked ? 1 : 0);
	};

	box->setTitle(tr::lng_export_option_choose_format());

	// HTML checkbox
	*htmlBox = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_export_option_html(tr::now),
			*htmlChecked,
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	(*htmlBox)->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		if (!checked && countChecked() == 1) {
			// Prevent unchecking last one
			(*htmlBox)->setChecked(true);
		} else {
			*htmlChecked = checked;
		}
	}, (*htmlBox)->lifetime());

	// JSON checkbox
	*jsonBox = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_export_option_json(tr::now),
			*jsonChecked,
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	(*jsonBox)->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		if (!checked && countChecked() == 1) {
			(*jsonBox)->setChecked(true);
		} else {
			*jsonChecked = checked;
		}
	}, (*jsonBox)->lifetime());

	// Markdown checkbox
	*markdownBox = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			QString("Markdown"),
			*markdownChecked,
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	(*markdownBox)->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		if (!checked && countChecked() == 1) {
			(*markdownBox)->setChecked(true);
		} else {
			*markdownChecked = checked;
		}
	}, (*markdownBox)->lifetime());

	// Compute combined Format from checkbox states
	const auto computeFormat = [=]() -> Format {
		const bool h = *htmlChecked;
		const bool j = *jsonChecked;
		const bool m = *markdownChecked;
		if (h && j && m) return Format::All;
		if (h && j) return Format::HtmlAndJson;
		if (h && m) return Format::HtmlAndMarkdown;
		if (j && m) return Format::JsonAndMarkdown;
		if (h) return Format::Html;
		if (j) return Format::Json;
		return Format::Markdown;
	};

	box->addButton(tr::lng_settings_save(), [=] { done(computeFormat()); });
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

// =============================================================================
// PUBLIC UTILITY FUNCTIONS
// =============================================================================

/**
 * @brief Converts a slider index (0 to kSizeValueCount-1) to a file size limit in bytes.
 *
 * This function implements a non-linear scale for the media size slider:
 * - Index 1-10:  1MB to 10MB (increments of 1MB)
 * - Index 11-30: 12MB to 50MB (increments of 2MB)
 * - Index 31-40: 55MB to 100MB (increments of 5MB)
 * - Index 41-60: 110MB to 300MB (increments of 10MB)
 * - Index 61-70: 320MB to 500MB (increments of 20MB)
 * - Index 71-80: 550MB to 1000MB (increments of 50MB)
 * - Index 81-90: 1100MB to 2000MB (increments of 100MB)
 * - Index 91+:   2200MB+ (increments of 200MB)
 *
 * @param index Slider position (0 to kSizeValueCount-1)
 * @return Size limit in bytes
 */
int64 SizeLimitByIndex(int index) {
	Expects(index >= 0 && index < kSizeValueCount);

	index += 1;
	const auto megabytes = [&] {
		if (index <= 10) {
			return index;
		} else if (index <= 30) {
			return 10 + (index - 10) * 2;
		} else if (index <= 40) {
			return 50 + (index - 30) * 5;
		} else if (index <= 60) {
			return 100 + (index - 40) * 10;
		} else if (index <= 70) {
			return 300 + (index - 60) * 20;
		} else if (index <= 80) {
			return 500 + (index - 70) * 50;
		} else if (index <= 90) {
			return 1000 + (index - 80) * 100;
		} else {
			return 2000 + (index - 90) * 200;
		}
	}();
	return megabytes * kMegabyte;
}

// =============================================================================
// SETTINGSWIDGET CLASS IMPLEMENTATION
// =============================================================================

/**
 * @brief Constructor for the export settings widget.
 *
 * This widget provides the UI for configuring data export options. It supports
 * two modes:
 * 1. Full Export Mode: Export all Telegram data (chats, media, contacts, etc.)
 * 2. Single Peer Mode: Export only one specific chat (activated when _singlePeerId != 0)
 *
 * Single peer mode offers a simplified interface with:
 * - Multi-format selection (HTML, JSON, Markdown checkboxes)
 * - Path selection
 * - Date range limits
 * - "Unrestricted mode" option (bypasses server rate limits)
 *
 * @param parent Parent widget
 * @param session Current Telegram session (provides access to data/API)
 * @param data Initial export settings to populate the UI
 */
SettingsWidget::SettingsWidget(
	QWidget *parent,
	not_null<Main::Session*> session,
	Settings data)
: RpWidget(parent)
, _session(session)
, _singlePeerId(ReadPeerId(session, data.singlePeer))
, _internal_data(std::move(data)) {
	ResolveSettings(session, _internal_data);
	setupContent();
}

const Settings &SettingsWidget::readData() const {
	return _internal_data;
}

template <typename Callback>
void SettingsWidget::changeData(Callback &&callback) {
	callback(_internal_data);
	_changes.fire_copy(_internal_data);
}

void SettingsWidget::setupContent() {
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		this,
		st::boxScroll);
	const auto wrap = scroll->setOwnedWidget(
		object_ptr<Ui::OverrideMargins>(
			scroll,
			object_ptr<Ui::VerticalLayout>(scroll)));
	const auto content = static_cast<Ui::VerticalLayout*>(wrap->entity());

	const auto buttons = setupButtons(scroll, wrap);
	setupOptions(content);
	setupPathAndFormat(content);

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		scroll->resize(size.width(), size.height() - buttons->height());
		wrap->resizeToWidth(size.width());
		content->resizeToWidth(size.width());
	}, lifetime());
}

void SettingsWidget::setupOptions(not_null<Ui::VerticalLayout*> container) {
	if (!_singlePeerId) {
		setupFullExportOptions(container);
	}
	setupMediaOptions(container);
	if (!_singlePeerId) {
		setupOtherOptions(container);
	}
}

void SettingsWidget::setupFullExportOptions(
		not_null<Ui::VerticalLayout*> container) {
	addOptionWithAbout(
		container,
		tr::lng_export_option_info(tr::now),
		Type::PersonalInfo | Type::Userpics,
		tr::lng_export_option_info_about(tr::now));
	addOptionWithAbout(
		container,
		tr::lng_export_option_contacts(tr::now),
		Type::Contacts,
		tr::lng_export_option_contacts_about(tr::now));
	addOptionWithAbout(
		container,
		tr::lng_export_option_stories(tr::now),
		Type::Stories,
		tr::lng_export_option_stories_about(tr::now));
	addOptionWithAbout(
		container,
		tr::lng_export_option_profile_music(tr::now),
		Type::ProfileMusic,
		tr::lng_export_option_profile_music_about(tr::now));
	addHeader(container, tr::lng_export_header_chats(tr::now));
	addOption(
		container,
		tr::lng_export_option_personal_chats(tr::now),
		Type::PersonalChats);
	addOption(
		container,
		tr::lng_export_option_bot_chats(tr::now),
		Type::BotChats);
	addChatOption(
		container,
		tr::lng_export_option_private_groups(tr::now),
		Type::PrivateGroups);
	addChatOption(
		container,
		tr::lng_export_option_private_channels(tr::now),
		Type::PrivateChannels);
	addChatOption(
		container,
		tr::lng_export_option_public_groups(tr::now),
		Type::PublicGroups);
	addChatOption(
		container,
		tr::lng_export_option_public_channels(tr::now),
		Type::PublicChannels);
}

void SettingsWidget::setupMediaOptions(
		not_null<Ui::VerticalLayout*> container) {
	if (_singlePeerId != 0) {
		// For single peer export, add media options without size slider
		addMediaOption(
			container,
			tr::lng_export_option_photos(tr::now),
			MediaType::Photo);
		addMediaOption(
			container,
			tr::lng_export_option_video_files(tr::now),
			MediaType::Video);
		addMediaOption(
			container,
			tr::lng_export_option_voice_messages(tr::now),
			MediaType::VoiceMessage);
		addMediaOption(
			container,
			tr::lng_export_option_video_messages(tr::now),
			MediaType::VideoMessage);
		addMediaOption(
			container,
			tr::lng_export_option_stickers(tr::now),
			MediaType::Sticker);
		addMediaOption(
			container,
			tr::lng_export_option_gifs(tr::now),
			MediaType::GIF);
		addMediaOption(
			container,
			tr::lng_export_option_files(tr::now),
			MediaType::File);
		// No size slider for single peer export
		return;
	}
	const auto mediaWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto media = mediaWrap->entity();
	addHeader(media, tr::lng_export_header_media(tr::now));
	addMediaOptions(media);

	value() | rpl::map([](const Settings &data) {
		return data.types;
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](Settings::Types types) {
		mediaWrap->toggle((types & (Type::PersonalChats
			| Type::BotChats
			| Type::PrivateGroups
			| Type::PrivateChannels
			| Type::PublicGroups
			| Type::PublicChannels
			| Type::ProfileMusic)) != 0, anim::type::normal);
	}, mediaWrap->lifetime());

	widthValue(
	) | rpl::start_with_next([=](int width) {
		mediaWrap->resizeToWidth(width);
	}, mediaWrap->lifetime());
}

void SettingsWidget::setupOtherOptions(
		not_null<Ui::VerticalLayout*> container) {
	addHeader(container, tr::lng_export_header_other(tr::now));
	addOptionWithAbout(
		container,
		tr::lng_export_option_sessions(tr::now),
		Type::Sessions,
		tr::lng_export_option_sessions_about(tr::now));
	addOptionWithAbout(
		container,
		tr::lng_export_option_other(tr::now),
		Type::OtherData,
		tr::lng_export_option_other_about(tr::now));
}

/**
 * @brief Sets up the output format and path selection UI.
 *
 * This method handles both full export and single-peer export modes differently:
 *
 * **Single Peer Mode** (_singlePeerId != 0):
 * - Adds a "Format" header section
 * - Shows clickable format label (opens ChooseFormatBox for multi-select)
 * - Shows clickable path label (opens folder picker)
 * - Shows date range limits (from/till date pickers)
 * - Shows "Unrestricted mode" checkbox (pre-selected, enables gradual export)
 *
 * **Full Export Mode** (_singlePeerId == 0):
 * - Uses radio buttons for single-format selection (HTML, JSON, HTML+JSON, Markdown)
 * - Shows separate "Gradual export mode" checkbox
 *
 * @param container The vertical layout container to add widgets to
 */
void SettingsWidget::setupPathAndFormat(
		not_null<Ui::VerticalLayout*> container) {
	if (_singlePeerId != 0) {
		// === SINGLE PEER EXPORT MODE ===
		// Simplified UI with multi-format support and unrestricted mode
		addHeader(container, tr::lng_export_header_format(tr::now));
		addSinglePeerFormatLabel(container);
		addSinglePeerPathLabel(container);
		addLimitsLabel(container);
		// Add extra vertical spacing before Unrestricted mode
		container->add(object_ptr<Ui::FixedHeightWidget>(container, 8));
		addUnrestrictedModeCheckbox(container);
		return;
	}
	// === FULL EXPORT MODE ===
	// Traditional radio button interface for format selection
	const auto formatGroup = std::make_shared<Ui::RadioenumGroup<Format>>(
		readData().format);
	formatGroup->setChangedCallback([=](Format format) {
		changeData([&](Settings &data) {
			data.format = format;
		});
	});
	const auto addFormatOption = [&](QString label, Format format) {
		container->add(
			object_ptr<Ui::Radioenum<Format>>(
				container,
				formatGroup,
				format,
				label,
				st::defaultBoxCheckbox),
			st::exportSettingPadding);
	};
	addHeader(container, tr::lng_export_header_format(tr::now));
	addLocationLabel(container);
	addFormatOption(tr::lng_export_option_html(tr::now), Format::Html);
	addFormatOption(tr::lng_export_option_json(tr::now), Format::Json);
	addFormatOption(tr::lng_export_option_html_and_json(tr::now), Format::HtmlAndJson);
	addFormatOption(QString("Markdown"), Format::Markdown);

	// Gradual mode checkbox
	const auto gradualCheck = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			QString("Gradual export mode (bypasses restrictions)"),
			false,
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	gradualCheck->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			data.gradualMode = checked;
		});
	}, gradualCheck->lifetime());
}

void SettingsWidget::addLocationLabel(
		not_null<Ui::VerticalLayout*> container) {
#ifndef OS_MAC_STORE
	auto pathLink = value() | rpl::map([](const Settings &data) {
		return data.path;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](const QString &path) {
		const auto text = IsDefaultPath(_session, path)
			? Core::App().canReadDefaultDownloadPath()
			? u"Downloads/"_q + File::DefaultDownloadPathFolder(_session)
			: tr::lng_download_path_temp(tr::now)
			: path;
		return Ui::Text::Link(
			QDir::toNativeSeparators(text),
			QString("internal:edit_export_path"));
	});
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_export_option_location(
				lt_path,
				std::move(pathLink),
				Ui::Text::WithEntities),
			st::exportLocationLabel),
		st::exportLocationPadding);
	label->overrideLinkClickHandler([=] {
		chooseFolder();
	});
#endif // OS_MAC_STORE
}

/**
 * @brief Opens the format chooser dialog.
 *
 * Displays the ChooseFormatBox dialog which allows multi-select format
 * selection. When the user saves their selection, the format is updated
 * and the dialog closes.
 *
 * This is invoked when the user clicks on the format link in the
 * single-peer export settings panel.
 */
void SettingsWidget::chooseFormat() {
	// Keep a weak reference to close the box after selection
	const auto shared = std::make_shared<base::weak_qptr<Ui::GenericBox>>();

	// Callback invoked when user saves format selection
	const auto callback = [=](Format format) {
		changeData([&](Settings &data) {
			data.format = format;
		});
		if (const auto strong = shared->get()) {
			strong->closeBox();
		}
	};

	// Create and show the format chooser dialog
	auto box = Box(
		ChooseFormatBox,
		readData().format,
		callback);
	*shared = base::make_weak(box.data());
	_showBoxCallback(std::move(box));
}

void SettingsWidget::addFormatAndLocationLabel(
		not_null<Ui::VerticalLayout*> container) {
#ifndef OS_MAC_STORE
	auto pathLink = value() | rpl::map([](const Settings &data) {
		return data.path;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](const QString &path) {
		const auto text = IsDefaultPath(_session, path)
			? Core::App().canReadDefaultDownloadPath()
			? u"Downloads/"_q + File::DefaultDownloadPathFolder(_session)
			: tr::lng_download_path_temp(tr::now)
			: path;
		return Ui::Text::Link(
			QDir::toNativeSeparators(text),
			u"internal:edit_export_path"_q);
	});
	auto formatLink = value() | rpl::map([](const Settings &data) {
		return data.format;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](Format format) {
		const auto text = (format == Format::Html)
			? "HTML"
			: (format == Format::Json)
			? "JSON"
			: tr::lng_export_option_html_and_json(tr::now);
		return Ui::Text::Link(text, u"internal:edit_format"_q);
	});
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_export_option_format_location(
				lt_format,
				std::move(formatLink),
				lt_path,
				std::move(pathLink),
				Ui::Text::WithEntities),
			st::exportLocationLabel),
		st::exportLocationPadding);
	label->overrideLinkClickHandler([=](const QString &url) {
		if (url == u"internal:edit_export_path"_q) {
			chooseFolder();
		} else if (url == u"internal:edit_format"_q) {
			chooseFormat();
		} else {
			Unexpected("Click handler URL in export limits edit.");
		}
	});
#endif // OS_MAC_STORE
}

/**
 * @brief Adds a clickable format label for single-peer export.
 *
 * Creates a label that displays the currently selected export format(s) as a
 * clickable link. When clicked, opens the ChooseFormatBox dialog for multi-format
 * selection.
 *
 * Display format examples:
 * - Single format: "Format: HTML" or "Format: JSON" or "Format: Markdown"
 * - Two formats: "Format: HTML, JSON" or "Format: HTML, Markdown"
 * - All three: "Format: HTML, JSON, Markdown"
 *
 * The label automatically updates when the format selection changes via the
 * rpl reactive stream from value().
 *
 * @param container The vertical layout container to add the label to
 */
void SettingsWidget::addSinglePeerFormatLabel(
		not_null<Ui::VerticalLayout*> container) {
	// Reactive stream that updates the label text whenever format changes
	auto formatText = value() | rpl::map([](const Settings &data) {
		return data.format;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](Format format) {
		// Build human-readable format string based on enum value
		QString formatName;
		switch (format) {
		case Format::Html:
			formatName = u"HTML"_q;
			break;
		case Format::Json:
			formatName = u"JSON"_q;
			break;
		case Format::Markdown:
			formatName = u"Markdown"_q;
			break;
		case Format::HtmlAndJson:
			formatName = u"HTML, JSON"_q;
			break;
		case Format::HtmlAndMarkdown:
			formatName = u"HTML, Markdown"_q;
			break;
		case Format::JsonAndMarkdown:
			formatName = u"JSON, Markdown"_q;
			break;
		case Format::All:
			formatName = u"HTML, JSON, Markdown"_q;
			break;
		}
		auto result = TextWithEntities{ u"Format: "_q };
		result.append(Ui::Text::Link(formatName, u"internal:edit_format"_q));
		return result;
	});
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(formatText),
			st::exportLocationLabel),
		st::exportLocationPadding);
	label->overrideLinkClickHandler([=] {
		chooseFormat();
	});
}

/**
 * @brief Adds a clickable download path label for single-peer export.
 *
 * Creates a label that displays the current export destination folder as a
 * clickable link. When clicked, opens a native folder picker dialog.
 *
 * Display format: "Download path: /path/to/folder"
 *
 * Special handling:
 * - If using default Downloads folder, shows "Downloads/TelegramExport"
 * - If temp path is being used (Mac App Store), shows localized temp message
 * - For custom paths, shows the full native path
 *
 * Note: This is excluded from Mac App Store builds (#ifndef OS_MAC_STORE)
 * because sandboxed apps have restricted file access.
 *
 * @param container The vertical layout container to add the label to
 */
void SettingsWidget::addSinglePeerPathLabel(
		not_null<Ui::VerticalLayout*> container) {
#ifndef OS_MAC_STORE
	// Reactive stream that updates the path display whenever it changes
	auto pathText = value() | rpl::map([](const Settings &data) {
		return data.path;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](const QString &path) {
		const auto pathDisplay = IsDefaultPath(_session, path)
			? Core::App().canReadDefaultDownloadPath()
			? u"Downloads/"_q + File::DefaultDownloadPathFolder(_session)
			: tr::lng_download_path_temp(tr::now)
			: path;
		auto result = TextWithEntities{ u"Download path: "_q };
		result.append(Ui::Text::Link(
			QDir::toNativeSeparators(pathDisplay),
			u"internal:edit_export_path"_q));
		return result;
	});
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(pathText),
			st::exportLocationLabel),
		st::exportLocationPadding);
	label->overrideLinkClickHandler([=] {
		chooseFolder();
	});
#endif // OS_MAC_STORE
}

/**
 * @brief Adds the "Unrestricted mode" checkbox for single-peer export.
 *
 * This checkbox enables "gradual mode" export, which is a custom enhancement
 * that bypasses Telegram's server-side rate limits and restrictions on data
 * export. When enabled, the export process works more gradually and can access
 * data that might otherwise be blocked.
 *
 * **Important**: This checkbox is PRE-SELECTED by default (initialChecked = true)
 * to provide the best experience for users exporting single chats.
 *
 * The checkbox controls Settings::gradualMode, which affects how the export
 * controller handles the data retrieval process.
 *
 * @param container The vertical layout container to add the checkbox to
 *
 * @see Settings::gradualMode
 */
void SettingsWidget::addUnrestrictedModeCheckbox(
		not_null<Ui::VerticalLayout*> container) {
	// === UNRESTRICTED MODE (GRADUAL EXPORT) ===
	// Pre-selected by default for better user experience
	const auto initialChecked = true;

	// Initialize gradualMode to true if not already set
	if (!readData().gradualMode) {
		changeData([](Settings &data) {
			data.gradualMode = true;
		});
	}
	const auto checkbox = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			QString::fromUtf8("Unrestricted mode"),
			initialChecked,
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	checkbox->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			data.gradualMode = checked;
		});
	}, checkbox->lifetime());

	// Fine-print explanation below the checkbox
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			QString::fromUtf8("Exports from local cache, bypassing server restrictions. Only includes messages already loaded in this client."),
			st::exportAboutOptionLabel),
		st::exportAboutOptionPadding);
}

void SettingsWidget::addLimitsLabel(
		not_null<Ui::VerticalLayout*> container) {
	const auto makeLink = [](const QString &text, const QString &url) {
		return Ui::Text::Link(text, url);
	};

	auto fromDateLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerFrom;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](TimeId from) {
		return (from
			? rpl::single(langDayOfMonthFull(
				base::unixtime::parse(from).date()))
			: tr::lng_export_beginning()
		) | rpl::map([=](const QString &text) {
			return makeLink(text, u"internal:edit_from"_q);
		});
	}) | rpl::flatten_latest();

	const auto mapToTime = [=](TimeId id, const QString &link) {
		return rpl::single(id
			? QLocale().toString(
				base::unixtime::parse(id).time(),
				QLocale::ShortFormat)
			: QString()
		) | rpl::map([=](const QString &text) {
			return makeLink(text, link);
		});
	};

	const auto concat = [](TextWithEntities date, TextWithEntities link) {
		return link.text.isEmpty()
			? date
			: date.append(u", "_q).append(std::move(link));
	};

	auto fromTimeLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerFrom;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](TimeId from) {
		return mapToTime(from, u"internal:edit_from_time"_q);
	}) | rpl::flatten_latest();

	auto fromLink = rpl::combine(
		std::move(fromDateLink),
		std::move(fromTimeLink)
	) | rpl::map(concat);

	auto tillDateLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerTill;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](TimeId till) {
		return (till
			? rpl::single(langDayOfMonthFull(
				base::unixtime::parse(till).date()))
			: tr::lng_export_end()
		) | rpl::map([=](const QString &text) {
			return makeLink(text, u"internal:edit_till"_q);
		});
	}) | rpl::flatten_latest();

	auto tillTimeLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerTill;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](TimeId till) {
		return mapToTime(till, u"internal:edit_till_time"_q);
	}) | rpl::flatten_latest();

	auto tillLink = rpl::combine(
		std::move(tillDateLink),
		std::move(tillTimeLink)
	) | rpl::map(concat);

	auto datesText = tr::lng_export_limits(
		lt_from,
		std::move(fromLink),
		lt_till,
		std::move(tillLink),
		Ui::Text::WithEntities
	) | rpl::after_next([=] {
		container->resizeToWidth(container->width());
	});

	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(datesText),
			st::exportLocationLabel),
		st::exportLocationPadding);

	const auto removeTime = [](TimeId dateTime) {
		return base::unixtime::serialize(
			QDateTime(
				base::unixtime::parse(dateTime).date(),
				QTime()));
	};

	const auto editTimeLimit = [=](Fn<TimeId()> now, Fn<void(TimeId)> done) {
		_showBoxCallback(Box([=](not_null<Ui::GenericBox*> box) {
			auto result = Ui::ChooseTimeWidget(
				box->verticalLayout(),
				[&] {
					const auto time = base::unixtime::parse(now()).time();
					return time.hour() * 3600
						+ time.minute() * 60
						+ time.second();
				}(),
				true);
			const auto widget = box->addRow(std::move(result.widget));
			const auto toSave = widget->lifetime().make_state<TimeId>(0);
			std::move(
				result.secondsValue
			) | rpl::start_with_next([=](TimeId t) {
				*toSave = t;
			}, box->lifetime());
			box->addButton(tr::lng_settings_save(), [=] {
				done(*toSave);
				box->closeBox();
			});
			box->addButton(tr::lng_cancel(), [=] {
				box->closeBox();
			});
			box->setTitle(tr::lng_settings_ttl_after_custom());
		}));
	};

	constexpr auto kOffset = 600;

	label->overrideLinkClickHandler([=](const QString &url) {
		if (url == u"internal:edit_from"_q) {
			const auto done = [=](TimeId limit) {
				changeData([&](Settings &settings) {
					settings.singlePeerFrom = limit;
				});
			};
			editDateLimit(
				readData().singlePeerFrom,
				0,
				readData().singlePeerTill,
				tr::lng_export_from_beginning(),
				done);
		} else if (url == u"internal:edit_from_time"_q) {
			const auto now = [=] {
				auto result = TimeId(0);
				changeData([&](Settings &settings) {
					result = settings.singlePeerFrom;
				});
				return result;
			};
			const auto done = [=](TimeId time) {
				changeData([&](Settings &settings) {
					const auto result = time
						+ removeTime(settings.singlePeerFrom);
					if (result >= settings.singlePeerTill
							&& settings.singlePeerTill) {
						settings.singlePeerFrom = settings.singlePeerTill
							- kOffset;
					} else {
						settings.singlePeerFrom = result;
					}
				});
			};
			editTimeLimit(now, done);
		} else if (url == u"internal:edit_till"_q) {
			const auto done = [=](TimeId limit) {
				changeData([&](Settings &settings) {
					if (limit <= settings.singlePeerFrom
							&& settings.singlePeerFrom) {
						settings.singlePeerTill = settings.singlePeerFrom
							+ kOffset;
					} else {
						settings.singlePeerTill = limit;
					}
				});
			};
			editDateLimit(
				readData().singlePeerTill,
				readData().singlePeerFrom,
				0,
				tr::lng_export_till_end(),
				done);
		} else if (url == u"internal:edit_till_time"_q) {
			const auto now = [=] {
				auto result = TimeId(0);
				changeData([&](Settings &settings) {
					result = settings.singlePeerTill;
				});
				return result;
			};
			const auto done = [=](TimeId time) {
				changeData([&](Settings &settings) {
					const auto result = time
						+ removeTime(settings.singlePeerTill);
					if (result <= settings.singlePeerFrom
							&& settings.singlePeerFrom) {
						settings.singlePeerTill = settings.singlePeerFrom
							+ kOffset;
					} else {
						settings.singlePeerTill = result;
					}
				});
			};
			editTimeLimit(now, done);
		} else {
			Unexpected("Click handler URL in export limits edit.");
		}
	});
}

void SettingsWidget::editDateLimit(
		TimeId current,
		TimeId min,
		TimeId max,
		rpl::producer<QString> resetLabel,
		Fn<void(TimeId)> done) {
	Expects(_showBoxCallback != nullptr);

	const auto highlighted = current
		? base::unixtime::parse(current).date()
		: max
		? base::unixtime::parse(max).date()
		: min
		? base::unixtime::parse(min).date()
		: QDate::currentDate();
	const auto month = highlighted;
	const auto shared = std::make_shared<base::weak_qptr<Ui::CalendarBox>>();
	const auto finalize = [=](not_null<Ui::CalendarBox*> box) {
		box->addLeftButton(std::move(resetLabel), crl::guard(this, [=] {
			done(0);
			if (const auto weak = shared->get()) {
				weak->closeBox();
			}
		}));
	};
	const auto callback = crl::guard(this, [=](const QDate &date) {
		done(base::unixtime::serialize(date.startOfDay()));
		if (const auto weak = shared->get()) {
			weak->closeBox();
		}
	});
	auto box = Box<Ui::CalendarBox>(Ui::CalendarBoxArgs{
		.month = month,
		.highlighted = highlighted,
		.callback = callback,
		.finalize = finalize,
		.st = st::exportCalendarSizes,
		.minDate = (min
			? base::unixtime::parse(min).date()
			: QDate(2013, 8, 1)), // Telegram was launched in August 2013 :)
		.maxDate = (max
			? base::unixtime::parse(max).date()
			: QDate::currentDate()),
	});
	*shared = base::make_weak(box.data());
	_showBoxCallback(std::move(box));
}

not_null<Ui::RpWidget*> SettingsWidget::setupButtons(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::RpWidget*> wrap) {
	using namespace rpl::mappers;

	const auto buttonsPadding = st::defaultBox.buttonPadding;
	const auto buttonsHeight = buttonsPadding.top()
		+ st::defaultBoxButton.height
		+ buttonsPadding.bottom();
	const auto buttons = Ui::CreateChild<Ui::FixedHeightWidget>(
		this,
		buttonsHeight);
	const auto topShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	const auto bottomShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	topShadow->toggleOn(scroll->scrollTopValue(
	) | rpl::map(_1 > 0));
	bottomShadow->toggleOn(rpl::combine(
		scroll->heightValue(),
		scroll->scrollTopValue(),
		wrap->heightValue(),
		_2
	) | rpl::map([=](int top) {
		return top < scroll->scrollTopMax();
	}));

	value() | rpl::map([](const Settings &data) {
		return (data.types != Types(0)) || data.onlySinglePeer();
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool canStart) {
		refreshButtons(buttons, canStart);
		topShadow->raise();
		bottomShadow->raise();
	}, buttons->lifetime());

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		buttons->resizeToWidth(size.width());
		buttons->moveToLeft(0, size.height() - buttons->height());
		topShadow->resizeToWidth(size.width());
		topShadow->moveToLeft(0, 0);
		bottomShadow->resizeToWidth(size.width());
		bottomShadow->moveToLeft(0, buttons->y() - st::lineWidth);
	}, buttons->lifetime());

	return buttons;
}

void SettingsWidget::addHeader(
		not_null<Ui::VerticalLayout*> container,
		const QString &text) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			text,
			st::exportHeaderLabel),
		st::exportHeaderPadding);
}

not_null<Ui::Checkbox*> SettingsWidget::addOption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		Types types) {
	const auto checkbox = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			text,
			((readData().types & types) == types),
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	checkbox->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.types |= types;
			} else {
				data.types &= ~types;
			}
		});
	}, checkbox->lifetime());
	return checkbox;
}

not_null<Ui::Checkbox*> SettingsWidget::addOptionWithAbout(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		Types types,
		const QString &about) {
	const auto result = addOption(container, text, types);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			about,
			st::exportAboutOptionLabel),
		st::exportAboutOptionPadding);
	return result;
}

void SettingsWidget::addChatOption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		Types types) {
	const auto checkbox = addOption(container, text, types);
	const auto onlyMy = container->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			container,
			object_ptr<Ui::Checkbox>(
				container,
				tr::lng_export_option_only_my(tr::now),
				((readData().fullChats & types) != types),
				st::defaultBoxCheckbox),
			st::exportSubSettingPadding));

	onlyMy->entity()->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.fullChats &= ~types;
			} else {
				data.fullChats |= types;
			}
		});
	}, onlyMy->lifetime());

	onlyMy->toggleOn(checkbox->checkedValue());

	if (types & (Type::PublicGroups | Type::PublicChannels)) {
		onlyMy->entity()->setChecked(true);
		onlyMy->entity()->setDisabled(true);
	}
}

void SettingsWidget::addMediaOptions(
		not_null<Ui::VerticalLayout*> container) {
	addMediaOption(
		container,
		tr::lng_export_option_photos(tr::now),
		MediaType::Photo);
	addMediaOption(
		container,
		tr::lng_export_option_video_files(tr::now),
		MediaType::Video);
	addMediaOption(
		container,
		tr::lng_export_option_voice_messages(tr::now),
		MediaType::VoiceMessage);
	addMediaOption(
		container,
		tr::lng_export_option_video_messages(tr::now),
		MediaType::VideoMessage);
	addMediaOption(
		container,
		tr::lng_export_option_stickers(tr::now),
		MediaType::Sticker);
	addMediaOption(
		container,
		tr::lng_export_option_gifs(tr::now),
		MediaType::GIF);
	addMediaOption(
		container,
		tr::lng_export_option_files(tr::now),
		MediaType::File);
	addSizeSlider(container);
}

void SettingsWidget::addMediaOption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		MediaType type) {
	const auto checkbox = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			text,
			((readData().media.types & type) == type),
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	checkbox->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.media.types |= type;
			} else {
				data.media.types &= ~type;
			}
		});
	}, checkbox->lifetime());
}

void SettingsWidget::addSizeSlider(
		not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;

	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::exportFileSizeSlider),
		st::exportFileSizePadding);
	slider->resize(st::exportFileSizeSlider.seekSize);
	slider->setPseudoDiscrete(
		kSizeValueCount,
		SizeLimitByIndex,
		readData().media.sizeLimit,
		[=](int64 limit) {
			changeData([&](Settings &data) {
				data.media.sizeLimit = limit;
			});
		});

	const auto label = Ui::CreateChild<Ui::LabelSimple>(
		container.get(),
		st::exportFileSizeLabel);
	value() | rpl::map([](const Settings &data) {
		return data.media.sizeLimit;
	}) | rpl::start_with_next([=](int64 sizeLimit) {
		const auto limit = sizeLimit / kMegabyte;
		const auto size = QString::number(limit) + " MB";
		const auto text = tr::lng_export_option_size_limit(
			tr::now,
			lt_size,
			size);
		label->setText(text);
	}, slider->lifetime());

	rpl::combine(
		label->widthValue(),
		slider->geometryValue(),
		_2
	) | rpl::start_with_next([=](QRect geometry) {
		label->moveToRight(
			st::exportFileSizePadding.right(),
			geometry.y() - label->height() - st::exportFileSizeLabelBottom);
	}, label->lifetime());
}

void SettingsWidget::refreshButtons(
		not_null<Ui::RpWidget*> container,
		bool canStart) {
	container->hideChildren();
	const auto children = container->children();
	for (const auto child : children) {
		if (child->isWidgetType()) {
			child->deleteLater();
		}
	}
	const auto start = canStart
		? Ui::CreateChild<Ui::RoundButton>(
			container.get(),
			tr::lng_export_start(),
			st::defaultBoxButton)
		: nullptr;
	if (start) {
		start->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		start->show();
		_startClicks = start->clicks() | rpl::to_empty;

		container->sizeValue(
		) | rpl::start_with_next([=](QSize size) {
			const auto right = st::defaultBox.buttonPadding.right();
			const auto top = st::defaultBox.buttonPadding.top();
			start->moveToRight(right, top);
		}, start->lifetime());
	}

	const auto cancel = Ui::CreateChild<Ui::RoundButton>(
		container.get(),
		tr::lng_cancel(),
		st::defaultBoxButton);
	cancel->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	cancel->show();
	_cancelClicks = cancel->clicks() | rpl::to_empty;

	rpl::combine(
		container->sizeValue(),
		start ? start->widthValue() : rpl::single(0)
	) | rpl::start_with_next([=](QSize size, int width) {
		const auto right = st::defaultBox.buttonPadding.right()
			+ (width ? width + st::defaultBox.buttonPadding.left() : 0);
		const auto top = st::defaultBox.buttonPadding.top();
		cancel->moveToRight(right, top);
	}, cancel->lifetime());
}

void SettingsWidget::chooseFolder() {
	const auto callback = [=](QString &&result) {
		changeData([&](Settings &data) {
			data.path = std::move(result);
			data.forceSubPath = IsDefaultPath(_session, data.path);
		});
	};
	FileDialog::GetFolder(
		this,
		tr::lng_export_folder(tr::now),
		readData().path,
		callback);
}

rpl::producer<Settings> SettingsWidget::changes() const {
	return _changes.events();
}

rpl::producer<Settings> SettingsWidget::value() const {
	return rpl::single(readData()) | rpl::then(changes());
}

rpl::producer<> SettingsWidget::startClicks() const {
	return _startClicks.value(
	) | rpl::map([](Wrap &&wrap) {
		return std::move(wrap.value);
	}) | rpl::flatten_latest();
}

rpl::producer<> SettingsWidget::cancelClicks() const {
	return _cancelClicks.value(
	) | rpl::map([](Wrap &&wrap) {
		return std::move(wrap.value);
	}) | rpl::flatten_latest();
}

} // namespace View
} // namespace Export
