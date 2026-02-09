/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

namespace Ui {
class VerticalLayout;
class RoundButton;
class FlatLabel;
class ScrollArea;
class PlainShadow;
} // namespace Ui

namespace Export {
namespace View {

class OperationsLogWidget : public Ui::RpWidget {
public:
	OperationsLogWidget(QWidget *parent);

	void addLogEntry(const QString &message);
	void setProgress(int current, int total);
	void setSize(qint64 textBytes, qint64 mediaBytes);
	void setFinished(const QString &path);

	rpl::producer<> cancelClicks() const;
	rpl::producer<> doneClicks() const;

	~OperationsLogWidget();

private:
	void setupBottomButton(not_null<Ui::RoundButton*> button);
	void scrollToBottom();

	not_null<Ui::ScrollArea*> _scroll;
	not_null<Ui::VerticalLayout*> _content;
	QPointer<Ui::FlatLabel> _progressLabel;
	QPointer<Ui::FlatLabel> _sizeLabel;
	QPointer<Ui::PlainShadow> _topShadow;
	base::unique_qptr<Ui::RoundButton> _cancel;
	base::unique_qptr<Ui::RoundButton> _done;
	rpl::event_stream<> _doneClicks;

	QString _finishedPath;
	int _logCount = 0;
};

} // namespace View
} // namespace Export
