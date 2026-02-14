/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_operations_log.h"

#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "lang/lang_keys.h"
#include "styles/style_widgets.h"
#include "styles/style_export.h"
#include "styles/style_layers.h"

#include <QtCore/QDateTime>

namespace Export {
namespace View {
namespace {

QString FormatBytes(qint64 bytes) {
	if (bytes < 1024) {
		return QString("%1 B").arg(bytes);
	} else if (bytes < 1024 * 1024) {
		return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
	} else if (bytes < 1024 * 1024 * 1024) {
		return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
	} else {
		return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
	}
}

} // namespace

OperationsLogWidget::OperationsLogWidget(QWidget *parent)
: RpWidget(parent)
, _scroll(Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll))
, _content(_scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(_scroll))) {

	// Progress label at top
	_progressLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		QString("Preparing export..."),
		st::exportProgressLabel);

	// Size label below progress
	_sizeLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		QString("Size: calculating..."),
		st::exportAboutLabel);

	// Top shadow below labels
	_topShadow = Ui::CreateChild<Ui::PlainShadow>(this);

	// Cancel button at bottom
	_cancel = base::make_unique_q<Ui::RoundButton>(
		this,
		tr::lng_export_stop(),
		st::exportCancelButton);
	setupBottomButton(_cancel.get());

	// Layout
	sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto padding = st::exportSettingPadding;
		const auto buttonHeight = _cancel ? _cancel->height() : 0;
		const auto bottomPadding = st::exportCancelBottom;

		// Progress label
		_progressLabel->resizeToWidth(size.width() - padding.left() - padding.right());
		_progressLabel->moveToLeft(padding.left(), padding.top());

		// Size label
		_sizeLabel->resizeToWidth(size.width() - padding.left() - padding.right());
		_sizeLabel->moveToLeft(padding.left(), _progressLabel->y() + _progressLabel->height() + 4);

		// Shadow
		const auto shadowTop = _sizeLabel->y() + _sizeLabel->height() + padding.top() / 2;
		_topShadow->setGeometry(0, shadowTop, size.width(), st::lineWidth);

		// Scroll area
		const auto scrollTop = shadowTop + st::lineWidth;
		const auto scrollHeight = size.height() - scrollTop - buttonHeight - bottomPadding - padding.bottom();
		_scroll->setGeometry(0, scrollTop, size.width(), scrollHeight);

		// Content inside scroll
		_content->resizeToWidth(size.width() - padding.left() - padding.right());
	}, lifetime());
}

void OperationsLogWidget::addLogEntry(const QString &message) {
	const auto timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
	const auto fullMessage = QString("[%1] %2").arg(timestamp, message);

	auto label = object_ptr<Ui::FlatLabel>(
		_content,
		fullMessage,
		st::exportAboutLabel);
	label->setTextColorOverride(st::windowFg->c);

	_content->add(std::move(label), st::exportSettingPadding);
	_logCount++;

	// Resize and scroll to bottom
	_content->resizeToWidth(_scroll->width() - st::exportSettingPadding.left() - st::exportSettingPadding.right());
	scrollToBottom();
}

void OperationsLogWidget::setProgress(int current, int total) {
	const auto percent = total > 0 ? (current * 100 / total) : 0;
	_progressLabel->setText(QString("Exporting: %1 / %2 messages (%3%)")
		.arg(current)
		.arg(total)
		.arg(percent));
}

void OperationsLogWidget::setSize(qint64 textBytes, qint64 mediaBytes) {
	const auto totalBytes = textBytes + mediaBytes;
	QString sizeText = QString("Size: %1 text").arg(FormatBytes(textBytes));
	if (mediaBytes > 0) {
		sizeText += QString(" + %1 media").arg(FormatBytes(mediaBytes));
	}
	sizeText += QString(" = %1 total").arg(FormatBytes(totalBytes));
	_sizeLabel->setText(sizeText);
}

void OperationsLogWidget::setFinished(const QString &path) {
	_finishedPath = path;
	_progressLabel->setText(QString("Export complete!"));

	// Replace cancel with done button
	_cancel = nullptr;
	_done = base::make_unique_q<Ui::RoundButton>(
		this,
		tr::lng_export_done(),
		st::exportDoneButton);

	const auto desired = std::min(
		st::exportDoneButton.style.font->width(tr::lng_export_done(tr::now))
		+ st::exportDoneButton.height
		- st::exportDoneButton.style.font->height,
		st::exportPanelSize.width() - 2 * st::exportCancelBottom);
	if (_done->width() < desired) {
		_done->setFullWidth(desired);
	}

	_done->clicks(
	) | rpl::to_empty
	| rpl::start_to_stream(_doneClicks, _done->lifetime());

	setupBottomButton(_done.get());

	addLogEntry(QString("Files saved to: %1").arg(path));
}

void OperationsLogWidget::scrollToBottom() {
	_scroll->scrollToY(_scroll->scrollTopMax());
}

void OperationsLogWidget::setupBottomButton(not_null<Ui::RoundButton*> button) {
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	button->show();

	sizeValue(
	) | rpl::on_next([=](QSize size) {
		button->move(
			(size.width() - button->width()) / 2,
			(size.height() - st::exportCancelBottom - button->height()));
	}, button->lifetime());
}

rpl::producer<> OperationsLogWidget::cancelClicks() const {
	return _cancel
		? (_cancel->clicks() | rpl::to_empty)
		: (rpl::never<>() | rpl::type_erased);
}

rpl::producer<> OperationsLogWidget::doneClicks() const {
	return _doneClicks.events();
}

OperationsLogWidget::~OperationsLogWidget() = default;

} // namespace View
} // namespace Export
