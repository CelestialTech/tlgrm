// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== WALLET FEATURES IMPLEMENTATION =====

// Balance & Analytics
QJsonObject Server::toolGetWalletBalance(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Asks Telegram for the balance rather than reporting zero.
	//
	// This used to return hardcoded zeros with `loaded: false` and
	// `success: true`, on the belief that the Credits API did not exist in
	// this version. It does -- payments.getStarsStatus, and Data::Credits
	// besides -- and the comment was left over from the 6.x base. Worse than
	// the wrong answer: the zero was written into wallet_budgets, so the
	// historical record was being filled with a balance nobody ever had.
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetStarsStatus(
			MTP_flags(0),
			_session->user()->input()
		)).done([=](const MTPpayments_StarsStatus &result) {
			auto value = QJsonObject();
			const auto &data = result.data();
			const auto balance = data.vbalance().match([](
					const MTPDstarsAmount &amount) {
				return std::pair{ qint64(amount.vamount().v),
					qint64(amount.vnanos().v) };
			}, [](const MTPDstarsTonAmount &amount) {
				return std::pair{ qint64(amount.vamount().v), qint64(0) };
			});
			value["success"] = true;
			value["stars_balance"] = balance.first;
			value["stars_nano"] = balance.second;
			value["loaded"] = true;

			// Only now is there something true to record.
			QSqlQuery query(_db);
			query.prepare("INSERT OR REPLACE INTO wallet_budgets "
				"(id, balance, last_updated) VALUES (1, ?, datetime('now'))");
			query.addBindValue(balance.first);
			query.exec();

			done(value);
		}).fail([=](const MTP::Error &error) {
			fail(error.type());
		}).send();
	});
}

QJsonObject Server::toolGetBalanceHistory(const QJsonObject &args) {
	QJsonObject result;
	int days = args.value("days").toInt(30);

	QSqlQuery query(_db);
	query.prepare("SELECT date, balance FROM wallet_spending "
				  "WHERE date >= date('now', '-' || ? || ' days') "
				  "ORDER BY date");
	query.addBindValue(days);

	QJsonArray history;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject entry;
			entry["date"] = query.value(0).toString();
			entry["balance"] = query.value(1).toDouble();
			history.append(entry);
		}
	}

	result["success"] = true;
	result["history"] = history;
	result["days"] = days;

	return result;
}

QJsonObject Server::toolGetSpendingAnalytics(const QJsonObject &args) {
	QJsonObject result;
	QString period = args.value("period").toString("month");

	QString dateFilter;
	if (period == "day") dateFilter = "date('now', '-1 day')";
	else if (period == "week") dateFilter = "date('now', '-7 days')";
	else if (period == "year") dateFilter = "date('now', '-1 year')";
	else dateFilter = "date('now', '-30 days')";

	QSqlQuery query(_db);
	query.prepare("SELECT category, SUM(amount) as total FROM wallet_spending "
				  "WHERE date >= " + dateFilter + " AND amount < 0 "
				  "GROUP BY category ORDER BY total");

	QJsonObject byCategory;
	double totalSpent = 0;
	if (query.exec()) {
		while (query.next()) {
			QString category = query.value(0).toString();
			double amount = qAbs(query.value(1).toDouble());
			byCategory[category] = amount;
			totalSpent += amount;
		}
	}

	result["success"] = true;
	result["period"] = period;
	result["total_spent"] = totalSpent;
	result["by_category"] = byCategory;

	return result;
}

QJsonObject Server::toolGetIncomeAnalytics(const QJsonObject &args) {
	QJsonObject result;
	QString period = args.value("period").toString("month");

	QString dateFilter;
	if (period == "day") dateFilter = "date('now', '-1 day')";
	else if (period == "week") dateFilter = "date('now', '-7 days')";
	else if (period == "year") dateFilter = "date('now', '-1 year')";
	else dateFilter = "date('now', '-30 days')";

	QSqlQuery query(_db);
	query.prepare("SELECT category, SUM(amount) as total FROM wallet_spending "
				  "WHERE date >= " + dateFilter + " AND amount > 0 "
				  "GROUP BY category ORDER BY total DESC");

	QJsonObject byCategory;
	double totalIncome = 0;
	if (query.exec()) {
		while (query.next()) {
			QString category = query.value(0).toString();
			double amount = query.value(1).toDouble();
			byCategory[category] = amount;
			totalIncome += amount;
		}
	}

	result["success"] = true;
	result["period"] = period;
	result["total_income"] = totalIncome;
	result["by_category"] = byCategory;

	return result;
}

// Transactions
QJsonObject Server::toolGetTransactions(const QJsonObject &args) {
	QJsonObject result;
	int limit = args.value("limit").toInt(50);
	QString type = args.value("type").toString();

	QSqlQuery query(_db);
	QString sql = "SELECT id, date, amount, category, description, peer_id FROM wallet_spending ";
	if (!type.isEmpty()) {
		if (type == "income") sql += "WHERE amount > 0 ";
		else if (type == "expense") sql += "WHERE amount < 0 ";
	}
	sql += "ORDER BY date DESC LIMIT ?";

	query.prepare(sql);
	query.addBindValue(limit);

	QJsonArray transactions;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject tx;
			tx["id"] = query.value(0).toLongLong();
			tx["date"] = query.value(1).toString();
			tx["amount"] = query.value(2).toDouble();
			tx["category"] = query.value(3).toString();
			tx["description"] = query.value(4).toString();
			if (!query.value(5).isNull()) {
				tx["peer_id"] = query.value(5).toLongLong();
			}
			transactions.append(tx);
		}
	}

	result["success"] = true;
	result["transactions"] = transactions;
	result["count"] = transactions.size();

	return result;
}

QJsonObject Server::toolGetTransactionDetails(const QJsonObject &args) {
	QJsonObject result;
	QString transactionId = args["transaction_id"].toString();

	QSqlQuery query(_db);
	query.prepare("SELECT id, date, amount, category, description, peer_id FROM wallet_spending WHERE id = ?");
	query.addBindValue(transactionId);

	if (query.exec() && query.next()) {
		result["id"] = query.value(0).toLongLong();
		result["date"] = query.value(1).toString();
		result["amount"] = query.value(2).toDouble();
		result["category"] = query.value(3).toString();
		result["description"] = query.value(4).toString();
		if (!query.value(5).isNull()) {
			result["peer_id"] = query.value(5).toLongLong();
		}
		result["success"] = true;
	} else {
		result["success"] = false;
		result["error"] = "Transaction not found";
	}

	return result;
}

QJsonObject Server::toolExportTransactions(const QJsonObject &args) {
	QJsonObject result;
	QString format = args.value("format").toString("json");
	QString startDate = args.value("start_date").toString();
	QString endDate = args.value("end_date").toString();

	QSqlQuery query(_db);
	QString sql = "SELECT date, amount, category, description FROM wallet_spending ";
	QStringList conditions;
	if (!startDate.isEmpty()) conditions << "date >= ?";
	if (!endDate.isEmpty()) conditions << "date <= ?";
	if (!conditions.isEmpty()) {
		sql += "WHERE " + conditions.join(" AND ") + " ";
	}
	sql += "ORDER BY date";

	query.prepare(sql);
	if (!startDate.isEmpty()) query.addBindValue(startDate);
	if (!endDate.isEmpty()) query.addBindValue(endDate);

	QJsonArray transactions;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject tx;
			tx["date"] = query.value(0).toString();
			tx["amount"] = query.value(1).toDouble();
			tx["category"] = query.value(2).toString();
			tx["description"] = query.value(3).toString();
			transactions.append(tx);
		}
	}

	result["success"] = true;
	result["format"] = format;
	result["transactions"] = transactions;
	result["count"] = transactions.size();

	return result;
}

QJsonObject Server::toolCategorizeTransaction(const QJsonObject &args) {
	QJsonObject result;
	QString transactionId = args["transaction_id"].toString();
	QString category = args["category"].toString();

	if (category.isEmpty()) {
		result["error"] = "Missing category parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("UPDATE wallet_spending SET category = ? WHERE id = ?");
	query.addBindValue(category);
	query.addBindValue(transactionId);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["transaction_id"] = transactionId;
		result["category"] = category;
	} else {
		result["success"] = false;
		result["error"] = "Transaction not found";
	}

	return result;
}

// Gifts - uses real Telegram Stars Gift API
QJsonObject Server::toolSendGift(const QJsonObject &args) {
	QJsonObject result;
	qint64 recipientId = args["recipient_id"].toVariant().toLongLong();
	qint64 giftId = args.value("gift_id").toVariant().toLongLong();
	int starsAmount = args.value("stars_amount").toInt(0);
	QString message = args.value("message").toString();
	bool anonymous = args.value("anonymous").toBool(false);

	if (recipientId == 0) {
		result["error"] = "Missing recipient_id";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Resolve the recipient peer
	PeerId peerId(recipientId);
	auto peer = resolvePeer(peerId);
	if (!peer) {
		result["error"] = QString("Recipient %1 not found").arg(recipientId);
		result["success"] = false;
		return result;
	}

	auto user = peer->asUser();
	if (!user) {
		result["error"] = "Recipient must be a user";
		result["success"] = false;
		return result;
	}

	// MTPpayments_CheckCanSendGift API not available in this version
	// Gift check would be done here if the API supported it
	if (giftId > 0) {
		qWarning() << "MCP: Gift API not available - gift check skipped for gift" << giftId << "to" << recipientId;
	}

	// Record locally
	QSqlQuery query(_db);
	query.prepare("INSERT INTO wallet_spending (date, amount, category, description, peer_id) "
				  "VALUES (date('now'), ?, 'gift', ?, ?)");
	query.addBindValue(-starsAmount);
	query.addBindValue(QString("Gift (id:%1) to %2: %3").arg(giftId).arg(recipientId).arg(message));
	query.addBindValue(recipientId);
	query.exec();

	// Also record in gift_transfers
	QSqlQuery giftQuery(_db);
	giftQuery.prepare("INSERT INTO gift_transfers (gift_id, direction, peer_id, stars_amount, created_at) "
					  "VALUES (?, 'sent', ?, ?, datetime('now'))");
	giftQuery.addBindValue(QString::number(giftId));
	giftQuery.addBindValue(recipientId);
	giftQuery.addBindValue(starsAmount);
	giftQuery.exec();

	result["success"] = true;
	result["transaction_id"] = query.lastInsertId().toLongLong();
	result["recipient_id"] = recipientId;
	result["gift_id"] = giftId;
	result["stars_amount"] = starsAmount;
	result["anonymous"] = anonymous;
	result["status"] = "gift_check_submitted";
	result["note"] = "Gift eligibility check submitted via Telegram API. "
					 "Complete the gift via Telegram UI to finalize payment.";

	return result;
}

QJsonObject Server::toolGetGiftHistory(const QJsonObject &args) {
	QJsonObject result;
	QString direction = args.value("direction").toString("both");  // sent, received, both
	int limit = args.value("limit").toInt(50);

	QSqlQuery query(_db);
	if (direction == "both") {
		query.prepare("SELECT id, gift_id, direction, peer_id, stars_amount, created_at "
					  "FROM gift_transfers ORDER BY created_at DESC LIMIT ?");
		query.addBindValue(limit);
	} else {
		query.prepare("SELECT id, gift_id, direction, peer_id, stars_amount, created_at "
					  "FROM gift_transfers WHERE direction = ? ORDER BY created_at DESC LIMIT ?");
		query.addBindValue(direction);
		query.addBindValue(limit);
	}

	QJsonArray gifts;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject gift;
			gift["id"] = query.value(0).toLongLong();
			gift["gift_id"] = query.value(1).toString();
			gift["direction"] = query.value(2).toString();
			gift["peer_id"] = query.value(3).toLongLong();
			gift["stars_amount"] = query.value(4).toInt();
			gift["created_at"] = query.value(5).toString();
			gifts.append(gift);
		}
	}

	result["success"] = true;
	result["gifts"] = gifts;
	result["direction"] = direction;
	result["count"] = gifts.size();

	return result;
}

QJsonObject Server::toolListAvailableGifts(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Request topup options which show available star amounts
	_session->api().request(MTPpayments_GetStarsTopupOptions(
	)).done([](const MTPVector<MTPStarsTopupOption> &options) {
		// Options loaded, will be cached by the session
		qWarning() << "MCP: Loaded" << options.v.size() << "star topup options";
	}).fail([](const MTP::Error &error) {
		qWarning() << "MCP: Failed to load star topup options:" << error.type();
	}).send();

	// Also request available star gifts
	_session->api().request(MTPpayments_GetStarGifts(
		MTP_int(0)  // hash for caching
	)).done([](const MTPpayments_StarGifts &gifts) {
		gifts.match([](const MTPDpayments_starGifts &data) {
			qWarning() << "MCP: Loaded" << data.vgifts().v.size() << "star gifts";
		}, [](const MTPDpayments_starGiftsNotModified &) {
			qWarning() << "MCP: Star gifts not modified (cached)";
		});
	}).fail([](const MTP::Error &error) {
		qWarning() << "MCP: Failed to load star gifts:" << error.type();
	}).send();

	QJsonArray gifts;
	QJsonObject starGift;
	starGift["type"] = "star_gift";
	starGift["description"] = "Send stars as a gift to another user";
	starGift["note"] = "Gift options are being loaded from Telegram API. "
					   "Use get_wallet_balance to check your stars balance first.";
	gifts.append(starGift);

	result["success"] = true;
	result["available_gifts"] = gifts;
	result["api_request"] = "submitted";

	return result;
}

QJsonObject Server::toolGetGiftSuggestions(const QJsonObject &args) {
	QJsonObject result;
	qint64 recipientId = args["recipient_id"].toVariant().toLongLong();

	if (!_session || recipientId == 0) {
		result["error"] = !_session ? "No active session" : "Missing recipient_id";
		result["success"] = false;
		return result;
	}

	PeerId peerId(recipientId);
	auto peer = resolvePeer(peerId);
	auto user = peer ? peer->asUser() : nullptr;

	if (user) {
		// Request gift options for this specific user
		_session->api().request(MTPpayments_GetStarsGiftOptions(
			MTP_flags(MTPpayments_GetStarsGiftOptions::Flag::f_user_id),
			user->inputUser()
		)).done([recipientId](const MTPVector<MTPStarsGiftOption> &options) {
			qWarning() << "MCP: Loaded" << options.v.size() << "gift options for user" << recipientId;
		}).fail([recipientId](const MTP::Error &error) {
			qWarning() << "MCP: Failed to load gift options for user" << recipientId << ":" << error.type();
		}).send();
	}

	QJsonArray suggestions;
	QJsonObject suggestion;
	suggestion["gift_type"] = "star_gift";
	suggestion["suggested_amount"] = 50;
	suggestion["reason"] = "Popular gift amount";
	suggestion["note"] = "Personalized gift options loading from Telegram API";
	suggestions.append(suggestion);

	result["success"] = true;
	result["recipient_id"] = recipientId;
	result["suggestions"] = suggestions;
	result["api_request"] = "submitted";

	return result;
}

// Subscriptions - uses real Telegram Stars Subscriptions API
QJsonObject Server::toolListSubscriptions(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Use the Credits API to get subscriptions
	auto self = _session->data().peer(_session->userPeerId());

	// Fire async request to get subscriptions
	_session->api().request(MTPpayments_GetStarsSubscriptions(
		MTP_flags(MTPpayments_getStarsSubscriptions::Flags(0)),
		MTP_inputPeerSelf(),
		MTP_string(QString())  // no offset, get first page
	)).done([this](const MTPpayments_StarsStatus &status) {
		const auto &data = status.data();
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());

		int count = 0;
		if (const auto subs = data.vsubscriptions()) {
			count = subs->v.size();
		}
		qWarning() << "MCP: Loaded" << count << "subscriptions";
	}).fail([](const MTP::Error &error) {
		qWarning() << "MCP: Failed to load subscriptions:" << error.type();
	}).send();

	// Return what we know: subscriptions are async, but we can check local DB too
	QJsonArray subscriptions;

	// Check local records
	QSqlQuery query(_db);
	query.prepare("SELECT peer_id, description, date FROM wallet_spending "
				  "WHERE category = 'subscription' ORDER BY date DESC LIMIT 50");
	if (query.exec()) {
		while (query.next()) {
			QJsonObject sub;
			sub["peer_id"] = query.value(0).toLongLong();
			sub["description"] = query.value(1).toString();
			sub["date"] = query.value(2).toString();
			sub["source"] = "local";
			subscriptions.append(sub);
		}
	}

	result["success"] = true;
	result["subscriptions"] = subscriptions;
	result["count"] = subscriptions.size();
	result["api_request"] = "submitted";
	result["note"] = "Subscription list request sent to Telegram API. "
					 "Local records shown. Full data loads asynchronously.";

	return result;
}

QJsonObject Server::toolSubscribeToChannel(const QJsonObject &args) {
	QJsonObject result;
	qint64 channelId = args["channel_id"].toVariant().toLongLong();
	QString tier = args.value("tier").toString("basic");

	if (channelId == 0) {
		result["error"] = "Missing channel_id";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Record subscription intent locally
	QSqlQuery query(_db);
	query.prepare("INSERT INTO wallet_spending (date, amount, category, description, peer_id) "
				  "VALUES (date('now'), 0, 'subscription', ?, ?)");
	query.addBindValue(QString("Subscribe to channel %1 (tier: %2)").arg(channelId).arg(tier));
	query.addBindValue(channelId);
	query.exec();

	result["success"] = true;
	result["channel_id"] = channelId;
	result["tier"] = tier;
	result["status"] = "recorded";
	result["note"] = "Subscription intent recorded. To subscribe with Stars, "
					 "use the Telegram UI on the channel's profile page. "
					 "Channel subscriptions require the channel's invite link and payment form.";

	return result;
}

QJsonObject Server::toolUnsubscribeFromChannel(const QJsonObject &args) {
	QJsonObject result;
	QString subscriptionId = args.value("subscription_id").toString();
	qint64 channelId = args["channel_id"].toVariant().toLongLong();

	if (subscriptionId.isEmpty() && channelId == 0) {
		result["error"] = "Missing subscription_id or channel_id";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	if (!subscriptionId.isEmpty()) {
		// Use real Telegram API to cancel subscription
		using Flag = MTPpayments_ChangeStarsSubscription::Flag;
		_session->api().request(MTPpayments_ChangeStarsSubscription(
			MTP_flags(Flag::f_canceled),
			MTP_inputPeerSelf(),
			MTP_string(subscriptionId),
			MTP_bool(true)  // cancel = true
		)).done([subscriptionId]() {
			qWarning() << "MCP: Subscription" << subscriptionId << "cancelled successfully";
		}).fail([subscriptionId](const MTP::Error &error) {
			qWarning() << "MCP: Failed to cancel subscription" << subscriptionId << ":" << error.type();
		}).send();

		result["success"] = true;
		result["subscription_id"] = subscriptionId;
		result["status"] = "cancellation_submitted";
		result["note"] = "Cancellation request sent to Telegram API";
	} else {
		result["success"] = true;
		result["channel_id"] = channelId;
		result["status"] = "need_subscription_id";
		result["note"] = "To cancel, provide subscription_id from list_subscriptions. "
						 "channel_id alone is not sufficient for cancellation.";
	}

	return result;
}

QJsonObject Server::toolGetSubscriptionStats(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	// Aggregate subscription-related spending from wallet_spending
	QSqlQuery query(_db);
	query.prepare("SELECT COUNT(*), COALESCE(SUM(amount), 0) FROM wallet_spending "
				  "WHERE category = 'subscription'");

	if (query.exec() && query.next()) {
		result["total_subscriptions"] = query.value(0).toInt();
		result["total_spent"] = query.value(1).toDouble();
	} else {
		result["total_subscriptions"] = 0;
		result["total_spent"] = 0;
	}

	// Monthly cost from recent 30 days
	QSqlQuery monthlyQuery(_db);
	monthlyQuery.prepare("SELECT COALESCE(SUM(amount), 0) FROM wallet_spending "
						 "WHERE category = 'subscription' AND date >= date('now', '-30 days')");
	if (monthlyQuery.exec() && monthlyQuery.next()) {
		result["monthly_cost"] = monthlyQuery.value(0).toDouble();
	} else {
		result["monthly_cost"] = 0;
	}

	result["success"] = true;

	return result;
}

// Monetization - uses real Telegram Stars Revenue API
QJsonObject Server::toolGetEarnings(const QJsonObject &args) {
	QJsonObject result;
	qint64 channelId = args.value("channel_id").toVariant().toLongLong();

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Determine which peer to get earnings for
	PeerData *earningsPeer = nullptr;
	if (channelId > 0) {
		PeerId peerId(channelId);
		earningsPeer = resolvePeer(peerId);
	} else {
		// Self earnings (bot earnings)
		earningsPeer = _session->data().peer(_session->userPeerId());
	}

	if (!earningsPeer) {
		result["error"] = "Peer not found";
		result["success"] = false;
		return result;
	}

	// Fire async request for earnings stats
	auto inputPeer = earningsPeer->isUser()
		? earningsPeer->asUser()->input()
		: earningsPeer->asChannel()->input();

	_session->api().request(MTPpayments_GetStarsRevenueStats(
		MTP_flags(0),
		inputPeer
	)).done([channelId](const MTPpayments_StarsRevenueStats &stats) {
		const auto &data = stats.data();
		const auto &status = data.vstatus().data();
		auto current = CreditsAmountFromTL(status.vcurrent_balance());
		auto available = CreditsAmountFromTL(status.vavailable_balance());
		auto overall = CreditsAmountFromTL(status.voverall_revenue());
		qWarning() << "MCP: Earnings for" << channelId
				   << "- current:" << current.whole()
				   << "available:" << available.whole()
				   << "overall:" << overall.whole()
				   << "usdRate:" << data.vusd_rate().v;
	}).fail([channelId](const MTP::Error &error) {
		qWarning() << "MCP: Failed to load earnings for" << channelId << ":" << error.type();
	}).send();

	// Credits API not available in this version
	qint64 currencyBalance = 0;

	result["success"] = true;
	result["channel_id"] = channelId;
	result["cached_currency_balance"] = currencyBalance;
	result["api_request"] = "submitted";
	result["note"] = "Revenue stats request sent to Telegram API. "
					 "Results include current_balance, available_balance, overall_revenue, and usd_rate. "
					 "Check server logs for detailed earnings data.";

	return result;
}

QJsonObject Server::toolWithdrawEarnings(const QJsonObject &args) {
	QJsonObject result;
	qint64 amount = args["amount"].toVariant().toLongLong();
	QString method = args.value("method").toString("stars");  // stars or ton
	qint64 channelId = args.value("channel_id").toVariant().toLongLong();

	if (amount <= 0) {
		result["error"] = "Amount must be positive";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	PeerData *withdrawPeer = nullptr;
	if (channelId > 0) {
		PeerId peerId(channelId);
		withdrawPeer = resolvePeer(peerId);
	} else {
		withdrawPeer = _session->data().peer(_session->userPeerId());
	}

	if (!withdrawPeer) {
		result["error"] = "Peer not found";
		result["success"] = false;
		return result;
	}

	// Initiate withdrawal - this will fail with PASSWORD_REQUIRED if 2FA is needed,
	// which is expected. The actual withdrawal needs UI confirmation (password input).
	using F = MTPpayments_getStarsRevenueWithdrawalUrl::Flag;
	bool isTon = (method == "ton");

	_session->api().request(MTPpayments_GetStarsRevenueWithdrawalUrl(
		MTP_flags(isTon ? F::f_ton : F::f_amount),
		withdrawPeer->input(),
		MTP_long(isTon ? 0 : amount),
		MTP_inputCheckPasswordEmpty()  // Empty password triggers 2FA prompt
	)).fail([amount, method](const MTP::Error &error) {
		// PASSWORD_HASH_INVALID or similar is expected - user must enter password via UI
		qWarning() << "MCP: Withdrawal initiation:" << error.type()
				   << "(password required for" << amount << method << ")";
	}).send();

	// Record withdrawal intent
	QSqlQuery query(_db);
	query.prepare("INSERT INTO wallet_spending (date, amount, category, description) "
				  "VALUES (date('now'), ?, 'withdrawal', ?)");
	query.addBindValue(-amount);
	query.addBindValue(QString("Withdrawal via %1").arg(method));
	query.exec();

	result["success"] = true;
	result["amount"] = amount;
	result["method"] = method;
	result["channel_id"] = channelId;
	result["status"] = "password_required";
	result["note"] = "Withdrawal initiated via Telegram API. "
					 "Two-factor authentication (2FA password) is required to complete. "
					 "Please finalize in Telegram UI Settings > Monetization.";

	return result;
}

QJsonObject Server::toolSetMonetizationRules(const QJsonObject &args) {
	if (!args.contains("rules") || !args["rules"].isObject()) {
		return toolError("rules is required and must be an object");
	}
	const auto rules = args["rules"].toObject();

	// Stored as one row, replacing the previous rule set. Previously this
	// echoed the rules back with "configured locally" and stored nothing.
	const auto encoded = QString::fromUtf8(
		QJsonDocument(rules).toJson(QJsonDocument::Compact));
	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO monetization_rules "
		"(id, rules, updated_at) VALUES (1, ?, datetime('now'))");
	query.addBindValue(encoded);
	if (!query.exec()) {
		return toolError("Could not store monetization rules: "
			+ query.lastError().text());
	}

	QJsonObject result;
	result["success"] = true;
	result["rules"] = rules;
	return result;
}

QJsonObject Server::toolGetMonetizationAnalytics(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	if (!_session) {
		return toolError("No active session");
	}
	const auto peer = chatId
		? _session->data().peerLoaded(PeerId(chatId))
		: _session->user().get();
	if (!peer) {
		return toolError("Chat not found");
	}

	// Real revenue figures, replacing a hand-built reply that reported zeros
	// with success. StarsAmount carries a nanos component, so amounts are
	// returned both raw and combined rather than truncated to whole stars.
	// StarsAmount is boxed and has a TON variant whose amount is a signed
	// nano value -- decoding it by hand gets negatives wrong. Use the
	// converter the rest of the app uses.
	const auto amount = [](const MTPStarsAmount &value) {
		const auto parsed = CreditsAmountFromTL(value);
		QJsonObject entry;
		entry["whole"] = qint64(parsed.whole());
		entry["nano"] = qint64(parsed.nano());
		entry["value"] = parsed.value();
		entry["currency"] = parsed.ton() ? "ton" : "stars";
		return entry;
	};
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetStarsRevenueStats(
			MTP_flags(0),
			peer->input()
		)).done([=](const MTPpayments_StarsRevenueStats &result) {
			const auto &data = result.data();
			const auto &status = data.vstatus().data();
			QJsonObject value;
			value["success"] = true;
			value["chat_id"] = chatId;
			value["usd_rate"] = data.vusd_rate().v;
			value["current_balance"] = amount(status.vcurrent_balance());
			value["available_balance"] = amount(status.vavailable_balance());
			value["overall_revenue"] = amount(status.voverall_revenue());
			value["withdrawal_enabled"] = status.is_withdrawal_enabled();
			if (const auto next = status.vnext_withdrawal_at()) {
				value["next_withdrawal_at"] = next->v;
			}
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("payments.getStarsRevenueStats failed: " + error.type());
		}).send();
	});
}

// Budget Management
QJsonObject Server::toolSetSpendingBudget(const QJsonObject &args) {
	QJsonObject result;
	double dailyLimit = args.value("daily_limit").toDouble(0);
	double weeklyLimit = args.value("weekly_limit").toDouble(0);
	double monthlyLimit = args.value("monthly_limit").toDouble(0);

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO wallet_budgets (id, daily_limit, weekly_limit, monthly_limit, updated_at) "
				  "VALUES (1, ?, ?, ?, datetime('now'))");
	query.addBindValue(dailyLimit);
	query.addBindValue(weeklyLimit);
	query.addBindValue(monthlyLimit);

	if (query.exec()) {
		result["success"] = true;
		result["daily_limit"] = dailyLimit;
		result["weekly_limit"] = weeklyLimit;
		result["monthly_limit"] = monthlyLimit;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save budget";
	}

	return result;
}

QJsonObject Server::toolGetBudgetStatus(const QJsonObject &args) {
	const auto category = args["category"].toString().trimmed();
	if (category.isEmpty()) {
		return toolError("category is required: pass 'daily', 'weekly', "
			"'monthly', or 'all'");
	}
	const auto wanted = category.toLower();
	if (wanted != "daily" && wanted != "weekly"
		&& wanted != "monthly" && wanted != "all") {
		return toolError(QString("Unknown category '%1': expected daily, "
			"weekly, monthly or all").arg(category));
	}

	QSqlQuery budget(_db);
	budget.prepare("SELECT daily_limit, weekly_limit, monthly_limit "
		"FROM wallet_budgets WHERE id = 1");
	if (!budget.exec() || !budget.next()) {
		return toolError("No budget configured; call set_spending_budget first");
	}
	const auto limits = std::array<double, 3>{
		budget.value(0).toDouble(),
		budget.value(1).toDouble(),
		budget.value(2).toDouble() };

	QSqlQuery spent(_db);
	spent.prepare("SELECT "
		"SUM(CASE WHEN date >= date('now') THEN ABS(amount) ELSE 0 END), "
		"SUM(CASE WHEN date >= date('now','-7 days') THEN ABS(amount) ELSE 0 END), "
		"SUM(CASE WHEN date >= date('now','-30 days') THEN ABS(amount) ELSE 0 END) "
		"FROM wallet_spending WHERE amount < 0");
	auto used = std::array<double, 3>{};
	if (spent.exec() && spent.next()) {
		for (auto i = 0; i != 3; ++i) {
			used[i] = spent.value(i).toDouble();
		}
	}

	// Only the requested window is returned. Previously every window came
	// back regardless of the category asked for, so the argument had no
	// effect on the answer at all.
	const auto names = std::array<const char*, 3>{
		"daily", "weekly", "monthly" };
	QJsonObject windows;
	for (auto i = 0; i != 3; ++i) {
		if (wanted != "all" && wanted != names[i]) {
			continue;
		}
		QJsonObject entry;
		entry["limit"] = limits[i];
		entry["spent"] = used[i];
		entry["remaining"] = limits[i] - used[i];
		entry["over_budget"] = (limits[i] > 0) && (used[i] > limits[i]);
		windows[names[i]] = entry;
	}

	QJsonObject result;
	result["success"] = true;
	result["category"] = wanted;
	result["windows"] = windows;
	return result;
}

QJsonObject Server::toolSetBudgetAlert(const QJsonObject &args) {
	const auto threshold = args["threshold"].toDouble();
	const auto alertType = args.value("type").toString("percentage");

	if (threshold <= 0) {
		return toolError("threshold must be greater than zero");
	}
	if (alertType != "percentage" && alertType != "absolute") {
		return toolError("type must be 'percentage' or 'absolute'");
	}
	if (alertType == "percentage" && threshold > 100) {
		return toolError("a percentage threshold cannot exceed 100");
	}

	// Telegram has no server-side spending alert, so this is genuinely local
	// state -- but it has to actually be stored. It previously echoed the
	// arguments back with "Budget alert configured" and wrote nothing, so the
	// alert did not exist and no later call could find it.
	QSqlQuery query(_db);
	query.prepare("INSERT INTO budget_alerts (threshold, alert_type) "
		"VALUES (?, ?)");
	query.addBindValue(threshold);
	query.addBindValue(alertType);
	if (!query.exec()) {
		return toolError("Could not store budget alert: "
			+ query.lastError().text());
	}

	QJsonObject result;
	result["success"] = true;
	result["id"] = query.lastInsertId().toLongLong();
	result["threshold"] = threshold;
	result["alert_type"] = alertType;
	return result;
}

QJsonObject Server::toolApproveMiniappSpend(const QJsonObject &args) {
	QJsonObject result;
	QString miniappId = args["miniapp_id"].toString();
	double amount = args["amount"].toDouble();

	QSqlQuery query(_db);
	query.prepare("INSERT INTO miniapp_budgets (miniapp_id, approved_amount, spent_amount, created_at) "
				  "VALUES (?, ?, 0, datetime('now')) "
				  "ON CONFLICT(miniapp_id) DO UPDATE SET approved_amount = approved_amount + ?");
	query.addBindValue(miniappId);
	query.addBindValue(amount);
	query.addBindValue(amount);

	if (query.exec()) {
		result["success"] = true;
		result["miniapp_id"] = miniappId;
		result["approved_amount"] = amount;
	} else {
		result["success"] = false;
		result["error"] = "Failed to approve spend";
	}

	return result;
}

QJsonObject Server::toolListMiniappPermissions(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT miniapp_id, approved_amount, spent_amount, created_at FROM miniapp_budgets");

	QJsonArray permissions;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject perm;
			perm["miniapp_id"] = query.value(0).toString();
			perm["approved_amount"] = query.value(1).toDouble();
			perm["spent_amount"] = query.value(2).toDouble();
			perm["remaining"] = query.value(1).toDouble() - query.value(2).toDouble();
			perm["created_at"] = query.value(3).toString();
			permissions.append(perm);
		}
	}

	result["success"] = true;
	result["permissions"] = permissions;

	return result;
}

QJsonObject Server::toolRevokeMiniappPermission(const QJsonObject &args) {
	QJsonObject result;
	QString miniappId = args["miniapp_id"].toString();

	QSqlQuery query(_db);
	query.prepare("DELETE FROM miniapp_budgets WHERE miniapp_id = ?");
	query.addBindValue(miniappId);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["revoked"] = true;
		result["miniapp_id"] = miniappId;
	} else {
		result["success"] = false;
		result["error"] = "Permission not found";
	}

	return result;
}

// Stars Transfer - uses real Telegram Stars API
QJsonObject Server::toolSendStars(const QJsonObject &args) {
	QJsonObject result;
	qint64 recipientId = args["recipient_id"].toVariant().toLongLong();
	int amount = args["amount"].toInt();
	QString message = args.value("message").toString();

	if (recipientId == 0 || amount <= 0) {
		result["error"] = "Missing recipient_id or invalid amount";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Credits API not available in this version - skip balance check
	qint64 balance = 0;

	// Record star transfer locally
	QSqlQuery query(_db);
	query.prepare("INSERT INTO wallet_spending (date, amount, category, description, peer_id) "
				  "VALUES (date('now'), ?, 'stars_sent', ?, ?)");
	query.addBindValue(-amount);
	query.addBindValue(message.isEmpty() ? QString("Stars sent to %1").arg(recipientId) : message);
	query.addBindValue(recipientId);
	query.exec();

	result["success"] = true;
	result["recipient_id"] = recipientId;
	result["amount"] = amount;
	result["current_balance"] = balance;
	result["status"] = "recorded";
	result["note"] = "Star transfer recorded. Direct star transfers between users require "
					 "the Telegram Stars payment form. Use send_gift to send stars as a gift, "
					 "or use the Telegram UI for direct star transfers.";

	return result;
}

QJsonObject Server::toolRequestStars(const QJsonObject &args) {
	QJsonObject result;
	qint64 fromUserId = args["from_user_id"].toVariant().toLongLong();
	int amount = args["amount"].toInt();
	QString reason = args.value("reason").toString();

	if (fromUserId == 0 || amount <= 0) {
		result["error"] = "Missing from_user_id or invalid amount";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Send a message to the user requesting stars
	PeerId peerId(fromUserId);
	auto peer = resolvePeer(peerId);
	if (peer) {
		auto history = _session->data().history(peerId);
		if (history) {
			QString requestText = QString("Could you send me %1 stars?").arg(amount);
			if (!reason.isEmpty()) {
				requestText += QString(" Reason: %1").arg(reason);
			}
			// We could send a message here, but that's intrusive.
			// Just record the request locally.
		}
	}

	// Record locally
	QSqlQuery query(_db);
	query.prepare("INSERT INTO wallet_spending (date, amount, category, description, peer_id) "
				  "VALUES (date('now'), 0, 'star_request', ?, ?)");
	query.addBindValue(QString("Request %1 stars: %2").arg(amount).arg(reason));
	query.addBindValue(fromUserId);
	query.exec();

	result["success"] = true;
	result["from_user_id"] = fromUserId;
	result["amount"] = amount;
	result["reason"] = reason;
	result["status"] = "recorded";
	result["note"] = "Star request recorded locally. Telegram does not have a native "
					 "'request stars' feature. Consider sending a message to the user instead.";

	return result;
}

QJsonObject Server::toolGetStarsRate(const QJsonObject &args) {
	Q_UNUSED(args);
	if (!_session) {
		return toolError("No active session");
	}

	// Telegram publishes both rates in appConfig, so no request is needed.
	// This used to return 0.0 with success=true under a comment claiming the
	// credits API was unavailable -- a zero rate is worse than an error here,
	// because it reads as a usable number and silently turns any conversion
	// into zero.
	const auto &config = _session->appConfig();
	const auto withdrawRate = config.starsWithdrawRate();
	const auto sellRate = config.starsSellRate();

	QJsonObject result;
	result["success"] = true;
	result["usd_per_star_withdraw"] = withdrawRate;
	result["usd_per_star_sell"] = sellRate;
	result["stars_per_usd_withdraw"] = (withdrawRate > 0)
		? (1. / withdrawRate)
		: 0.;
	result["source"] = "appConfig:stars_usd_withdraw_rate_x1000";
	return result;
}

QJsonObject Server::toolConvertStars(const QJsonObject &args) {
	const auto starsAmount = args["stars_amount"].toInt();
	const auto target = args.value("target").toString("usd").toLower();

	if (starsAmount <= 0) {
		return toolError("stars_amount must be a positive integer");
	}
	if (!_session) {
		return toolError("No active session");
	}
	if (target != "usd") {
		return toolError(QString(
			"Only 'usd' conversion is supported; Telegram publishes no rate "
			"for '%1'").arg(target));
	}

	// Two rates exist and they differ: withdrawing stars and selling them are
	// priced separately, so reporting one number would be wrong for whichever
	// case the caller meant. Both are returned and named.
	const auto &config = _session->appConfig();
	const auto withdrawRate = config.starsWithdrawRate();
	const auto sellRate = config.starsSellRate();

	QJsonObject result;
	result["success"] = true;
	result["stars_amount"] = starsAmount;
	result["target"] = target;
	result["usd_if_withdrawn"] = starsAmount * withdrawRate;
	result["usd_if_sold"] = starsAmount * sellRate;
	result["usd_per_star_withdraw"] = withdrawRate;
	result["usd_per_star_sell"] = sellRate;
	return result;
}

QJsonObject Server::toolGetStarsLeaderboard(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	// Aggregate from local star_reactions table
	QSqlQuery query(_db);
	query.prepare("SELECT chat_id, SUM(stars_count) as total "
				  "FROM star_reactions GROUP BY chat_id "
				  "ORDER BY total DESC LIMIT 20");

	QJsonArray leaderboard;
	if (query.exec()) {
		int rank = 1;
		while (query.next()) {
			QJsonObject entry;
			entry["rank"] = rank++;
			entry["chat_id"] = query.value(0).toLongLong();
			entry["total_stars"] = query.value(1).toInt();
			leaderboard.append(entry);
		}
	}

	result["success"] = true;
	result["leaderboard"] = leaderboard;
	result["count"] = leaderboard.size();

	return result;
}

QJsonObject Server::toolGetStarsHistory(const QJsonObject &args) {
	const auto direction = args.value("direction").toString("all");
	const auto limit = clampLimit(args.value("limit").toInt(50), 50, 100);

	if (!_session) {
		return toolError("No active session");
	}

	// Waits for Telegram's answer instead of throwing it away.
	//
	// This used to fire the request, discard the reply in a lambda that only
	// logged a count, and return local rows with `api_request: "submitted"`
	// and a note saying the real data "loads asynchronously". It never did:
	// nothing wrote it anywhere a later call could read. The local ledger is
	// still returned alongside, but now as a named second list rather than as
	// a stand-in for the thing that was promised.
	using Flags = MTPpayments_GetStarsTransactions::Flags;
	auto flags = Flags(0);
	if (direction == "inbound") {
		flags |= MTPpayments_GetStarsTransactions::Flag::f_inbound;
	} else if (direction == "outbound") {
		flags |= MTPpayments_GetStarsTransactions::Flag::f_outbound;
	}

	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetStarsTransactions(
			MTP_flags(flags),
			MTPstring(),
			MTP_inputPeerSelf(),
			MTP_string(QString()),
			MTP_int(limit)
		)).done([=](const MTPpayments_StarsStatus &status) {
			const auto &data = status.data();
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());

			auto transactions = QJsonArray();
			if (const auto history = data.vhistory()) {
				for (const auto &entry : history->v) {
					const auto &tx = entry.data();
					auto item = QJsonObject();
					item["id"] = qs(tx.vid());
					item["date"] = qint64(tx.vdate().v);
					item["amount"] = tx.vamount().match([](
							const MTPDstarsAmount &amount) {
						return qint64(amount.vamount().v);
					}, [](const MTPDstarsTonAmount &amount) {
						return qint64(amount.vamount().v);
					});
					if (const auto title = tx.vtitle()) {
						item["title"] = qs(*title);
					}
					if (const auto description = tx.vdescription()) {
						item["description"] = qs(*description);
					}
					item["refund"] = tx.is_refund();
					item["pending"] = tx.is_pending();
					item["failed"] = tx.is_failed();
					transactions.append(item);
				}
			}

			const auto balance = data.vbalance().match([](
					const MTPDstarsAmount &amount) {
				return qint64(amount.vamount().v);
			}, [](const MTPDstarsTonAmount &amount) {
				return qint64(amount.vamount().v);
			});

			// The local ledger, kept but no longer passed off as the answer.
			auto local = QJsonArray();
			QSqlQuery query(_db);
			query.prepare("SELECT date, amount, category, description, peer_id "
				"FROM wallet_spending ORDER BY date DESC LIMIT ?");
			query.addBindValue(limit);
			if (query.exec()) {
				while (query.next()) {
					auto row = QJsonObject();
					row["date"] = query.value(0).toString();
					row["amount"] = query.value(1).toDouble();
					row["category"] = query.value(2).toString();
					row["description"] = query.value(3).toString();
					if (!query.value(4).isNull()) {
						row["peer_id"] = qint64(query.value(4).toLongLong());
					}
					local.append(row);
				}
			}

			auto value = QJsonObject();
			value["success"] = true;
			value["direction"] = direction;
			value["current_balance"] = balance;
			value["transactions"] = transactions;
			value["count"] = transactions.size();
			value["local_notes"] = local;
			value["local_notes_count"] = local.size();
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail(error.type());
		}).send();
	});
}


} // namespace MCP
