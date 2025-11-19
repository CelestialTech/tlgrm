// Bot Statistics Widget
// This file is part of Telegram Desktop MCP integration.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include "ui/rp_widget.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace MCP {
class BotManager;
struct BotStats;
} // namespace MCP

namespace Window {
class SessionController;
} // namespace Window

namespace Info {
namespace BotStatistics {

class Widget : public Ui::RpWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		MCP::BotManager *botManager);

private:
	void setupContent();
	void setupSystemOverview();
	void setupBotPerformance();
	void setupActivityChart();
	void setupActions();

	void refreshStats();
	void exportData();
	void resetStats();

	not_null<Window::SessionController*> _controller;
	MCP::BotManager *_botManager;

	// UI elements
	Ui::FlatLabel *_totalBotsLabel = nullptr;
	Ui::FlatLabel *_runningBotsLabel = nullptr;
	Ui::FlatLabel *_uptimeLabel = nullptr;

	Ui::VerticalLayout *_performanceContainer = nullptr;
	Ui::RpWidget *_chartWidget = nullptr;

	rpl::lifetime _lifetime;
};

// Simple chart widget for activity visualization
class ActivityChart : public Ui::RpWidget {
public:
	ActivityChart(QWidget *parent);

	void setData(const QVector<int> &data);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QVector<int> _data;
	int _maxValue = 0;
};

} // namespace BotStatistics
} // namespace Info
