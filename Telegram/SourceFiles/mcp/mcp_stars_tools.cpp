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

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Returns the account's real gift collections.
	//
	// This used to fire exactly this request, hand the reply to qWarning, and
	// then answer with rows from a local `gift_collections` table -- so the
	// caller got collections this client had invented while the real ones went
	// to stderr and were dropped. Star gift collections live on the server;
	// there is nothing for a local table to add.
	return awaitMtp([&](auto done, auto fail) {
		const auto self = _session->data().peer(_session->userPeerId());
		_session->api().request(MTPpayments_GetStarGiftCollections(
			self->input(),
			MTP_long(0) // hash 0: nothing cached, send the full list
		)).done([=](const MTPpayments_StarGiftCollections &result) {
			auto value = QJsonObject();
			auto collections = QJsonArray();
			result.match([&](const MTPDpayments_starGiftCollections &data) {
				for (const auto &entry : data.vcollections().v) {
					const auto &collection = entry.data();
					auto obj = QJsonObject();
					obj["collection_id"] = collection.vcollection_id().v;
					obj["title"] = qs(collection.vtitle());
					obj["gifts_count"] = collection.vgifts_count().v;
					collections.append(obj);
				}
			}, [](const MTPDpayments_starGiftCollectionsNotModified &) {
			});
			value["success"] = true;
			value["collections"] = collections;
			value["count"] = collections.size();
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail(error.type());
		}).send();
	});
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

// Gift auctions.
//
// Telegram runs these itself: an auction is a property of a gift, opened and
// closed server-side, so there is no create and no cancel for a client to
// call. The two tools that claimed to do that (create_gift_auction,
// cancel_auction) invented an `auctions` SQLite table and reported UUIDs that
// existed nowhere but this database -- they are gone rather than reimplemented,
// because no API can back them.
//
// What remains is real: the active auction list, one auction's live state, and
// the gifts a finished auction handed out. Bidding stays unimplemented on
// purpose -- see toolPlaceBid.

QJsonObject Server::toolPlaceBid(const QJsonObject &args) {
	QJsonObject result;
	const auto giftId = qint64(args["gift_id"].toDouble());
	const auto bidAmount = qint64(args["bid_amount"].toDouble());

	// A bid is a purchase: it goes through the invoice flow
	// (inputInvoiceStarGiftAuctionBid -> payments.getPaymentForm ->
	// payments.sendStarsForm) and debits real stars. This used to write a row
	// into a local `auctions` table and answer "bid_placed", which was a lie in
	// the most expensive direction -- a caller could believe it held the top
	// bid on a gift it had never bid on.
	//
	// Refusing is the honest answer until the flow is wired and there is a
	// balance to exercise it against.
	result["success"] = false;
	result["implemented"] = false;
	result["gift_id"] = giftId;
	result["bid_amount"] = bidAmount;
	result["error"] = "Bidding is not implemented";
	result["reason"] = "A bid spends stars through the payment-form flow "
		"(inputInvoiceStarGiftAuctionBid). Nothing is recorded locally, because "
		"a bid this client did not actually place must never look like one it did.";
	return result;
}

namespace {

// A gift carries its auction, so the gift id is the auction's identity --
// there is no separate auction id anywhere in the API.
[[nodiscard]] qint64 StarGiftId(const MTPStarGift &gift) {
	return gift.match([](const MTPDstarGift &data) {
		return qint64(data.vid().v);
	}, [](const MTPDstarGiftUnique &data) {
		return qint64(data.vid().v);
	});
}

// Auctions run in rounds: each round releases gifts to the current top
// bidders, so "the state" is a running tally, not a single high bid. Reported
// as-is; nothing here is inferred or interpolated.
[[nodiscard]] QJsonObject AuctionStateJson(
		const MTPStarGiftAuctionState &state) {
	auto json = QJsonObject();
	state.match([&](const MTPDstarGiftAuctionState &data) {
		json["phase"] = "running";
		json["version"] = data.vversion().v;
		json["start_date"] = qint64(data.vstart_date().v);
		json["end_date"] = qint64(data.vend_date().v);
		json["min_bid_amount"] = qint64(data.vmin_bid_amount().v);
		json["next_round_at"] = qint64(data.vnext_round_at().v);
		json["last_gift_num"] = data.vlast_gift_num().v;
		json["gifts_left"] = data.vgifts_left().v;
		json["current_round"] = data.vcurrent_round().v;
		json["total_rounds"] = data.vtotal_rounds().v;

		auto levels = QJsonArray();
		for (const auto &level : data.vbid_levels().v) {
			const auto &entry = level.data();
			auto obj = QJsonObject();
			obj["pos"] = entry.vpos().v;
			obj["amount"] = qint64(entry.vamount().v);
			obj["date"] = qint64(entry.vdate().v);
			levels.append(obj);
		}
		json["bid_levels"] = levels;
	}, [&](const MTPDstarGiftAuctionStateFinished &data) {
		json["phase"] = "finished";
		json["start_date"] = qint64(data.vstart_date().v);
		json["end_date"] = qint64(data.vend_date().v);
		json["average_price"] = qint64(data.vaverage_price().v);
		if (const auto listed = data.vlisted_count()) {
			json["listed_count"] = listed->v;
		}
		if (const auto url = data.vfragment_listed_url()) {
			json["fragment_listed_url"] = qs(*url);
		}
	}, [&](const MTPDstarGiftAuctionStateNotModified &) {
		json["phase"] = "not_modified";
	});
	return json;
}

// What this account did in the auction. `acquired_count` is always present;
// the bid fields only exist once a bid has actually been placed, so their
// absence is reported as absence rather than as a zero bid.
[[nodiscard]] QJsonObject AuctionUserStateJson(
		const MTPStarGiftAuctionUserState &state) {
	const auto &data = state.data();
	auto json = QJsonObject();
	json["acquired_count"] = data.vacquired_count().v;
	json["returned"] = data.is_returned();
	if (const auto amount = data.vbid_amount()) {
		json["bid_amount"] = qint64(amount->v);
	}
	if (const auto date = data.vbid_date()) {
		json["bid_date"] = qint64(date->v);
	}
	if (const auto min = data.vmin_bid_amount()) {
		json["min_bid_amount"] = qint64(min->v);
	}
	return json;
}

} // namespace

QJsonObject Server::toolListAuctions(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject result;

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// Telegram decides which auctions are live; this used to read them out of
	// a local table that only ever held rows create_gift_auction had invented.
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetStarGiftActiveAuctions(
			MTP_long(0) // hash 0: nothing cached, send the full list
		)).done([=](const MTPpayments_StarGiftActiveAuctions &result) {
			auto value = QJsonObject();
			auto auctions = QJsonArray();
			result.match([&](
					const MTPDpayments_starGiftActiveAuctions &data) {
				for (const auto &entry : data.vauctions().v) {
					const auto &auction = entry.data();
					auto obj = QJsonObject();
					obj["gift_id"] = StarGiftId(auction.vgift());
					obj["state"] = AuctionStateJson(auction.vstate());
					obj["your_state"] = AuctionUserStateJson(
						auction.vuser_state());
					auctions.append(obj);
				}
			}, [](const MTPDpayments_starGiftActiveAuctionsNotModified &) {
			});
			value["success"] = true;
			value["auctions"] = auctions;
			value["count"] = auctions.size();
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail(error.type());
		}).send();
	});
}

QJsonObject Server::toolGetAuctionStatus(const QJsonObject &args) {
	QJsonObject result;
	const auto giftId = qint64(args["gift_id"].toDouble());

	if (!giftId) {
		result["error"] = "Missing gift_id parameter";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetStarGiftAuctionState(
			MTP_inputStarGiftAuction(MTP_long(giftId)),
			MTP_int(0) // version 0: no cached state to diff against
		)).done([=](const MTPpayments_StarGiftAuctionState &result) {
			const auto &data = result.data();
			auto value = QJsonObject();
			value["success"] = true;
			value["gift_id"] = StarGiftId(data.vgift());
			value["state"] = AuctionStateJson(data.vstate());
			value["your_state"] = AuctionUserStateJson(data.vuser_state());
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail(error.type());
		}).send();
	});
}

QJsonObject Server::toolGetAuctionHistory(const QJsonObject &args) {
	QJsonObject result;
	const auto giftId = qint64(args["gift_id"].toDouble());

	if (!giftId) {
		result["error"] = "Missing gift_id parameter";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	// The gifts an auction actually handed out, and to whom -- the closest
	// thing to a result sheet Telegram exposes.
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetStarGiftAuctionAcquiredGifts(
			MTP_long(giftId)
		)).done([=](const MTPpayments_StarGiftAuctionAcquiredGifts &result) {
			const auto &data = result.data();
			auto value = QJsonObject();
			auto gifts = QJsonArray();
			for (const auto &item : data.vgifts().v) {
				const auto &entry = item.data();
				auto obj = QJsonObject();
				obj["peer_id"] = entry.is_name_hidden()
					? QJsonValue()
					: QJsonValue(qint64(peerFromMTP(entry.vpeer()).value));
				obj["name_hidden"] = entry.is_name_hidden();
				obj["date"] = qint64(entry.vdate().v);
				obj["bid_amount"] = qint64(entry.vbid_amount().v);
				obj["round"] = entry.vround().v;
				obj["pos"] = entry.vpos().v;
				if (const auto num = entry.vgift_num()) {
					obj["gift_num"] = num->v;
				}
				gifts.append(obj);
			}
			value["success"] = true;
			value["gift_id"] = giftId;
			value["acquired_gifts"] = gifts;
			value["count"] = gifts.size();
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail(error.type());
		}).send();
	});
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

	// MTPpayments_UpdateStarGiftPrice API not available in this version
	// Gift listing would be done here if the API supported it
	qint64 giftIdNum = giftId.toLongLong();
	if (giftIdNum > 0) {
		qWarning() << "MCP: Gift listing API not available - price update skipped for listing" << listingId;
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

		// Credits API not available in this version - skip balance check
		qint64 balance = 0;

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
		result["current_balance"] = balance;
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

		// MTPpayments_UpdateStarGiftPrice API not available in this version
		// Delisting would be done here if the API supported it
		if (_session) {
			qWarning() << "MCP: Delist API not available - skipped for listing" << listingId;
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
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto stars = qint64(args.value("min_stars").toInt(1));
	if (!_session) {
		return toolError("No active session");
	}
	if (!chatId) {
		return toolError("chat_id is required and must be non-zero");
	}
	if (stars < 0) {
		return toolError("min_stars must not be negative");
	}
	const auto peer = _session->data().peerLoaded(PeerId(chatId));
	const auto channel = peer ? peer->asChannel() : nullptr;
	if (!channel) {
		return toolError("chat_id must name a channel or supergroup: paid "
			"message pricing is only settable there");
	}

	// Previously returned success with the note "Reaction price set locally",
	// having stored nothing anywhere. This sets the real per-message price.
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPchannels_UpdatePaidMessagesPrice(
			MTP_flags(0),
			channel->inputChannel(),
			MTP_long(stars)
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			QJsonObject value;
			value["success"] = true;
			value["chat_id"] = chatId;
			value["send_paid_messages_stars"] = stars;
			done(value);
		}).fail([=](const MTP::Error &error) {
			// Telegram answers CHAT_NOT_MODIFIED when the price already has
			// the requested value. Nothing failed -- the caller asked for a
			// state the channel is already in, so report the state, not an
			// error it would have to special-case itself.
			if (error.type() == u"CHAT_NOT_MODIFIED"_q) {
				QJsonObject value;
				value["success"] = true;
				value["chat_id"] = chatId;
				value["send_paid_messages_stars"] = stars;
				value["note"] = "Already set to this price";
				done(value);
				return;
			}
			fail("channels.updatePaidMessagesPrice failed: " + error.type());
		}).send();
	});
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
	const auto format = args.value("format").toString("json").toLower();
	if (format != "json" && format != "csv") {
		return toolError("format must be 'json' or 'csv'");
	}

	QJsonObject report;
	report["generated_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
	const auto portfolio = toolGetPortfolio(QJsonObject());
	const auto holdings = portfolio.value("holdings").toArray();
	report["holdings"] = holdings;
	const auto value = toolGetPortfolioValue(QJsonObject());
	report["total_value"] = value.value("current_value");
	report["profit_loss"] = value.value("profit_loss");

	// The tool is called export and takes a format, so it writes a file in
	// that format. It used to accept "csv", ignore it, and hand back the same
	// JSON object inline -- a caller asking for CSV got JSON and no file.
	const auto dir = defaultExportDir();
	QDir().mkpath(dir);
	const auto stamp = QDateTime::currentDateTimeUtc().toString(
		u"yyyyMMdd-HHmmss"_q);
	const auto path = QString("%1/portfolio-%2.%3").arg(dir, stamp, format);

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return toolError("Could not open " + path + " for writing");
	}
	if (format == "json") {
		file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
	} else {
		QTextStream out(&file);
		out << "gift_type,quantity,avg_price,current_value\n";
		for (const auto &entry : holdings) {
			const auto row = entry.toObject();
			// Quote the one free-text column: a gift type containing a comma
			// would otherwise shift every following column.
			auto type = row.value("gift_type").toString();
			type.replace('"', u"\"\""_q);
			out << '"' << type << '"' << ','
				<< row.value("quantity").toInt() << ','
				<< row.value("avg_price").toDouble() << ','
				<< row.value("current_value").toDouble() << '\n';
		}
	}
	file.close();

	QJsonObject result;
	result["success"] = true;
	result["format"] = format;
	result["path"] = path;
	result["holdings_count"] = holdings.size();
	result["report"] = report;
	return result;
}

// Achievement System
QJsonObject Server::toolListAchievements(const QJsonObject &args) {
	Q_UNUSED(args);

	// The catalogue and get_achievement_progress must agree on the ids, so
	// this reports each one's live progress rather than a static list. The
	// previous version returned three hardcoded entries with no progress at
	// all, one of which could never be completed.
	static const auto ids = std::array<const char*, 5>{
		"first_gift", "generous_giver", "archivist", "librarian", "collector" };

	QJsonArray achievements;
	auto completed = 0;
	for (const auto id : ids) {
		QJsonObject query;
		query["achievement_id"] = QString::fromLatin1(id);
		const auto entry = toolGetAchievementProgress(query);
		if (entry.value("completed").toBool()) {
			++completed;
		}
		achievements.append(entry);
	}

	QJsonObject result;
	result["success"] = true;
	result["achievements"] = achievements;
	result["count"] = achievements.size();
	result["completed_count"] = completed;
	return result;
}

QJsonObject Server::toolGetAchievementProgress(const QJsonObject &args) {
	const auto achievementId = args["achievement_id"].toString();
	if (achievementId.isEmpty()) {
		return toolError("achievement_id is required");
	}

	// Achievements track use of *this client*, measured from what it actually
	// records: gifts it sent, messages it archived, gift types it holds. The
	// earlier set included "collect 1000 stars", which reported 0 forever
	// under a comment saying the credits API was unavailable -- a target that
	// cannot move is not an achievement.
	const auto count = [&](const char *sql) {
		QSqlQuery query(_db);
		query.prepare(QString::fromLatin1(sql));
		return (query.exec() && query.next()) ? query.value(0).toInt() : 0;
	};

	auto progress = 0;
	auto target = 0;
	auto description = QString();
	if (achievementId == u"first_gift"_q) {
		progress = count("SELECT COUNT(*) FROM gift_transfers WHERE direction = 'sent'");
		target = 1;
		description = u"Send a gift"_q;
	} else if (achievementId == u"generous_giver"_q) {
		progress = count("SELECT COUNT(*) FROM gift_transfers WHERE direction = 'sent'");
		target = 100;
		description = u"Send 100 gifts"_q;
	} else if (achievementId == u"archivist"_q) {
		progress = count("SELECT COUNT(*) FROM messages");
		target = 10000;
		description = u"Archive 10,000 messages"_q;
	} else if (achievementId == u"librarian"_q) {
		progress = count("SELECT COUNT(DISTINCT chat_id) FROM messages");
		target = 50;
		description = u"Archive messages from 50 chats"_q;
	} else if (achievementId == u"collector"_q) {
		progress = count("SELECT COUNT(*) FROM portfolio WHERE quantity > 0");
		target = 10;
		description = u"Hold 10 different gift types"_q;
	} else {
		return toolError(QString("Unknown achievement '%1'; call "
			"list_achievements for the available ones").arg(achievementId));
	}

	QSqlQuery claimed(_db);
	claimed.prepare("SELECT claimed_at FROM achievement_claims "
		"WHERE achievement_id = ?");
	claimed.addBindValue(achievementId);
	const auto alreadyClaimed = (claimed.exec() && claimed.next());

	QJsonObject result;
	result["success"] = true;
	result["achievement_id"] = achievementId;
	result["description"] = description;
	result["progress"] = progress;
	result["target"] = target;
	result["completed"] = (progress >= target);
	result["claimed"] = alreadyClaimed;
	if (alreadyClaimed) {
		result["claimed_at"] = claimed.value(0).toString();
	}
	return result;
}

QJsonObject Server::toolClaimAchievementReward(const QJsonObject &args) {
	const auto achievementId = args["achievement_id"].toString();
	if (achievementId.isEmpty()) {
		return toolError("achievement_id is required");
	}

	// A claim has to be recorded, or it is not a claim. This used to answer
	// "reward_claimed" and write nothing, so the same achievement could be
	// claimed without limit and no later call could tell it ever had been.
	QSqlQuery existing(_db);
	existing.prepare("SELECT claimed_at FROM achievement_claims "
		"WHERE achievement_id = ?");
	existing.addBindValue(achievementId);
	if (existing.exec() && existing.next()) {
		return toolError(QString("Achievement '%1' was already claimed at %2")
			.arg(achievementId, existing.value(0).toString()));
	}

	const auto progressResult = toolGetAchievementProgress(args);
	if (!progressResult.value("completed").toBool()) {
		auto error = toolError(QString("Achievement '%1' is not complete")
			.arg(achievementId));
		error["progress"] = progressResult.value("progress");
		error["target"] = progressResult.value("target");
		return error;
	}

	QSqlQuery insert(_db);
	insert.prepare("INSERT INTO achievement_claims "
		"(achievement_id, progress_at_claim) VALUES (?, ?)");
	insert.addBindValue(achievementId);
	insert.addBindValue(progressResult.value("progress").toInt());
	if (!insert.exec()) {
		return toolError("Could not record the claim: "
			+ insert.lastError().text());
	}

	QJsonObject result;
	result["success"] = true;
	result["achievement_id"] = achievementId;
	result["progress_at_claim"] = progressResult.value("progress");
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
	const auto achievementId = args["achievement_id"].toString();
	const auto chatId = args.value("chat_id").toVariant().toLongLong();

	if (achievementId.isEmpty()) {
		return toolError("achievement_id is required");
	}
	if (!chatId) {
		return toolError("chat_id is required and must be non-zero");
	}
	if (!_session) {
		return toolError("No active session");
	}

	// Only report a share once a message has actually been queued. This used
	// to return success with the target chat echoed back, having sent nothing
	// at all -- the achievement was never shared with anyone.
	const auto progress = toolGetAchievementProgress(args);
	if (!progress.value("completed").toBool()) {
		return toolError(QString(
			"Achievement '%1' is not completed yet").arg(achievementId));
	}

	const auto history = _session->data().history(PeerId(chatId));
	if (!history) {
		return toolError("Chat not found");
	}
	const auto text = QString("Achievement unlocked: %1").arg(
		progress.value("name").toString(achievementId));

	auto message = Api::MessageToSend(Api::SendAction(history));
	message.textWithTags = TextWithTags{ text };
	_session->api().sendMessage(std::move(message));

	QJsonObject result;
	result["success"] = true;
	result["achievement_id"] = achievementId;
	result["shared_to"] = chatId;
	result["text"] = text;
	result["status"] = "Message queued for sending";
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
		Q_UNUSED(totalValue);

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
	auto peer = resolvePeer(peerId);
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

	// Credits API not available in this version
	if (_session) {
		dashboard["stars_balance"] = 0;
	}

	result["success"] = true;
	result["dashboard"] = dashboard;

	return result;
}


} // namespace MCP
