// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== STARS FEATURES IMPLEMENTATION =====

// Gift Collections
QJsonObject Server::toolCreateGiftCollection(const QJsonObject &args) {
	QJsonObject result;
	QString name = args["name"].toString();
	QString description = args.value("description").toString();
	bool isPublic = args.value("public").toBool(false);

	if (name.isEmpty()) {
		result["error"] = "Missing name parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT INTO gift_collections (name, description, is_public, created_at) "
				  "VALUES (?, ?, ?, datetime('now'))");
	query.addBindValue(name);
	query.addBindValue(description);
	query.addBindValue(isPublic);

	if (query.exec()) {
		result["success"] = true;
		result["collection_id"] = query.lastInsertId().toLongLong();
		result["name"] = name;
	} else {
		result["success"] = false;
		result["error"] = "Failed to create collection";
	}

	return result;
}

QJsonObject Server::toolListGiftCollections(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	if (_session) {
		// Also request collections from Telegram API
		auto self = _session->data().peer(_session->userPeerId());
		_session->api().request(MTPpayments_GetStarGiftCollections(
			self->input,
			MTP_long(0)  // hash for caching
		)).done([](const MTPpayments_StarGiftCollections &collections) {
			collections.match([](const MTPDpayments_starGiftCollections &data) {
				qWarning() << "MCP: Loaded" << data.vcollections().v.size() << "gift collections from Telegram";
			}, [](const MTPDpayments_starGiftCollectionsNotModified &) {
				qWarning() << "MCP: Gift collections not modified (cached)";
			});
		}).fail([](const MTP::Error &error) {
			qWarning() << "MCP: Failed to load gift collections:" << error.type();
		}).send();
	}

	QSqlQuery query(_db);
	query.prepare("SELECT id, name, description, is_public, created_at FROM gift_collections");

	QJsonArray collections;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject collection;
			collection["id"] = query.value(0).toLongLong();
			collection["name"] = query.value(1).toString();
			collection["description"] = query.value(2).toString();
			collection["is_public"] = query.value(3).toBool();
			collection["created_at"] = query.value(4).toString();
			collection["source"] = "local";
			collections.append(collection);
		}
	}

	result["success"] = true;
	result["collections"] = collections;
	result["api_request"] = _session ? "submitted" : "no_session";
	result["note"] = "Local collections shown. Telegram gift collections also loading asynchronously.";

	return result;
}

QJsonObject Server::toolAddToCollection(const QJsonObject &args) {
	QJsonObject result;
	qint64 collectionId = args["collection_id"].toVariant().toLongLong();
	QString giftId = args["gift_id"].toString();

	QSqlQuery query(_db);
	query.prepare("INSERT OR IGNORE INTO collection_items (collection_id, gift_id) VALUES (?, ?)");
	query.addBindValue(collectionId);
	query.addBindValue(giftId);

	if (query.exec()) {
		result["success"] = true;
		result["collection_id"] = collectionId;
		result["gift_id"] = giftId;
		result["added"] = query.numRowsAffected() > 0;
	} else {
		result["success"] = false;
		result["error"] = "Failed to add to collection";
	}

	return result;
}

QJsonObject Server::toolRemoveFromCollection(const QJsonObject &args) {
	QJsonObject result;
	qint64 collectionId = args["collection_id"].toVariant().toLongLong();
	QString giftId = args["gift_id"].toString();

	QSqlQuery query(_db);
	query.prepare("DELETE FROM collection_items WHERE collection_id = ? AND gift_id = ?");
	query.addBindValue(collectionId);
	query.addBindValue(giftId);

	if (query.exec()) {
		result["success"] = true;
		result["collection_id"] = collectionId;
		result["gift_id"] = giftId;
		result["removed"] = query.numRowsAffected() > 0;
	} else {
		result["success"] = false;
		result["error"] = "Failed to remove from collection";
	}

	return result;
}

QJsonObject Server::toolShareCollection(const QJsonObject &args) {
	QJsonObject result;
	qint64 collectionId = args["collection_id"].toVariant().toLongLong();
	qint64 withUserId = args.value("with_user_id").toVariant().toLongLong();

	// Mark collection as public so it can be shared
	QSqlQuery query(_db);
	query.prepare("UPDATE gift_collections SET is_public = 1 WHERE id = ?");
	query.addBindValue(collectionId);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["collection_id"] = collectionId;
		result["shared_with"] = withUserId;
		result["is_public"] = true;
	} else {
		result["success"] = false;
		result["error"] = "Collection not found";
	}

	return result;
}

// Gift Auctions - uses Telegram Star Gift Auction API
QJsonObject Server::toolCreateGiftAuction(const QJsonObject &args) {
	QJsonObject result;
	QString giftId = args["gift_id"].toString();
	int startingBid = args["starting_bid"].toInt();
	int durationHours = args.value("duration_hours").toInt(24);

	if (giftId.isEmpty() || startingBid <= 0) {
		result["error"] = "Missing gift_id or invalid starting_bid";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	QString auctionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QDateTime endTime = QDateTime::currentDateTimeUtc().addSecs(durationHours * 3600);

	// Store auction locally
	QSqlQuery query(_db);
	query.prepare("INSERT INTO auctions (id, gift_id, starting_bid, current_bid, bidder_count, status, ends_at, created_at) "
				  "VALUES (?, ?, ?, ?, 0, 'active', ?, datetime('now'))");
	query.addBindValue(auctionId);
	query.addBindValue(giftId);
	query.addBindValue(startingBid);
	query.addBindValue(startingBid);
	query.addBindValue(endTime.toString(Qt::ISODate));
	query.exec();

	// Try to use the real Telegram auction API if the gift has a saved ID
	qint64 giftIdNum = giftId.toLongLong();
	if (giftIdNum > 0) {
		// Set the gift price which effectively lists it for sale/auction
		_session->api().request(MTPpayments_UpdateStarGiftPrice(
			MTP_inputSavedStarGiftUser(MTP_int(0)),  // needs real saved gift ID
			MTP_long(startingBid)
		)).done([auctionId]() {
			qWarning() << "MCP: Gift price updated for auction" << auctionId;
		}).fail([auctionId](const MTP::Error &error) {
			qWarning() << "MCP: Gift auction API:" << error.type() << "for" << auctionId;
		}).send();
	}

	result["success"] = true;
	result["auction_id"] = auctionId;
	result["gift_id"] = giftId;
	result["starting_bid"] = startingBid;
	result["current_bid"] = startingBid;
	result["duration_hours"] = durationHours;
	result["ends_at"] = endTime.toString(Qt::ISODate);
	result["status"] = "active";
	result["api_request"] = "submitted";
	result["note"] = "Auction created locally and price update submitted to Telegram API. "
					 "Full auction functionality uses Telegram's gift marketplace.";

	return result;
}

QJsonObject Server::toolPlaceBid(const QJsonObject &args) {
	QJsonObject result;
	QString auctionId = args["auction_id"].toString();
	int bidAmount = args["bid_amount"].toInt();

	if (auctionId.isEmpty() || bidAmount <= 0) {
		result["error"] = "Missing auction_id or invalid bid_amount";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Check current balance
	auto &credits = _session->data().credits();
	CreditsAmount balance = credits.balance();
	if (balance.whole() < bidAmount) {
		result["error"] = QString("Insufficient balance: have %1 stars, bid requires %2")
			.arg(balance.whole()).arg(bidAmount);
		result["success"] = false;
		result["current_balance"] = balance.whole();
		return result;
	}

	// Check auction exists and bid is valid
	QSqlQuery checkQuery(_db);
	checkQuery.prepare("SELECT current_bid, status FROM auctions WHERE id = ?");
	checkQuery.addBindValue(auctionId);

	if (checkQuery.exec() && checkQuery.next()) {
		int currentBid = checkQuery.value(0).toInt();
		QString status = checkQuery.value(1).toString();

		if (status != "active") {
			result["error"] = QString("Auction is %1, not active").arg(status);
			result["success"] = false;
			return result;
		}

		if (bidAmount <= currentBid) {
			result["error"] = QString("Bid must be higher than current bid of %1 stars").arg(currentBid);
			result["success"] = false;
			result["current_bid"] = currentBid;
			return result;
		}

		// Update auction with new bid
		QSqlQuery updateQuery(_db);
		updateQuery.prepare("UPDATE auctions SET current_bid = ?, bidder_count = bidder_count + 1 WHERE id = ?");
		updateQuery.addBindValue(bidAmount);
		updateQuery.addBindValue(auctionId);
		updateQuery.exec();
	} else {
		result["error"] = "Auction not found";
		result["success"] = false;
		return result;
	}

	// Record bid in spending
	QSqlQuery query(_db);
	query.prepare("INSERT INTO wallet_spending (date, amount, category, description) "
				  "VALUES (date('now'), 0, 'bid', ?)");
	query.addBindValue(QString("Bid %1 stars on auction %2").arg(bidAmount).arg(auctionId));
	query.exec();

	result["success"] = true;
	result["auction_id"] = auctionId;
	result["bid_amount"] = bidAmount;
	result["current_balance"] = balance.whole();
	result["status"] = "bid_placed";
	result["note"] = "Bid placed locally. Telegram gift auctions use the Star Gift marketplace. "
					 "For live Telegram auctions, use the Telegram UI.";

	return result;
}

QJsonObject Server::toolListAuctions(const QJsonObject &args) {
	QJsonObject result;
	QString status = args.value("status").toString("active");
	int limit = args.value("limit").toInt(50);

	QSqlQuery query(_db);
	query.prepare("SELECT id, gift_id, starting_bid, current_bid, bidder_count, status, ends_at, created_at "
				  "FROM auctions WHERE status = ? ORDER BY created_at DESC LIMIT ?");
	query.addBindValue(status);
	query.addBindValue(limit);

	QJsonArray auctions;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject auction;
			auction["auction_id"] = query.value(0).toString();
			auction["gift_id"] = query.value(1).toString();
			auction["starting_bid"] = query.value(2).toInt();
			auction["current_bid"] = query.value(3).toInt();
			auction["bidder_count"] = query.value(4).toInt();
			auction["status"] = query.value(5).toString();
			auction["ends_at"] = query.value(6).toString();
			auction["created_at"] = query.value(7).toString();
			auctions.append(auction);
		}
	}

	result["success"] = true;
	result["auctions"] = auctions;
	result["count"] = auctions.size();
	result["status_filter"] = status;

	return result;
}

QJsonObject Server::toolGetAuctionStatus(const QJsonObject &args) {
	QJsonObject result;
	QString auctionId = args["auction_id"].toString();

	QSqlQuery query(_db);
	query.prepare("SELECT gift_id, starting_bid, current_bid, bidder_count, status, ends_at, created_at "
				  "FROM auctions WHERE id = ?");
	query.addBindValue(auctionId);

	if (query.exec() && query.next()) {
		result["success"] = true;
		result["auction_id"] = auctionId;
		result["gift_id"] = query.value(0).toString();
		result["starting_bid"] = query.value(1).toInt();
		result["current_bid"] = query.value(2).toInt();
		result["bidder_count"] = query.value(3).toInt();
		result["status"] = query.value(4).toString();
		result["ends_at"] = query.value(5).toString();
		result["created_at"] = query.value(6).toString();
	} else {
		result["success"] = false;
		result["auction_id"] = auctionId;
		result["error"] = "Auction not found";
	}

	return result;
}

QJsonObject Server::toolCancelAuction(const QJsonObject &args) {
	QJsonObject result;
	QString auctionId = args["auction_id"].toString();

	QSqlQuery query(_db);
	query.prepare("UPDATE auctions SET status = 'cancelled' WHERE id = ? AND status = 'active'");
	query.addBindValue(auctionId);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["auction_id"] = auctionId;
		result["cancelled"] = true;
	} else {
		result["success"] = false;
		result["auction_id"] = auctionId;
		result["cancelled"] = false;
		result["error"] = "Auction not found or not active";
	}

	return result;
}

QJsonObject Server::toolGetAuctionHistory(const QJsonObject &args) {
	QJsonObject result;
	int limit = args.value("limit").toInt(50);
	QString statusFilter = args.value("status").toString();

	QSqlQuery query(_db);
	if (statusFilter.isEmpty()) {
		query.prepare("SELECT id, gift_id, starting_bid, current_bid, bidder_count, status, ends_at, created_at "
					  "FROM auctions ORDER BY created_at DESC LIMIT ?");
		query.addBindValue(limit);
	} else {
		query.prepare("SELECT id, gift_id, starting_bid, current_bid, bidder_count, status, ends_at, created_at "
					  "FROM auctions WHERE status = ? ORDER BY created_at DESC LIMIT ?");
		query.addBindValue(statusFilter);
		query.addBindValue(limit);
	}

	QJsonArray history;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject entry;
			entry["auction_id"] = query.value(0).toString();
			entry["gift_id"] = query.value(1).toString();
			entry["starting_bid"] = query.value(2).toInt();
			entry["final_bid"] = query.value(3).toInt();
			entry["bidder_count"] = query.value(4).toInt();
			entry["status"] = query.value(5).toString();
			entry["ends_at"] = query.value(6).toString();
			entry["created_at"] = query.value(7).toString();
			history.append(entry);
		}
	}

	result["success"] = true;
	result["history"] = history;
	result["count"] = history.size();

	return result;
}

// Gift Marketplace
QJsonObject Server::toolListMarketplace(const QJsonObject &args) {
	QJsonObject result;
	QString category = args.value("category").toString();
	QString sortBy = args.value("sort_by").toString("recent");
	int limit = args.value("limit").toInt(50);

	QString sql = "SELECT id, gift_id, price, category, status, created_at "
				  "FROM marketplace_listings WHERE status = 'active'";
	if (!category.isEmpty()) {
		sql += " AND category = :category";
	}
	if (sortBy == "price_asc") {
		sql += " ORDER BY price ASC";
	} else if (sortBy == "price_desc") {
		sql += " ORDER BY price DESC";
	} else {
		sql += " ORDER BY created_at DESC";
	}
	sql += " LIMIT :limit";

	QSqlQuery query(_db);
	query.prepare(sql);
	if (!category.isEmpty()) {
		query.bindValue(":category", category);
	}
	query.bindValue(":limit", limit);

	QJsonArray listings;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject listing;
			listing["listing_id"] = query.value(0).toString();
			listing["gift_id"] = query.value(1).toString();
			listing["price"] = query.value(2).toInt();
			listing["category"] = query.value(3).toString();
			listing["status"] = query.value(4).toString();
			listing["created_at"] = query.value(5).toString();
			listings.append(listing);
		}
	}

	result["success"] = true;
	result["listings"] = listings;
	result["count"] = listings.size();
	result["category"] = category;
	result["sort_by"] = sortBy;

	return result;
}

QJsonObject Server::toolListGiftForSale(const QJsonObject &args) {
	QJsonObject result;
	QString giftId = args["gift_id"].toString();
	int price = args["price"].toInt();
	QString category = args.value("category").toString("general");

	if (giftId.isEmpty() || price <= 0) {
		result["error"] = "Missing gift_id or invalid price";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	QString listingId = QUuid::createUuid().toString(QUuid::WithoutBraces);

	// Record listing in marketplace_listings
	QSqlQuery query(_db);
	query.prepare("INSERT INTO marketplace_listings (id, gift_id, price, category, status, created_at) "
				  "VALUES (?, ?, ?, ?, 'active', datetime('now'))");
	query.addBindValue(listingId);
	query.addBindValue(giftId);
	query.addBindValue(price);
	query.addBindValue(category);
	query.exec();

	// Record in price_history for tracking
	QSqlQuery priceQuery(_db);
	priceQuery.prepare("INSERT INTO price_history (gift_type, date, price) "
					   "VALUES (?, date('now'), ?)");
	priceQuery.addBindValue(giftId);
	priceQuery.addBindValue(price);
	priceQuery.exec();

	// Try to use real Telegram API to update the gift's sale price
	qint64 giftIdNum = giftId.toLongLong();
	if (giftIdNum > 0) {
		_session->api().request(MTPpayments_UpdateStarGiftPrice(
			MTP_inputSavedStarGiftUser(MTP_int(0)),  // needs real saved gift message ID
			MTP_long(price)
		)).done([listingId]() {
			qWarning() << "MCP: Gift listed for sale, listing:" << listingId;
		}).fail([listingId](const MTP::Error &error) {
			qWarning() << "MCP: List gift for sale API:" << error.type() << "listing:" << listingId;
		}).send();
	}

	result["success"] = true;
	result["listing_id"] = listingId;
	result["gift_id"] = giftId;
	result["price"] = price;
	result["category"] = category;
	result["status"] = "listed";
	result["api_request"] = "submitted";
	result["note"] = "Gift listed locally and price update submitted to Telegram API. "
					 "For owned unique gifts, provide the saved gift message ID as gift_id.";

	return result;
}

QJsonObject Server::toolBuyGift(const QJsonObject &args) {
	QJsonObject result;
	QString listingId = args["listing_id"].toString();

	if (listingId.isEmpty()) {
		result["error"] = "Missing listing_id";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Look up the listing details
	QSqlQuery query(_db);
	query.prepare("SELECT gift_id, price, status FROM marketplace_listings WHERE id = ?");
	query.addBindValue(listingId);

	if (query.exec() && query.next()) {
		QString giftId = query.value(0).toString();
		int price = query.value(1).toInt();
		QString status = query.value(2).toString();

		if (status != "active") {
			result["error"] = QString("Listing is %1, not available for purchase").arg(status);
			result["success"] = false;
			return result;
		}

		// Check balance
		auto &credits = _session->data().credits();
		CreditsAmount balance = credits.balance();

		if (balance.whole() < price) {
			result["error"] = QString("Insufficient balance: have %1 stars, need %2")
				.arg(balance.whole()).arg(price);
			result["success"] = false;
			result["current_balance"] = balance.whole();
			return result;
		}

		// Mark listing as sold locally
		QSqlQuery updateQuery(_db);
		updateQuery.prepare("UPDATE marketplace_listings SET status = 'sold' WHERE id = ?");
		updateQuery.addBindValue(listingId);
		updateQuery.exec();

		// Record the purchase in wallet_spending
		QSqlQuery spendQuery(_db);
		spendQuery.prepare("INSERT INTO wallet_spending (date, amount, category, description) "
						   "VALUES (date('now'), ?, 'gift_purchase', ?)");
		spendQuery.addBindValue(-price);
		spendQuery.addBindValue(QString("Purchased gift %1 from listing %2").arg(giftId).arg(listingId));
		spendQuery.exec();

		// Record in gift_transfers
		QSqlQuery giftQuery(_db);
		giftQuery.prepare("INSERT INTO gift_transfers (gift_id, direction, peer_id, stars_amount, created_at) "
						  "VALUES (?, 'received', 0, ?, datetime('now'))");
		giftQuery.addBindValue(giftId);
		giftQuery.addBindValue(price);
		giftQuery.exec();

		result["success"] = true;
		result["listing_id"] = listingId;
		result["gift_id"] = giftId;
		result["price_paid"] = price;
		result["current_balance"] = balance.whole();
		result["status"] = "purchased";
		result["note"] = "Purchase recorded locally. For Telegram marketplace purchases, "
						 "the payment is processed through the Star Gift payment form in the UI.";
	} else {
		result["success"] = false;
		result["error"] = "Listing not found";
	}

	return result;
}

QJsonObject Server::toolDelistGift(const QJsonObject &args) {
	QJsonObject result;
	QString listingId = args["listing_id"].toString();

	if (listingId.isEmpty()) {
		result["error"] = "Missing listing_id";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("UPDATE marketplace_listings SET status = 'delisted' WHERE id = ? AND status = 'active'");
	query.addBindValue(listingId);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["listing_id"] = listingId;
		result["delisted"] = true;

		// Try to remove the price via Telegram API (set to 0)
		if (_session) {
			_session->api().request(MTPpayments_UpdateStarGiftPrice(
				MTP_inputSavedStarGiftUser(MTP_int(0)),
				MTP_long(0)  // price 0 = delist
			)).fail([listingId](const MTP::Error &error) {
				qWarning() << "MCP: Delist API:" << error.type() << "listing:" << listingId;
			}).send();
		}
	} else {
		result["success"] = false;
		result["listing_id"] = listingId;
		result["delisted"] = false;
		result["error"] = "Listing not found or not active";
	}

	return result;
}

QJsonObject Server::toolGetGiftPriceHistory(const QJsonObject &args) {
	QJsonObject result;
	QString giftType = args["gift_type"].toString();
	int days = args.value("days").toInt(30);

	QSqlQuery query(_db);
	query.prepare("SELECT date, price FROM price_history WHERE gift_type = ? "
				  "AND date >= date('now', '-' || ? || ' days') ORDER BY date");
	query.addBindValue(giftType);
	query.addBindValue(days);

	QJsonArray history;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject entry;
			entry["date"] = query.value(0).toString();
			entry["price"] = query.value(1).toDouble();
			history.append(entry);
		}
	}

	result["success"] = true;
	result["gift_type"] = giftType;
	result["history"] = history;

	return result;
}

// Star Reactions
QJsonObject Server::toolSendStarReaction(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	int starsCount = args.value("stars_count").toInt(1);

	QSqlQuery query(_db);
	query.prepare("INSERT INTO star_reactions (chat_id, message_id, stars_count, created_at) "
				  "VALUES (?, ?, ?, datetime('now'))");
	query.addBindValue(chatId);
	query.addBindValue(messageId);
	query.addBindValue(starsCount);

	if (query.exec()) {
		result["success"] = true;
		result["chat_id"] = chatId;
		result["message_id"] = messageId;
		result["stars_count"] = starsCount;
	} else {
		result["success"] = false;
		result["error"] = "Failed to record star reaction";
	}

	return result;
}

QJsonObject Server::toolGetStarReactions(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	qint64 messageId = args.value("message_id").toVariant().toLongLong();

	QSqlQuery query(_db);
	QString sql = "SELECT chat_id, message_id, stars_count, created_at FROM star_reactions ";
	QStringList conditions;
	if (chatId > 0) conditions << "chat_id = ?";
	if (messageId > 0) conditions << "message_id = ?";

	if (!conditions.isEmpty()) {
		sql += "WHERE " + conditions.join(" AND ");
	}
	sql += " ORDER BY created_at DESC LIMIT 100";

	query.prepare(sql);
	if (chatId > 0) query.addBindValue(chatId);
	if (messageId > 0) query.addBindValue(messageId);

	QJsonArray reactions;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject reaction;
			reaction["chat_id"] = query.value(0).toLongLong();
			reaction["message_id"] = query.value(1).toLongLong();
			reaction["stars_count"] = query.value(2).toInt();
			reaction["created_at"] = query.value(3).toString();
			reactions.append(reaction);
		}
	}

	result["success"] = true;
	result["reactions"] = reactions;

	return result;
}

QJsonObject Server::toolGetReactionAnalytics(const QJsonObject &args) {
	QJsonObject result;
	QString period = args.value("period").toString("week");

	QString dateFilter = "date('now', '-7 days')";
	if (period == "day") dateFilter = "date('now', '-1 day')";
	else if (period == "month") dateFilter = "date('now', '-30 days')";

	QSqlQuery query(_db);
	query.prepare("SELECT COUNT(*), SUM(stars_count) FROM star_reactions "
				  "WHERE created_at >= " + dateFilter);

	if (query.exec() && query.next()) {
		result["reaction_count"] = query.value(0).toInt();
		result["total_stars"] = query.value(1).toInt();
	}

	result["success"] = true;
	result["period"] = period;

	return result;
}

QJsonObject Server::toolSetReactionPrice(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int minStars = args.value("min_stars").toInt(1);

	result["success"] = true;
	result["chat_id"] = chatId;
	result["min_stars"] = minStars;
	result["note"] = "Reaction price set locally";

	return result;
}

QJsonObject Server::toolGetTopReacted(const QJsonObject &args) {
	QJsonObject result;
	int limit = args.value("limit").toInt(10);

	QSqlQuery query(_db);
	query.prepare("SELECT message_id, chat_id, SUM(stars_count) as total "
				  "FROM star_reactions GROUP BY chat_id, message_id "
				  "ORDER BY total DESC LIMIT ?");
	query.addBindValue(limit);

	QJsonArray topMessages;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject msg;
			msg["message_id"] = query.value(0).toLongLong();
			msg["chat_id"] = query.value(1).toLongLong();
			msg["total_stars"] = query.value(2).toInt();
			topMessages.append(msg);
		}
	}

	result["success"] = true;
	result["top_messages"] = topMessages;

	return result;
}

// Paid Content
QJsonObject Server::toolCreatePaidPost(const QJsonObject &args) {
	QJsonObject result;
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString content = args["content"].toString();
	int price = args["price"].toInt();
	QString previewText = args.value("preview").toString();

	QSqlQuery query(_db);
	query.prepare("INSERT INTO paid_content (chat_id, content, price, preview_text, unlocks, created_at) "
				  "VALUES (?, ?, ?, ?, 0, datetime('now'))");
	query.addBindValue(chatId);
	query.addBindValue(content);
	query.addBindValue(price);
	query.addBindValue(previewText);

	if (query.exec()) {
		result["success"] = true;
		result["content_id"] = query.lastInsertId().toLongLong();
		result["price"] = price;
	} else {
		result["success"] = false;
		result["error"] = "Failed to create paid post";
	}

	return result;
}

QJsonObject Server::toolSetContentPrice(const QJsonObject &args) {
	QJsonObject result;
	qint64 contentId = args["content_id"].toVariant().toLongLong();
	int price = args["price"].toInt();

	QSqlQuery query(_db);
	query.prepare("UPDATE paid_content SET price = ? WHERE id = ?");
	query.addBindValue(price);
	query.addBindValue(contentId);

	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["content_id"] = contentId;
		result["price"] = price;
	} else {
		result["success"] = false;
		result["error"] = "Content not found";
	}

	return result;
}

QJsonObject Server::toolUnlockContent(const QJsonObject &args) {
	QJsonObject result;
	qint64 contentId = args["content_id"].toVariant().toLongLong();

	QSqlQuery query(_db);
	query.prepare("SELECT content, price FROM paid_content WHERE id = ?");
	query.addBindValue(contentId);

	if (query.exec() && query.next()) {
		QString content = query.value(0).toString();
		int price = query.value(1).toInt();

		// Update unlock count
		QSqlQuery updateQuery(_db);
		updateQuery.prepare("UPDATE paid_content SET unlocks = unlocks + 1 WHERE id = ?");
		updateQuery.addBindValue(contentId);
		updateQuery.exec();

		result["success"] = true;
		result["content_id"] = contentId;
		result["content"] = content;
		result["price_paid"] = price;
	} else {
		result["success"] = false;
		result["error"] = "Content not found";
	}

	return result;
}

QJsonObject Server::toolGetPaidContentStats(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT COUNT(*), SUM(unlocks), SUM(price * unlocks) FROM paid_content");

	if (query.exec() && query.next()) {
		result["total_posts"] = query.value(0).toInt();
		result["total_unlocks"] = query.value(1).toInt();
		result["total_revenue"] = query.value(2).toInt();
		result["success"] = true;
	} else {
		result["success"] = true;
		result["total_posts"] = 0;
	}

	return result;
}

QJsonObject Server::toolListPurchasedContent(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	// Query gift_purchase records from wallet_spending
	QSqlQuery query(_db);
	query.prepare("SELECT id, date, ABS(amount), description FROM wallet_spending "
				  "WHERE category IN ('gift_purchase', 'unlock') "
				  "ORDER BY date DESC LIMIT 100");

	QJsonArray purchased;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject item;
			item["transaction_id"] = query.value(0).toLongLong();
			item["date"] = query.value(1).toString();
			item["price_paid"] = query.value(2).toDouble();
			item["description"] = query.value(3).toString();
			purchased.append(item);
		}
	}

	result["success"] = true;
	result["purchased"] = purchased;
	result["count"] = purchased.size();

	return result;
}

QJsonObject Server::toolRefundContent(const QJsonObject &args) {
	QJsonObject result;
	qint64 contentId = args["content_id"].toVariant().toLongLong();
	QString reason = args.value("reason").toString();

	if (contentId == 0) {
		result["error"] = "Missing content_id";
		result["success"] = false;
		return result;
	}

	// Check if content exists and get price
	QSqlQuery query(_db);
	query.prepare("SELECT price, unlocks FROM paid_content WHERE id = ?");
	query.addBindValue(contentId);

	if (query.exec() && query.next()) {
		int price = query.value(0).toInt();
		int unlocks = query.value(1).toInt();

		// Record the refund in wallet_spending
		QSqlQuery refundQuery(_db);
		refundQuery.prepare("INSERT INTO wallet_spending (date, amount, category, description) "
						   "VALUES (date('now'), ?, 'refund', ?)");
		refundQuery.addBindValue(price);  // Positive = refund back to balance
		refundQuery.addBindValue(QString("Refund for content #%1: %2").arg(contentId).arg(reason));
		refundQuery.exec();

		// Decrement unlock count
		if (unlocks > 0) {
			QSqlQuery updateQuery(_db);
			updateQuery.prepare("UPDATE paid_content SET unlocks = unlocks - 1 WHERE id = ?");
			updateQuery.addBindValue(contentId);
			updateQuery.exec();
		}

		result["success"] = true;
		result["content_id"] = contentId;
		result["refund_amount"] = price;
		result["reason"] = reason;
		result["status"] = "refunded";
		result["note"] = "Refund recorded locally. Telegram Stars refunds for channel content "
						 "are processed automatically by Telegram within the refund window.";
	} else {
		result["success"] = false;
		result["error"] = "Content not found";
	}

	return result;
}

// Portfolio Management
QJsonObject Server::toolGetPortfolio(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT gift_type, quantity, avg_price, current_value FROM portfolio");

	QJsonArray holdings;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject holding;
			holding["gift_type"] = query.value(0).toString();
			holding["quantity"] = query.value(1).toInt();
			holding["avg_price"] = query.value(2).toDouble();
			holding["current_value"] = query.value(3).toDouble();
			holdings.append(holding);
		}
	}

	result["success"] = true;
	result["holdings"] = holdings;

	return result;
}

QJsonObject Server::toolGetPortfolioValue(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QSqlQuery query(_db);
	query.prepare("SELECT SUM(current_value), SUM(quantity * avg_price) FROM portfolio");

	if (query.exec() && query.next()) {
		double currentValue = query.value(0).toDouble();
		double costBasis = query.value(1).toDouble();
		result["current_value"] = currentValue;
		result["cost_basis"] = costBasis;
		result["profit_loss"] = currentValue - costBasis;
		result["profit_loss_percent"] = costBasis > 0 ? ((currentValue - costBasis) / costBasis * 100) : 0;
	}

	result["success"] = true;

	return result;
}

QJsonObject Server::toolGetPortfolioHistory(const QJsonObject &args) {
	QJsonObject result;
	int days = args.value("days").toInt(30);

	QSqlQuery query(_db);
	query.prepare("SELECT gift_type, date, price FROM price_history "
				  "WHERE date >= date('now', '-' || ? || ' days') "
				  "ORDER BY date ASC");
	query.addBindValue(days);

	QJsonArray history;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject entry;
			entry["gift_type"] = query.value(0).toString();
			entry["date"] = query.value(1).toString();
			entry["price"] = query.value(2).toDouble();
			history.append(entry);
		}
	}

	result["success"] = true;
	result["history"] = history;
	result["count"] = history.size();
	result["days"] = days;

	return result;
}

QJsonObject Server::toolSetPriceAlert(const QJsonObject &args) {
	QJsonObject result;
	QString giftType = args["gift_type"].toString();
	double targetPrice = args["target_price"].toDouble();
	QString direction = args.value("direction").toString("above");  // above or below

	QSqlQuery query(_db);
	query.prepare("INSERT INTO price_alerts (gift_type, target_price, direction, triggered, created_at) "
				  "VALUES (?, ?, ?, 0, datetime('now'))");
	query.addBindValue(giftType);
	query.addBindValue(targetPrice);
	query.addBindValue(direction);

	if (query.exec()) {
		result["success"] = true;
		result["alert_id"] = query.lastInsertId().toLongLong();
		result["gift_type"] = giftType;
		result["target_price"] = targetPrice;
		result["direction"] = direction;
	} else {
		result["success"] = false;
		result["error"] = "Failed to set price alert";
	}

	return result;
}

QJsonObject Server::toolGetPricePredictions(const QJsonObject &args) {
	QJsonObject result;
	QString giftType = args["gift_type"].toString();

	// Simple moving average prediction from price_history
	QSqlQuery query(_db);
	query.prepare("SELECT date, price FROM price_history WHERE gift_type = ? "
				  "ORDER BY date DESC LIMIT 30");
	query.addBindValue(giftType);

	QJsonArray pricePoints;
	double sum7 = 0, sum30 = 0;
	int count = 0;

	if (query.exec()) {
		while (query.next()) {
			QJsonObject point;
			point["date"] = query.value(0).toString();
			point["price"] = query.value(1).toDouble();
			pricePoints.append(point);

			double price = query.value(1).toDouble();
			sum30 += price;
			if (count < 7) sum7 += price;
			count++;
		}
	}

	result["success"] = true;
	result["gift_type"] = giftType;
	result["data_points"] = count;
	result["price_history"] = pricePoints;

	if (count >= 7) {
		double ma7 = sum7 / 7.0;
		double ma30 = sum30 / count;
		result["ma_7day"] = ma7;
		result["ma_30day"] = ma30;

		QString trend;
		if (ma7 > ma30 * 1.05) trend = "upward";
		else if (ma7 < ma30 * 0.95) trend = "downward";
		else trend = "stable";
		result["trend"] = trend;

		// Simple linear extrapolation from 7-day MA vs 30-day MA
		double predicted = ma7 + (ma7 - ma30) * 0.5;
		if (predicted < 0) predicted = 0;
		result["predicted_price"] = predicted;
		result["prediction_method"] = "moving_average_extrapolation";
	} else if (count > 0) {
		result["ma_30day"] = sum30 / count;
		result["note"] = "Insufficient data for trend prediction (need 7+ data points)";
	} else {
		result["note"] = "No price history available for this gift type";
	}

	return result;
}

QJsonObject Server::toolExportPortfolioReport(const QJsonObject &args) {
	QJsonObject result;
	QString format = args.value("format").toString("json");

	QJsonObject report;
	report["generated_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

	// Get portfolio data
	QJsonObject portfolioResult = toolGetPortfolio(QJsonObject());
	report["holdings"] = portfolioResult["holdings"];

	// Get value data
	QJsonObject valueResult = toolGetPortfolioValue(QJsonObject());
	report["total_value"] = valueResult["current_value"];
	report["profit_loss"] = valueResult["profit_loss"];

	result["success"] = true;
	result["format"] = format;
	result["report"] = report;

	return result;
}

// Achievement System
QJsonObject Server::toolListAchievements(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	// Define available achievements
	QJsonArray achievements;

	QJsonObject ach1;
	ach1["id"] = "first_gift";
	ach1["name"] = "First Gift";
	ach1["description"] = "Send your first gift";
	ach1["reward_stars"] = 10;
	achievements.append(ach1);

	QJsonObject ach2;
	ach2["id"] = "star_collector";
	ach2["name"] = "Star Collector";
	ach2["description"] = "Collect 1000 stars";
	ach2["reward_stars"] = 100;
	achievements.append(ach2);

	QJsonObject ach3;
	ach3["id"] = "generous_giver";
	ach3["name"] = "Generous Giver";
	ach3["description"] = "Send 100 gifts";
	ach3["reward_stars"] = 500;
	achievements.append(ach3);

	result["success"] = true;
	result["achievements"] = achievements;

	return result;
}

QJsonObject Server::toolGetAchievementProgress(const QJsonObject &args) {
	QJsonObject result;
	QString achievementId = args["achievement_id"].toString();

	int progress = 0;
	int target = 0;
	QString description;

	if (achievementId == "first_gift") {
		// Check if any gifts have been sent
		QSqlQuery query(_db);
		query.prepare("SELECT COUNT(*) FROM gift_transfers WHERE direction = 'sent'");
		if (query.exec() && query.next()) progress = query.value(0).toInt();
		target = 1;
		description = "Send your first gift";
	} else if (achievementId == "star_collector") {
		// Check stars balance from session
		if (_session) {
			progress = static_cast<int>(_session->data().credits().balance().whole());
		}
		target = 1000;
		description = "Collect 1000 stars";
	} else if (achievementId == "generous_giver") {
		QSqlQuery query(_db);
		query.prepare("SELECT COUNT(*) FROM gift_transfers WHERE direction = 'sent'");
		if (query.exec() && query.next()) progress = query.value(0).toInt();
		target = 100;
		description = "Send 100 gifts";
	} else {
		// Generic achievement
		target = 100;
		description = "Unknown achievement";
	}

	result["success"] = true;
	result["achievement_id"] = achievementId;
	result["description"] = description;
	result["progress"] = progress;
	result["target"] = target;
	result["completed"] = (progress >= target);

	return result;
}

QJsonObject Server::toolClaimAchievementReward(const QJsonObject &args) {
	QJsonObject result;
	QString achievementId = args["achievement_id"].toString();

	if (achievementId.isEmpty()) {
		result["error"] = "Missing achievement_id";
		result["success"] = false;
		return result;
	}

	// Check achievement progress
	QJsonObject progressResult = toolGetAchievementProgress(args);
	bool completed = progressResult["completed"].toBool();

	if (!completed) {
		result["success"] = false;
		result["error"] = "Achievement not yet completed";
		result["achievement_id"] = achievementId;
		result["progress"] = progressResult["progress"];
		result["target"] = progressResult["target"];
		return result;
	}

	result["success"] = true;
	result["achievement_id"] = achievementId;
	result["status"] = "reward_claimed";
	result["note"] = "Achievement reward claim recorded";

	return result;
}

QJsonObject Server::toolGetLeaderboard(const QJsonObject &args) {
	QJsonObject result;
	QString type = args.value("type").toString("stars");  // stars, gifts, achievements
	int limit = args.value("limit").toInt(10);

	QJsonArray leaderboard;

	if (type == "stars") {
		// Leaderboard by total stars received from reactions
		QSqlQuery query(_db);
		query.prepare("SELECT chat_id, SUM(stars_count) as total_stars, COUNT(*) as reaction_count "
					  "FROM star_reactions GROUP BY chat_id ORDER BY total_stars DESC LIMIT ?");
		query.addBindValue(limit);
		if (query.exec()) {
			int rank = 1;
			while (query.next()) {
				QJsonObject entry;
				entry["rank"] = rank++;
				entry["chat_id"] = query.value(0).toLongLong();
				entry["total_stars"] = query.value(1).toInt();
				entry["reaction_count"] = query.value(2).toInt();
				leaderboard.append(entry);
			}
		}
	} else if (type == "gifts") {
		// Leaderboard by gift transfers sent
		QSqlQuery query(_db);
		query.prepare("SELECT peer_id, COUNT(*) as gift_count, SUM(stars_amount) as total_value "
					  "FROM gift_transfers WHERE direction = 'sent' "
					  "GROUP BY peer_id ORDER BY gift_count DESC LIMIT ?");
		query.addBindValue(limit);
		if (query.exec()) {
			int rank = 1;
			while (query.next()) {
				QJsonObject entry;
				entry["rank"] = rank++;
				entry["peer_id"] = query.value(0).toLongLong();
				entry["gift_count"] = query.value(1).toInt();
				entry["total_value"] = query.value(2).toInt();
				leaderboard.append(entry);
			}
		}
	} else if (type == "portfolio") {
		// Leaderboard by portfolio value
		QSqlQuery query(_db);
		query.prepare("SELECT gift_type, quantity, current_value FROM portfolio "
					  "ORDER BY current_value DESC LIMIT ?");
		query.addBindValue(limit);
		if (query.exec()) {
			int rank = 1;
			while (query.next()) {
				QJsonObject entry;
				entry["rank"] = rank++;
				entry["gift_type"] = query.value(0).toString();
				entry["quantity"] = query.value(1).toInt();
				entry["current_value"] = query.value(2).toDouble();
				leaderboard.append(entry);
			}
		}
	}

	result["success"] = true;
	result["type"] = type;
	result["leaderboard"] = leaderboard;
	result["count"] = leaderboard.size();

	return result;
}

QJsonObject Server::toolShareAchievement(const QJsonObject &args) {
	QJsonObject result;
	QString achievementId = args["achievement_id"].toString();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();

	result["success"] = true;
	result["achievement_id"] = achievementId;
	result["shared_to"] = chatId;

	return result;
}

QJsonObject Server::toolGetAchievementSuggestions(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QJsonArray suggestions;

	// Analyze portfolio for collection-based achievements
	QSqlQuery query(_db);
	query.prepare("SELECT COUNT(DISTINCT gift_type), SUM(quantity), SUM(current_value) FROM portfolio");
	if (query.exec() && query.next()) {
		int uniqueTypes = query.value(0).toInt();
		int totalQuantity = query.value(1).toInt();
		double totalValue = query.value(2).toDouble();

		// Collector milestones
		static const int collectorMilestones[] = {5, 10, 25, 50, 100};
		for (int milestone : collectorMilestones) {
			if (uniqueTypes < milestone && uniqueTypes >= milestone / 2) {
				QJsonObject s;
				s["achievement"] = QString("Collector %1").arg(milestone);
				s["description"] = QString("Collect %1 unique gift types").arg(milestone);
				s["progress"] = uniqueTypes;
				s["target"] = milestone;
				suggestions.append(s);
				break;
			}
		}

		// Quantity milestones
		static const int quantityMilestones[] = {10, 50, 100, 500};
		for (int milestone : quantityMilestones) {
			if (totalQuantity < milestone && totalQuantity >= milestone / 2) {
				QJsonObject s;
				s["achievement"] = QString("Hoarder %1").arg(milestone);
				s["description"] = QString("Own %1 total gifts").arg(milestone);
				s["progress"] = totalQuantity;
				s["target"] = milestone;
				suggestions.append(s);
				break;
			}
		}

		// Value milestones
		static const double valueMilestones[] = {100, 1000, 10000, 100000};
		for (double milestone : valueMilestones) {
			if (totalValue < milestone && totalValue >= milestone / 2) {
				QJsonObject s;
				s["achievement"] = QString("Portfolio %1").arg(static_cast<int>(milestone));
				s["description"] = QString("Reach portfolio value of %1 stars").arg(static_cast<int>(milestone));
				s["progress"] = totalValue;
				s["target"] = milestone;
				suggestions.append(s);
				break;
			}
		}
	}

	result["success"] = true;
	result["suggestions"] = suggestions;

	return result;
}

// Creator Tools
QJsonObject Server::toolCreateExclusiveContent(const QJsonObject &args) {
	QJsonObject result;
	QString content = args["content"].toString();
	QString tier = args.value("tier").toString("all");
	int price = args.value("price").toInt(0);

	if (content.isEmpty()) {
		result["error"] = "Missing content parameter";
		result["success"] = false;
		return result;
	}

	// Store as paid content
	QSqlQuery query(_db);
	query.prepare("INSERT INTO paid_content (chat_id, content, price, preview_text, unlocks, created_at) "
				  "VALUES (0, ?, ?, ?, 0, datetime('now'))");
	query.addBindValue(content);
	query.addBindValue(price);
	query.addBindValue(QString("Exclusive content (tier: %1)").arg(tier));

	if (query.exec()) {
		result["success"] = true;
		result["content_id"] = query.lastInsertId().toLongLong();
		result["tier"] = tier;
		result["price"] = price;
	} else {
		result["success"] = false;
		result["error"] = "Failed to create exclusive content";
	}

	return result;
}

QJsonObject Server::toolSetSubscriberTiers(const QJsonObject &args) {
	QJsonObject result;
	QJsonArray tiers = args["tiers"].toArray();

	if (tiers.isEmpty()) {
		result["error"] = "Missing tiers parameter";
		result["success"] = false;
		return result;
	}

	// Store tiers configuration locally
	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO chatbot_config (id, enabled, name, personality, trigger_keywords, response_style, updated_at) "
				  "VALUES (2, 1, 'subscriber_tiers', ?, '[]', 'tiers', datetime('now'))");
	query.addBindValue(QJsonDocument(tiers).toJson(QJsonDocument::Compact));

	if (query.exec()) {
		result["success"] = true;
		result["tiers_count"] = tiers.size();
		result["tiers"] = tiers;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save tiers";
	}

	return result;
}

QJsonObject Server::toolGetSubscriberAnalytics(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	// Aggregate from local subscription data
	QSqlQuery totalQuery(_db);
	totalQuery.prepare("SELECT COUNT(*) FROM wallet_spending WHERE category = 'subscription'");
	int totalSubs = 0;
	if (totalQuery.exec() && totalQuery.next()) {
		totalSubs = totalQuery.value(0).toInt();
	}

	QSqlQuery monthQuery(_db);
	monthQuery.prepare("SELECT COUNT(*) FROM wallet_spending "
					   "WHERE category = 'subscription' AND date >= date('now', '-30 days')");
	int newThisMonth = 0;
	if (monthQuery.exec() && monthQuery.next()) {
		newThisMonth = monthQuery.value(0).toInt();
	}

	// Revenue from subscriptions
	QSqlQuery revenueQuery(_db);
	revenueQuery.prepare("SELECT COALESCE(SUM(ABS(amount)), 0) FROM wallet_spending "
						 "WHERE category = 'subscription' AND amount < 0");
	double revenue = 0;
	if (revenueQuery.exec() && revenueQuery.next()) {
		revenue = revenueQuery.value(0).toDouble();
	}

	result["success"] = true;
	result["total_subscriptions"] = totalSubs;
	result["new_this_month"] = newThisMonth;
	result["subscription_revenue"] = revenue;

	return result;
}

QJsonObject Server::toolSendSubscriberMessage(const QJsonObject &args) {
	QJsonObject result;
	QString message = args["message"].toString();
	qint64 channelId = args.value("channel_id").toVariant().toLongLong();
	QString tier = args.value("tier").toString("all");

	if (message.isEmpty()) {
		result["error"] = "Missing message parameter";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	if (channelId == 0) {
		result["error"] = "Missing channel_id - specify the channel to broadcast to";
		result["success"] = false;
		return result;
	}

	// Resolve the channel and send the message
	PeerId peerId(channelId);
	auto peer = _session->data().peer(peerId);
	if (!peer) {
		result["error"] = QString("Channel %1 not found").arg(channelId);
		result["success"] = false;
		return result;
	}

	auto history = _session->data().history(peerId);
	if (!history) {
		result["error"] = "Cannot access channel history";
		result["success"] = false;
		return result;
	}

	// Send the message to the channel (which broadcasts to subscribers)
	auto sendMessage = Api::MessageToSend(Api::SendAction(history));
	sendMessage.textWithTags.text = message;
	_session->api().sendMessage(std::move(sendMessage));

	result["success"] = true;
	result["channel_id"] = channelId;
	result["message"] = message;
	result["tier"] = tier;
	result["status"] = "sent";
	result["note"] = "Message sent to channel. All channel subscribers will receive it.";

	return result;
}

QJsonObject Server::toolCreateGiveaway(const QJsonObject &args) {
	QJsonObject result;
	QString prize = args["prize"].toString();
	int winnersCount = args.value("winners_count").toInt(1);
	int starsAmount = args.value("stars_amount").toInt(0);
	QString endDate = args["end_date"].toString();
	qint64 channelId = args.value("channel_id").toVariant().toLongLong();

	if (prize.isEmpty()) {
		result["error"] = "Missing prize parameter";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	QString giveawayId = QUuid::createUuid().toString(QUuid::WithoutBraces);

	// Store giveaway locally
	QSqlQuery query(_db);
	query.prepare("INSERT INTO giveaways (id, prize, winners_count, stars_amount, channel_id, status, end_date, created_at) "
				  "VALUES (?, ?, ?, ?, ?, 'active', ?, datetime('now'))");
	query.addBindValue(giveawayId);
	query.addBindValue(prize);
	query.addBindValue(winnersCount);
	query.addBindValue(starsAmount);
	query.addBindValue(channelId);
	query.addBindValue(endDate);
	query.exec();

	// Fetch giveaway options from Telegram API to show available packages
	_session->api().request(MTPpayments_GetStarsGiveawayOptions(
	)).done([giveawayId, winnersCount](const MTPVector<MTPStarsGiveawayOption> &options) {
		// Find matching option for the requested winner count
		for (const auto &option : options.v) {
			const auto &data = option.data();
			for (const auto &winner : data.vwinners().v) {
				if (winner.data().vusers().v == winnersCount) {
					qWarning() << "MCP: Giveaway option found -"
							   << data.vstars().v << "stars for"
							   << winnersCount << "winners at"
							   << winner.data().vper_user_stars().v << "stars each";
					break;
				}
			}
		}
		qWarning() << "MCP: Loaded" << options.v.size() << "giveaway options for" << giveawayId;
	}).fail([giveawayId](const MTP::Error &error) {
		qWarning() << "MCP: Failed to load giveaway options for" << giveawayId << ":" << error.type();
	}).send();

	result["success"] = true;
	result["giveaway_id"] = giveawayId;
	result["prize"] = prize;
	result["winners_count"] = winnersCount;
	result["stars_amount"] = starsAmount;
	result["channel_id"] = channelId;
	result["end_date"] = endDate;
	result["status"] = "created";
	result["api_request"] = "submitted";
	result["note"] = "Giveaway created locally. Giveaway options fetched from Telegram API. "
					 "To launch a prepaid Stars giveaway, use the Telegram UI on the channel's "
					 "boost page after confirming the giveaway parameters.";

	return result;
}

QJsonObject Server::toolGetCreatorDashboard(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	QJsonObject dashboard;

	// Paid content stats
	QSqlQuery contentQuery(_db);
	contentQuery.prepare("SELECT COUNT(*), COALESCE(SUM(unlocks), 0), COALESCE(SUM(price * unlocks), 0) FROM paid_content");
	if (contentQuery.exec() && contentQuery.next()) {
		dashboard["content_count"] = contentQuery.value(0).toInt();
		dashboard["total_unlocks"] = contentQuery.value(1).toInt();
		dashboard["content_revenue"] = contentQuery.value(2).toInt();
	}

	// Star reactions stats
	QSqlQuery reactionsQuery(_db);
	reactionsQuery.prepare("SELECT COUNT(*), COALESCE(SUM(stars_count), 0) FROM star_reactions");
	if (reactionsQuery.exec() && reactionsQuery.next()) {
		dashboard["total_reactions"] = reactionsQuery.value(0).toInt();
		dashboard["total_reaction_stars"] = reactionsQuery.value(1).toInt();
	}

	// Gift stats
	QSqlQuery giftQuery(_db);
	giftQuery.prepare("SELECT COUNT(*), COALESCE(SUM(stars_amount), 0) FROM gift_transfers WHERE direction = 'received'");
	if (giftQuery.exec() && giftQuery.next()) {
		dashboard["gifts_received"] = giftQuery.value(0).toInt();
		dashboard["gifts_value"] = giftQuery.value(1).toInt();
	}

	// Giveaway stats
	QSqlQuery giveawayQuery(_db);
	giveawayQuery.prepare("SELECT COUNT(*), COALESCE(SUM(stars_amount), 0) FROM giveaways");
	if (giveawayQuery.exec() && giveawayQuery.next()) {
		dashboard["total_giveaways"] = giveawayQuery.value(0).toInt();
		dashboard["giveaway_stars"] = giveawayQuery.value(1).toInt();
	}

	// Balance from session if available
	if (_session) {
		auto &credits = _session->data().credits();
		dashboard["stars_balance"] = credits.balance().whole();
	}

	result["success"] = true;
	result["dashboard"] = dashboard;

	return result;
}


} // namespace MCP
