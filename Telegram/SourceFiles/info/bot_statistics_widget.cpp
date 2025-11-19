// Bot Statistics Widget - Implementation
// This file is part of Telegram Desktop MCP integration.
// Licensed under GPLv3 with OpenSSL exception.

#include "bot_statistics_widget.h"

#include "mcp/bot_manager.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

namespace Info {
namespace BotStatistics {

Widget::Widget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	MCP::BotManager *botManager)
: RpWidget(parent)
, _controller(controller)
, _botManager(botManager) {
	setupContent();
}

void Widget::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	// Header
	auto header = content->add(object_ptr<Ui::FlatLabel>(
		content,
		QString("Bot Statistics"),
		st::defaultFlatLabel));

	content->add(object_ptr<Ui::BoxContentDivider>(content));

	// System Overview
	setupSystemOverview();

	content->add(object_ptr<Ui::BoxContentDivider>(content));

	// Bot Performance
	setupBotPerformance();

	content->add(object_ptr<Ui::BoxContentDivider>(content));

	// Activity Chart
	setupActivityChart();

	content->add(object_ptr<Ui::BoxContentDivider>(content));

	// Actions
	setupActions();

	Ui::ResizeFitChild(this, content);
	refreshStats();
}

void Widget::setupSystemOverview() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	content->add(object_ptr<Ui::FlatLabel>(
		content,
		QString("System Overview"),
		st::defaultFlatLabel));

	// Stats cards
	const auto cards = content->add(object_ptr<Ui::VerticalLayout>(content));

	_totalBotsLabel = cards->add(object_ptr<Ui::FlatLabel>(
		cards,
		QString("6 Bots Total"),
		st::defaultFlatLabel));

	_runningBotsLabel = cards->add(object_ptr<Ui::FlatLabel>(
		cards,
		QString("4 Running"),
		st::defaultFlatLabel));

	_uptimeLabel = cards->add(object_ptr<Ui::FlatLabel>(
		cards,
		QString("99.8% Uptime"),
		st::defaultFlatLabel));
}

void Widget::setupBotPerformance() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	content->add(object_ptr<Ui::FlatLabel>(
		content,
		QString("Bot Performance"),
		st::defaultFlatLabel));

	_performanceContainer = content->add(
		object_ptr<Ui::VerticalLayout>(content));

	// Add bot performance items
	const auto addPerfItem = [&](
			const QString &name,
			int messages,
			int commands,
			double avgTime,
			int errors) {

		auto item = _performanceContainer->add(object_ptr<Ui::FlatLabel>(
			_performanceContainer,
			QString("%1\nMessages: %2 | Commands: %3 | Avg: %4ms | Errors: %5")
				.arg(name)
				.arg(messages)
				.arg(commands)
				.arg(avgTime, 0, 'f', 1)
				.arg(errors),
			st::defaultFlatLabel));
	};

	// Example data
	addPerfItem("Context Assistant", 1284, 42, 8.2, 3);
	addPerfItem("Advanced Search", 89, 0, 124.0, 0);
	addPerfItem("Analytics Bot", 15, 0, 1200.0, 0);
}

void Widget::setupActivityChart() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	content->add(object_ptr<Ui::FlatLabel>(
		content,
		QString("Recent Activity (Last 24h)"),
		st::defaultFlatLabel));

	// Activity chart
	_chartWidget = content->add(object_ptr<ActivityChart>(content));
	_chartWidget->resize(400, 200);

	// Set example data
	QVector<int> sampleData = {50, 80, 120, 90, 150, 200, 180, 220, 190, 240, 280, 260};
	static_cast<ActivityChart*>(_chartWidget)->setData(sampleData);
}

void Widget::setupActions() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	// Export button
	const auto exportBtn = content->add(object_ptr<Ui::SettingsButton>(
		content,
		rpl::single(QString("Export Data")),
		st::settingsButton));

	exportBtn->setClickedCallback([=] {
		exportData();
	});

	// Reset stats button
	const auto resetBtn = content->add(object_ptr<Ui::SettingsButton>(
		content,
		rpl::single(QString("Reset Stats")),
		st::settingsButton));

	resetBtn->setClickedCallback([=] {
		resetStats();
	});
}

void Widget::refreshStats() {
	if (!_botManager) {
		return;
	}

	// Get stats from bot manager
	auto allStats = _botManager->getAllStats();
	QVector<MCP::BotBase*> allBots = _botManager->getAllBots();
	QVector<MCP::BotBase*> runningBots = _botManager->getRunningBots();

	// Update system overview
	_totalBotsLabel->setText(QString("%1 Bots Total").arg(allBots.size()));
	_runningBotsLabel->setText(QString("%1 Running").arg(runningBots.size()));
	_uptimeLabel->setText(QString("99.8% Uptime"));  // TODO: Calculate real uptime

	// TODO: Update bot performance with real data
	// TODO: Update activity chart with real data
}

void Widget::exportData() {
	// TODO: Implement data export (CSV/JSON)
}

void Widget::resetStats() {
	if (!_botManager) {
		return;
	}

	// Reset all bot statistics
	QVector<MCP::BotBase*> allBots = _botManager->getAllBots();
	for (auto *bot : allBots) {
		auto botInfo = bot->info();
		_botManager->resetStats(botInfo.id);
	}

	refreshStats();
}

// ActivityChart implementation

ActivityChart::ActivityChart(QWidget *parent)
: RpWidget(parent) {
	resize(400, 200);
}

void ActivityChart::setData(const QVector<int> &data) {
	_data = data;
	_maxValue = 0;

	for (int val : _data) {
		if (val > _maxValue) {
			_maxValue = val;
		}
	}

	update();
}

void ActivityChart::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	if (_data.isEmpty() || _maxValue == 0) {
		return;
	}

	const int padding = 20;
	const int chartWidth = width() - 2 * padding;
	const int chartHeight = height() - 2 * padding;

	// Draw axes
	p.setPen(QPen(QColor(200, 200, 200), 1));
	p.drawLine(padding, height() - padding, width() - padding, height() - padding);  // X-axis
	p.drawLine(padding, padding, padding, height() - padding);  // Y-axis

	// Draw data points and lines
	p.setPen(QPen(QColor(70, 130, 255), 2));

	QPainterPath path;
	bool firstPoint = true;

	const double xStep = static_cast<double>(chartWidth) / (_data.size() - 1);

	for (int i = 0; i < _data.size(); ++i) {
		double x = padding + i * xStep;
		double y = height() - padding - (static_cast<double>(_data[i]) / _maxValue) * chartHeight;

		if (firstPoint) {
			path.moveTo(x, y);
			firstPoint = false;
		} else {
			path.lineTo(x, y);
		}

		// Draw data point
		p.setBrush(QColor(70, 130, 255));
		p.drawEllipse(QPointF(x, y), 3, 3);
	}

	// Draw line
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);

	// Draw grid lines
	p.setPen(QPen(QColor(230, 230, 230), 1, Qt::DotLine));
	for (int i = 1; i <= 4; ++i) {
		int y = padding + (chartHeight * i) / 4;
		p.drawLine(padding, y, width() - padding, y);
	}
}

} // namespace BotStatistics
} // namespace Info
