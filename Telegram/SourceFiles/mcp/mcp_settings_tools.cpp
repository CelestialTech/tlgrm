// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ============================================================================
// Privacy helper functions
// ============================================================================

namespace {

QString privacyOptionToString(Api::UserPrivacy::Option option) {
	using Option = Api::UserPrivacy::Option;
	switch (option) {
	case Option::Everyone: return "everybody";
	case Option::Contacts: return "contacts";
	case Option::CloseFriends: return "close_friends";
	case Option::Nobody: return "nobody";
	}
	return "unknown";
}

Api::UserPrivacy::Option stringToPrivacyOption(const QString &str) {
	using Option = Api::UserPrivacy::Option;
	if (str == "everybody" || str == "everyone") return Option::Everyone;
	if (str == "contacts") return Option::Contacts;
	if (str == "close_friends") return Option::CloseFriends;
	if (str == "nobody") return Option::Nobody;
	return Option::Nobody; // Default to most restrictive
}

} // namespace

// ============================================================================
// PROFILE, PRIVACY, AND SECURITY SETTINGS IMPLEMENTATIONS
// These use real Telegram API integration
// ============================================================================

QJsonObject Server::toolGetProfileSettings(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	const auto user = _session->user();
	if (!user) {
		return QJsonObject{
			{"error", "User data not available"},
			{"status", "error"},
		};
	}

	// Get birthday info
	const auto birthday = user->birthday();
	QJsonObject birthdayObj;
	if (birthday) {
		birthdayObj["day"] = birthday.day();
		birthdayObj["month"] = birthday.month();
		if (birthday.year()) {
			birthdayObj["year"] = birthday.year();
		}
	}

	return QJsonObject{
		{"first_name", user->firstName},
		{"last_name", user->lastName},
		{"username", user->username()},
		{"phone", user->phone()},
		{"bio", user->about()},
		{"birthday", birthdayObj},
		{"is_premium", user->isPremium()},
		{"status", "success"},
	};
}

QJsonObject Server::toolUpdateProfileName(const QJsonObject &args) {
	QString firstName = args["first_name"].toString();
	QString lastName = args["last_name"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	if (firstName.isEmpty()) {
		return QJsonObject{
			{"error", "First name is required"},
			{"status", "error"},
		};
	}

	// Send MTP request to update profile name
	auto flags = MTPaccount_UpdateProfile::Flag::f_first_name
		| MTPaccount_UpdateProfile::Flag::f_last_name;

	_session->api().request(MTPaccount_UpdateProfile(
		MTP_flags(flags),
		MTP_string(firstName),
		MTP_string(lastName),
		MTPstring()
	)).done([session = _session](const MTPUser &user) {
		session->data().processUser(user);
	}).fail([](const MTP::Error &error) {
		qWarning() << "[MCP] Profile name update failed:" << error.type();
	}).send();

	return QJsonObject{
		{"first_name", firstName},
		{"last_name", lastName},
		{"status", "success"},
		{"note", "Profile name update request sent. Changes will appear shortly."},
	};
}

QJsonObject Server::toolUpdateProfileBio(const QJsonObject &args) {
	QString bio = args["bio"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Use the API to save bio
	_session->api().saveSelfBio(bio);

	return QJsonObject{
		{"bio", bio},
		{"status", "success"},
		{"note", "Bio update initiated"},
	};
}

QJsonObject Server::toolUpdateProfileUsername(const QJsonObject &args) {
	const auto username = args["username"].toString().trimmed();
	if (!_session) {
		return toolError("No active session");
	}
	if (username.isEmpty()) {
		return toolError("username is required (pass an empty_ok flag is not "
			"supported; use the app to clear a username)");
	}

	// account.updateUsername needs no verification step -- the earlier claim
	// that this "requires interactive verification" confused it with changing
	// a phone number, which does. Telegram validates the name and answers
	// with the updated user, or with USERNAME_OCCUPIED / USERNAME_INVALID /
	// USERNAME_PURCHASE_AVAILABLE, all of which are worth passing through
	// verbatim rather than flattening into one failure.
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPaccount_UpdateUsername(
			MTP_string(username)
		)).done([=](const MTPUser &result) {
			QJsonObject value;
			value["success"] = true;
			value["username"] = username;
			result.match([&](const MTPDuser &data) {
				value["user_id"] = qint64(data.vid().v);
			}, [](const MTPDuserEmpty &) {});
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("account.updateUsername failed: " + error.type());
		}).send();
	});
}

QJsonObject Server::toolUpdateProfilePhone(const QJsonObject &args) {
	const auto phone = args["phone"].toString().trimmed();
	const auto codeHash = args.value("phone_code_hash").toString();
	const auto code = args.value("code").toString();

	if (!_session) {
		return toolError("No active session");
	}
	if (phone.isEmpty()) {
		return toolError("phone is required, in international format");
	}

	// Changing a number genuinely needs a confirmation code, so this is two
	// calls rather than one: the first sends the code and returns the hash,
	// the second passes the hash back with the code the user received. The
	// previous single-shot version simply declared the whole thing
	// unsupported.
	if (code.isEmpty() || codeHash.isEmpty()) {
		return awaitMtp([&](auto done, auto fail) {
			_session->api().request(MTPaccount_SendChangePhoneCode(
				MTP_string(phone),
				MTP_codeSettings(
					MTP_flags(0),
					MTPVector<MTPbytes>(),
					MTPstring(),
					MTPBool())
			)).done([=](const MTPauth_SentCode &result) {
				QJsonObject value;
				result.match([&](const MTPDauth_sentCode &data) {
					value["success"] = true;
					value["phone"] = phone;
					value["phone_code_hash"] = qs(data.vphone_code_hash());
					value["status"] = "code_sent";
					value["next_step"] = "Call update_profile_phone again with the "
						"same phone, this phone_code_hash, and the code received.";
				}, [&](const auto &) {
					value = toolError("Telegram answered with an unsupported "
						"sent-code type for this flow");
				});
				done(value);
			}).fail([=](const MTP::Error &error) {
				fail("account.sendChangePhoneCode failed: " + error.type());
			}).send();
		});
	}

	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPaccount_ChangePhone(
			MTP_string(phone),
			MTP_string(codeHash),
			MTP_string(code)
		)).done([=](const MTPUser &result) {
			QJsonObject value;
			value["success"] = true;
			value["phone"] = phone;
			value["status"] = "changed";
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("account.changePhone failed: " + error.type());
		}).send();
	});
}

QJsonObject Server::toolGetPrivacySettings(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Request reload of all privacy settings
	auto &privacy = _session->api().userPrivacy();

	// Reload all relevant privacy keys
	privacy.reload(Api::UserPrivacy::Key::LastSeen);
	privacy.reload(Api::UserPrivacy::Key::ProfilePhoto);
	privacy.reload(Api::UserPrivacy::Key::PhoneNumber);
	privacy.reload(Api::UserPrivacy::Key::Forwards);
	privacy.reload(Api::UserPrivacy::Key::Birthday);
	privacy.reload(Api::UserPrivacy::Key::About);
	privacy.reload(Api::UserPrivacy::Key::Calls);
	privacy.reload(Api::UserPrivacy::Key::Invites);

	// Read current cached privacy values where available
	QJsonObject settings;
	auto &privacyApi = _session->api().userPrivacy();

	// Helper to read a privacy key's current cached value
	auto readKey = [&](Api::UserPrivacy::Key key, const QString &name) {
		// Trigger reload to ensure fresh data
		privacyApi.reload(key);
	};

	// Trigger reloads for all keys
	readKey(Api::UserPrivacy::Key::LastSeen, "last_seen");
	readKey(Api::UserPrivacy::Key::ProfilePhoto, "profile_photo");
	readKey(Api::UserPrivacy::Key::PhoneNumber, "phone_number");
	readKey(Api::UserPrivacy::Key::Forwards, "forwards");
	readKey(Api::UserPrivacy::Key::Birthday, "birthday");
	readKey(Api::UserPrivacy::Key::About, "about");
	readKey(Api::UserPrivacy::Key::Calls, "calls");
	readKey(Api::UserPrivacy::Key::Invites, "invites");

	return QJsonObject{
		{"status", "success"},
		{"note", "Privacy settings reload initiated. Use individual update_*_privacy tools "
			"to modify settings. Values are loaded asynchronously by Telegram."},
		{"available_keys", QJsonArray{
			"last_seen", "profile_photo", "phone_number", "forwards",
			"birthday", "about", "calls", "invites"
		}},
		{"usage", "Use update_last_seen_privacy, update_profile_photo_privacy, etc. "
			"with rule: everybody/contacts/close_friends/nobody"},
	};
}

QJsonObject Server::toolUpdateLastSeenPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::LastSeen,
		privacyRule
	);

	return QJsonObject{
		{"setting", "last_seen"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Last seen privacy update initiated"},
	};
}

QJsonObject Server::toolUpdateProfilePhotoPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::ProfilePhoto,
		privacyRule
	);

	return QJsonObject{
		{"setting", "profile_photo"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Profile photo privacy update initiated"},
	};
}

QJsonObject Server::toolUpdatePhoneNumberPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::PhoneNumber,
		privacyRule
	);

	return QJsonObject{
		{"setting", "phone_number"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Phone number privacy update initiated"},
	};
}

QJsonObject Server::toolUpdateForwardsPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::Forwards,
		privacyRule
	);

	return QJsonObject{
		{"setting", "forwards"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Forwards privacy update initiated"},
	};
}

QJsonObject Server::toolUpdateBirthdayPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::Birthday,
		privacyRule
	);

	return QJsonObject{
		{"setting", "birthday"},
		{"rule", rule},
		{"status", "success"},
		{"note", "Birthday privacy update initiated"},
	};
}

QJsonObject Server::toolUpdateAboutPrivacy(const QJsonObject &args) {
	QString rule = args["rule"].toString();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	Api::UserPrivacy::Rule privacyRule;
	privacyRule.option = stringToPrivacyOption(rule);

	_session->api().userPrivacy().save(
		Api::UserPrivacy::Key::About,
		privacyRule
	);

	return QJsonObject{
		{"setting", "about"},
		{"rule", rule},
		{"status", "success"},
		{"note", "About privacy update initiated"},
	};
}

QJsonObject Server::toolGetBlockedUsers(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Trigger reload to get fresh data
	_session->api().blockedPeers().reload();

	// Also query any locally cached blocked user info from our DB
	QJsonArray blockedArray;
	if (_db.isOpen()) {
		QSqlQuery query(_db);
		if (query.exec("SELECT user_id, username, blocked_at FROM blocked_users ORDER BY blocked_at DESC")) {
			while (query.next()) {
				QJsonObject blocked;
				blocked["user_id"] = query.value(0).toLongLong();
				blocked["username"] = query.value(1).toString();
				blocked["blocked_at"] = QDateTime::fromSecsSinceEpoch(
					query.value(2).toLongLong()).toString(Qt::ISODate);
				blockedArray.append(blocked);
			}
		}
	}

	return QJsonObject{
		{"status", "success"},
		{"blocked_users", blockedArray},
		{"count", blockedArray.size()},
		{"note", "Blocked list reload initiated. Cached data shown. "
			"Use block_user/unblock_user tools to manage blocked users."},
	};
}

QJsonObject Server::toolGetSecuritySettings(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Reload self-destruct settings to get auto-delete period
	_session->api().selfDestruct().reload();

	// Get current auto-delete period
	const auto ttl = _session->api().selfDestruct().periodDefaultHistoryTTLCurrent();

	return QJsonObject{
		{"auto_delete_period_seconds", ttl},
		{"status", "success"},
		{"note", "Security settings retrieved. 2FA status requires async API call."},
	};
}

QJsonObject Server::toolGetActiveSessions(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Reload authorizations
	_session->api().authorizations().reload();

	// Get current list (may be empty if not yet loaded)
	const auto list = _session->api().authorizations().list();

	QJsonArray sessions;
	for (const auto &entry : list) {
		QJsonObject sessionObj;
		sessionObj["hash"] = QString::number(entry.hash);
		sessionObj["name"] = entry.name;
		sessionObj["platform"] = entry.platform;
		sessionObj["system"] = entry.system;
		sessionObj["info"] = entry.info;
		sessionObj["ip"] = entry.ip;
		sessionObj["location"] = entry.location;
		sessionObj["active"] = entry.active;
		sessionObj["is_current"] = (entry.hash == 0);
		sessions.append(sessionObj);
	}

	return QJsonObject{
		{"sessions", sessions},
		{"total", _session->api().authorizations().total()},
		{"status", "success"},
	};
}

QJsonObject Server::toolTerminateSession(const QJsonObject &args) {
	qint64 hash = args["hash"].toVariant().toLongLong();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	if (hash == 0) {
		return QJsonObject{
			{"error", "Cannot terminate current session"},
			{"status", "error"},
		};
	}

	// Request session termination
	_session->api().authorizations().requestTerminate(
		[](const MTPBool &) { /* success */ },
		[](const MTP::Error &) { /* fail */ },
		static_cast<uint64>(hash)
	);

	return QJsonObject{
		{"session_hash", QString::number(hash)},
		{"status", "initiated"},
		{"note", "Session termination request sent"},
	};
}

QJsonObject Server::toolUpdateAutoDeletePeriod(const QJsonObject &args) {
	int period = args["period"].toInt();

	if (!_session) {
		return QJsonObject{
			{"error", "No active session"},
			{"status", "error"},
		};
	}

	// Validate period (must be 0, 86400, 604800, or 2592000)
	if (period != 0 && period != 86400 && period != 604800 && period != 2592000) {
		return QJsonObject{
			{"error", "Invalid period. Must be 0 (disabled), 86400 (1 day), 604800 (1 week), or 2592000 (1 month)"},
			{"period", period},
			{"status", "error"},
		};
	}

	// Update auto-delete period
	_session->api().selfDestruct().updateDefaultHistoryTTL(period);

	return QJsonObject{
		{"period", period},
		{"period_description", period == 0 ? "disabled" : (period == 86400 ? "1 day" : (period == 604800 ? "1 week" : "1 month"))},
		{"status", "success"},
		{"note", "Auto-delete period update initiated"},
	};
}

// ============================================================================
// TRANSLATION TOOLS - delegate to mcp_premium_tools.cpp implementations
// ============================================================================

QJsonObject Server::toolGetTranslationLanguages(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonArray languages;
	// Common Telegram-supported languages
	const char* langCodes[] = {
		"en", "es", "fr", "de", "it", "pt", "ru", "zh", "ja", "ko",
		"ar", "hi", "tr", "pl", "nl", "uk", "cs", "sv", "da", "fi",
		"no", "hu", "ro", "bg", "hr", "sk", "sl", "lt", "lv", "et"
	};
	const char* langNames[] = {
		"English", "Spanish", "French", "German", "Italian", "Portuguese",
		"Russian", "Chinese", "Japanese", "Korean", "Arabic", "Hindi",
		"Turkish", "Polish", "Dutch", "Ukrainian", "Czech", "Swedish",
		"Danish", "Finnish", "Norwegian", "Hungarian", "Romanian",
		"Bulgarian", "Croatian", "Slovak", "Slovenian", "Lithuanian",
		"Latvian", "Estonian"
	};
	for (int i = 0; i < 30; ++i) {
		QJsonObject lang;
		lang["code"] = QString::fromLatin1(langCodes[i]);
		lang["name"] = QString::fromLatin1(langNames[i]);
		languages.append(lang);
	}
	QJsonObject result;
	result["success"] = true;
	result["languages"] = languages;
	result["count"] = languages.size();
	return result;
}

QJsonObject Server::toolAutoTranslateChat(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString targetLanguage = args["target_language"].toString();
	bool enabled = args.value("enabled").toBool(true);

	if (targetLanguage.isEmpty()) {
		QJsonObject result;
		result["error"] = "Missing target_language parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO auto_translate_config (chat_id, target_language, enabled, updated_at) "
				  "VALUES (?, ?, ?, datetime('now'))");
	query.addBindValue(chatId);
	query.addBindValue(targetLanguage);
	query.addBindValue(enabled);

	QJsonObject result;
	if (query.exec()) {
		result["success"] = true;
		result["chat_id"] = chatId;
		result["target_language"] = targetLanguage;
		result["enabled"] = enabled;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save auto-translate config";
	}
	return result;
}

QJsonObject Server::toolTranslateMessages(const QJsonObject &args) {
	// Delegate to the translate_message implementation in premium tools
	return toolTranslateMessage(args);
}

// ============================================================================
// VOICE/VIDEO TOOLS - delegate or provide local tracking
// ============================================================================

QJsonObject Server::toolGenerateVoiceMessage(const QJsonObject &args) {
	// Delegate to TTS tool in business tools
	return toolTextToSpeech(args);
}

QJsonObject Server::toolListVoicePresets(const QJsonObject &args) {
	// Delegate to voice personas in business tools
	return toolListVoicePersonas(args);
}

QJsonObject Server::toolGetTranscriptionStatus(const QJsonObject &args) {
	QString transcriptionId = args["transcription_id"].toString();
	qint64 messageId = args.value("message_id").toVariant().toLongLong();

	QJsonObject result;
	if (_voiceTranscription && messageId > 0) {
		auto stored = _voiceTranscription->getStoredTranscription(messageId);
		if (stored.success) {
			result["success"] = true;
			result["status"] = "completed";
			result["text"] = stored.text;
			result["language"] = stored.language;
			result["confidence"] = stored.confidence;
			return result;
		}
	}

	result["success"] = true;
	result["transcription_id"] = transcriptionId;
	result["status"] = "pending";
	result["note"] = "Transcription not yet available";
	return result;
}

QJsonObject Server::toolGenerateVideoCircle(const QJsonObject &args) {
	// Delegate to video tool in business tools
	return toolTextToVideo(args);
}

QJsonObject Server::toolConfigureVideoAvatar(const QJsonObject &args) {
	// Delegate to avatar upload in business tools
	return toolUploadAvatarSource(args);
}

// ============================================================================
// AI CHATBOT TOOLS - delegate to business tools implementations
// ============================================================================

QJsonObject Server::toolConfigureAiChatbot(const QJsonObject &args) {
	return toolConfigureChatbot(args);
}

QJsonObject Server::toolResumeChatbot(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("UPDATE chatbot_config SET enabled = 1 WHERE id = 1");

	QJsonObject result;
	if (query.exec()) {
		result["success"] = true;
		result["enabled"] = true;
	} else {
		result["success"] = false;
		result["error"] = "No chatbot configured to resume";
	}
	return result;
}

QJsonObject Server::toolGetChatbotStats(const QJsonObject &args) {
	return toolGetChatbotAnalytics(args);
}

QJsonObject Server::toolSetChatbotPrompt(const QJsonObject &args) {
	QString prompt = args["prompt"].toString();
	if (prompt.isEmpty()) {
		QJsonObject result;
		result["error"] = "Missing prompt parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("UPDATE chatbot_config SET personality = ? WHERE id = 1");
	query.addBindValue(prompt);

	QJsonObject result;
	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["prompt"] = prompt;
	} else {
		result["success"] = false;
		result["error"] = "No chatbot configured - configure one first";
	}
	return result;
}

// ============================================================================
// GREETING TOOLS - delegate to business tools implementations
// ============================================================================

QJsonObject Server::toolConfigureGreeting(const QJsonObject &args) {
	return toolSetGreetingMessage(args);
}

QJsonObject Server::toolGetGreetingConfig(const QJsonObject &args) {
	return toolGetGreetingMessage(args);
}

QJsonObject Server::toolGetGreetingStats(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT greetings_sent, updated_at FROM greeting_config WHERE id = 1");

	QJsonObject result;
	if (query.exec() && query.next()) {
		result["greetings_sent"] = query.value(0).toInt();
		result["last_updated"] = query.value(1).toString();
		result["success"] = true;
	} else {
		result["greetings_sent"] = 0;
		result["success"] = true;
		result["note"] = "No greeting configured";
	}
	return result;
}

// ============================================================================
// AWAY MESSAGE TOOLS - delegate to business tools implementations
// ============================================================================

QJsonObject Server::toolConfigureAwayMessage(const QJsonObject &args) {
	return toolSetAwayMessage(args);
}

// ============================================================================
// QUICK REPLY TOOLS - delegate to business tools implementations
// ============================================================================

QJsonObject Server::toolSendQuickReply(const QJsonObject &args) {
	return toolUseQuickReply(args);
}

QJsonObject Server::toolEditQuickReply(const QJsonObject &args) {
	return toolUpdateQuickReply(args);
}

// ============================================================================
// BUSINESS LOCATION - local SQLite storage
// ============================================================================

QJsonObject Server::toolSetBusinessLocation(const QJsonObject &args) {
	QString address = args["address"].toString();
	double latitude = args.value("latitude").toDouble(0);
	double longitude = args.value("longitude").toDouble(0);

	if (address.isEmpty()) {
		QJsonObject result;
		result["error"] = "Missing address parameter";
		result["success"] = false;
		return result;
	}

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO business_location (id, address, latitude, longitude, updated_at) "
				  "VALUES (1, ?, ?, ?, datetime('now'))");
	query.addBindValue(address);
	query.addBindValue(latitude);
	query.addBindValue(longitude);

	QJsonObject result;
	if (query.exec()) {
		result["success"] = true;
		result["address"] = address;
		result["latitude"] = latitude;
		result["longitude"] = longitude;
	} else {
		result["success"] = false;
		result["error"] = "Failed to save business location";
	}
	return result;
}

QJsonObject Server::toolGetBusinessLocation(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT address, latitude, longitude, updated_at FROM business_location WHERE id = 1");

	QJsonObject result;
	if (query.exec() && query.next()) {
		result["address"] = query.value(0).toString();
		result["latitude"] = query.value(1).toDouble();
		result["longitude"] = query.value(2).toDouble();
		result["updated_at"] = query.value(3).toString();
		result["success"] = true;
	} else {
		result["success"] = true;
		result["note"] = "No business location configured";
	}
	return result;
}

// ============================================================================
// CHAT RULES - delegate to premium tools implementations
// ============================================================================

QJsonObject Server::toolCreateChatRule(const QJsonObject &args) {
	return toolSetChatRules(args);
}

QJsonObject Server::toolListChatRules(const QJsonObject &args) {
	return toolGetChatRules(args);
}

QJsonObject Server::toolDeleteChatRule(const QJsonObject &args) {
	qint64 ruleId = args["rule_id"].toVariant().toLongLong();
	QString ruleName = args.value("rule_name").toString();

	QSqlQuery query(_db);
	if (ruleId > 0) {
		query.prepare("DELETE FROM chat_rules WHERE id = ?");
		query.addBindValue(ruleId);
	} else if (!ruleName.isEmpty()) {
		query.prepare("DELETE FROM chat_rules WHERE rule_name = ?");
		query.addBindValue(ruleName);
	} else {
		QJsonObject result;
		result["error"] = "Provide rule_id or rule_name";
		result["success"] = false;
		return result;
	}

	QJsonObject result;
	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["deleted"] = true;
	} else {
		result["success"] = false;
		result["error"] = "Rule not found";
	}
	return result;
}

QJsonObject Server::toolExecuteChatRules(const QJsonObject &args) {
	return toolTestChatRules(args);
}

// ============================================================================
// TAG TOOLS - delegate to premium tools implementations
// ============================================================================

QJsonObject Server::toolGetTaggedMessages(const QJsonObject &args) {
	QString tag = args["tag"].toString();
	if (tag.isEmpty()) {
		return toolGetMessageTags(args);
	}
	return toolSearchByTag(args);
}

// ============================================================================
// PAID MESSAGES - local SQLite storage
// ============================================================================

QJsonObject Server::toolConfigurePaidMessages(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int pricePerMessage = args["price"].toInt();
	bool enabled = args.value("enabled").toBool(true);

	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO paid_message_config (chat_id, price, enabled, updated_at) "
				  "VALUES (?, ?, ?, datetime('now'))");
	query.addBindValue(chatId);
	query.addBindValue(pricePerMessage);
	query.addBindValue(enabled);

	QJsonObject result;
	if (query.exec()) {
		result["success"] = true;
		result["chat_id"] = chatId;
		result["price"] = pricePerMessage;
		result["enabled"] = enabled;
	} else {
		result["success"] = false;
		result["error"] = "Failed to configure paid messages";
	}
	return result;
}

QJsonObject Server::toolGetPaidMessagesStats(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT SUM(price * unlocks), SUM(unlocks), COUNT(*) FROM paid_content");

	QJsonObject result;
	if (query.exec() && query.next()) {
		result["total_revenue"] = query.value(0).toInt();
		result["total_unlocks"] = query.value(1).toInt();
		result["total_posts"] = query.value(2).toInt();
		result["success"] = true;
	} else {
		result["total_revenue"] = 0;
		result["total_unlocks"] = 0;
		result["success"] = true;
	}
	return result;
}

// ============================================================================
// AD FILTER - delegate to premium tools
// ============================================================================

QJsonObject Server::toolGetFilteredAds(const QJsonObject &args) {
	return toolGetAdFilterStats(args);
}

// ============================================================================
// MINIAPP TOOLS - delegate to wallet tools
// ============================================================================

QJsonObject Server::toolGetMiniappHistory(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT miniapp_id, approved_amount, spent_amount, created_at FROM miniapp_budgets ORDER BY created_at DESC");

	QJsonArray history;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject entry;
			entry["miniapp_id"] = query.value(0).toString();
			entry["approved_amount"] = query.value(1).toDouble();
			entry["spent_amount"] = query.value(2).toDouble();
			entry["created_at"] = query.value(3).toString();
			history.append(entry);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["history"] = history;
	result["count"] = history.size();
	return result;
}

QJsonObject Server::toolGetMiniappSpending(const QJsonObject &args) {
	QString miniappId = args.value("miniapp_id").toString();

	QSqlQuery query(_db);
	if (!miniappId.isEmpty()) {
		query.prepare("SELECT miniapp_id, approved_amount, spent_amount FROM miniapp_budgets WHERE miniapp_id = ?");
		query.addBindValue(miniappId);
	} else {
		query.prepare("SELECT miniapp_id, SUM(approved_amount), SUM(spent_amount) FROM miniapp_budgets GROUP BY miniapp_id");
	}

	QJsonObject spending;
	double totalSpent = 0;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject entry;
			entry["approved"] = query.value(1).toDouble();
			entry["spent"] = query.value(2).toDouble();
			spending[query.value(0).toString()] = entry;
			totalSpent += query.value(2).toDouble();
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["spending"] = spending;
	result["total_spent"] = totalSpent;
	return result;
}

QJsonObject Server::toolSetMiniappBudget(const QJsonObject &args) {
	return toolApproveMiniappSpend(args);
}

// ============================================================================
// TRANSACTION TOOLS - delegate to wallet tools
// ============================================================================

QJsonObject Server::toolSearchTransactions(const QJsonObject &args) {
	QString searchQuery = args["query"].toString();
	QString category = args.value("category").toString();
	int limit = args.value("limit").toInt(50);

	QSqlQuery query(_db);
	QString sql = "SELECT id, date, amount, category, description FROM wallet_spending WHERE 1=1 ";
	if (!searchQuery.isEmpty()) {
		sql += "AND description LIKE ? ";
	}
	if (!category.isEmpty()) {
		sql += "AND category = ? ";
	}
	sql += "ORDER BY date DESC LIMIT ?";

	query.prepare(sql);
	if (!searchQuery.isEmpty()) query.addBindValue("%" + searchQuery + "%");
	if (!category.isEmpty()) query.addBindValue(category);
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
			transactions.append(tx);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["transactions"] = transactions;
	result["count"] = transactions.size();
	return result;
}

QJsonObject Server::toolGetTopupOptions(const QJsonObject &args) {
	Q_UNUSED(args);
	if (!_session) {
		return toolError("No active session");
	}
	// payments.getStarsTopupOptions returns the real purchasable amounts.
	// This previously returned a hardcoded list, which went stale silently
	// whenever Telegram changed its pricing.
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetStarsTopupOptions(
		)).done([=](const MTPVector<MTPStarsTopupOption> &result) {
			QJsonArray options;
			for (const auto &option : result.v) {
				const auto &data = option.data();
				QJsonObject entry;
				entry["stars"] = qint64(data.vstars().v);
				entry["currency"] = qs(data.vcurrency());
				entry["amount"] = qint64(data.vamount().v);
				entry["extended"] = data.is_extended();
				if (const auto product = data.vstore_product()) {
					entry["store_product"] = qs(*product);
				}
				options.append(entry);
			}
			QJsonObject value;
			value["success"] = true;
			value["options"] = options;
			value["count"] = options.size();
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("payments.getStarsTopupOptions failed: " + error.type());
		}).send();
	});
}

// ============================================================================
// WALLET BUDGET/ALERT TOOLS - delegate to wallet tools
// ============================================================================

QJsonObject Server::toolSetWalletBudget(const QJsonObject &args) {
	return toolSetSpendingBudget(args);
}

QJsonObject Server::toolConfigureWalletAlerts(const QJsonObject &args) {
	return toolSetBudgetAlert(args);
}

QJsonObject Server::toolGetWithdrawalStatus(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT balance, last_updated FROM wallet_budgets WHERE id = 1");

	QJsonObject result;
	result["success"] = true;
	if (query.exec() && query.next()) {
		result["balance"] = query.value(0).toDouble();
		result["last_updated"] = query.value(1).toString();
		result["withdrawal_available"] = query.value(0).toDouble() > 0;
	} else {
		result["balance"] = 0;
		result["withdrawal_available"] = false;
	}
	result["note"] = "Withdrawals processed via Telegram Stars/Fragment";
	return result;
}

QJsonObject Server::toolCreateCryptoPayment(const QJsonObject &args) {
	double amount = args["amount"].toDouble();
	QString currency = args.value("currency").toString("TON");
	QString recipientAddress = args["recipient"].toString();
	QString comment = args.value("comment").toString();
	QString action = args.value("action").toString("send");

	QJsonObject result;
	if (recipientAddress.isEmpty() && action == "send") {
		result["error"] = "Missing recipient address";
		result["success"] = false;
		return result;
	}

	if (!_tonWallet) {
		result["error"] = "TonWallet not initialized";
		result["success"] = false;
		return result;
	}

	if (!_tonWallet->isRunning()) {
		result["error"] = "TonWallet not running (install: pip install tonsdk)";
		result["success"] = false;
		return result;
	}

	// Handle different actions
	if (action == "create_wallet") {
		auto walletResult = _tonWallet->createWallet();
		result["success"] = walletResult.success;
		if (walletResult.success) {
			result["address"] = walletResult.address;
			result["raw_address"] = walletResult.rawAddress;
			QJsonArray mnArray;
			for (const auto &word : walletResult.mnemonics) {
				mnArray.append(word);
			}
			result["mnemonics"] = mnArray;
			result["warning"] = "Save these 24 words securely. They cannot be recovered.";
		} else {
			result["error"] = walletResult.error;
		}
		return result;
	}

	if (action == "import_wallet") {
		auto mnemonicsStr = args.value("mnemonics").toString();
		if (mnemonicsStr.isEmpty()) {
			result["error"] = "Missing mnemonics (24 space-separated words)";
			result["success"] = false;
			return result;
		}
		auto words = mnemonicsStr.split(' ', Qt::SkipEmptyParts);
		auto walletResult = _tonWallet->importWallet(words);
		result["success"] = walletResult.success;
		if (walletResult.success) {
			result["address"] = walletResult.address;
			result["raw_address"] = walletResult.rawAddress;
			result["status"] = "imported";
		} else {
			result["error"] = walletResult.error;
		}
		return result;
	}

	if (action == "get_balance") {
		auto walletResult = _tonWallet->getBalance();
		result["success"] = walletResult.success;
		if (walletResult.success) {
			result["address"] = walletResult.address;
			result["balance_ton"] = walletResult.balanceTon;
			result["balance_nano"] = QString::number(walletResult.balanceNano);
			result["network"] = _tonWallet->network();
		} else {
			result["error"] = walletResult.error;
		}
		return result;
	}

	if (action == "get_address") {
		auto walletResult = _tonWallet->getWalletAddress();
		result["success"] = walletResult.success;
		if (walletResult.success) {
			result["address"] = walletResult.address;
			result["raw_address"] = walletResult.rawAddress;
			result["has_wallet"] = true;
		} else {
			result["error"] = walletResult.error;
			result["has_wallet"] = false;
		}
		return result;
	}

	if (action == "get_history") {
		int limit = args.value("limit").toInt(20);
		auto txs = _tonWallet->getTransactionHistory(limit);
		QJsonArray txArray;
		for (const auto &tx : txs) {
			QJsonObject txObj;
			txObj["hash"] = tx.hash;
			txObj["amount_ton"] = tx.amountTon;
			txObj["from"] = tx.from;
			txObj["to"] = tx.to;
			txObj["comment"] = tx.comment;
			txObj["timestamp"] = tx.timestamp.toString(Qt::ISODate);
			txObj["is_incoming"] = tx.isIncoming;
			txArray.append(txObj);
		}
		result["success"] = true;
		result["transactions"] = txArray;
		result["count"] = txArray.count();
		return result;
	}

	if (action == "get_jettons") {
		auto jettons = _tonWallet->getJettonBalances();
		result["success"] = true;
		result["jettons"] = jettons;
		return result;
	}

	if (action == "stats") {
		auto stats = _tonWallet->getStats();
		result["success"] = true;
		result["total_transactions"] = stats.totalTransactions;
		result["successful_transactions"] = stats.successfulTransactions;
		result["failed_transactions"] = stats.failedTransactions;
		result["total_sent_ton"] = stats.totalSentTon;
		result["total_received_ton"] = stats.totalReceivedTon;
		if (stats.lastTransaction.isValid()) {
			result["last_transaction"] = stats.lastTransaction.toString(Qt::ISODate);
		}
		result["network"] = _tonWallet->network();
		return result;
	}

	// Default action: send payment
	if (amount <= 0) {
		result["error"] = "Amount must be positive";
		result["success"] = false;
		return result;
	}

	if (currency.toUpper() != "TON") {
		result["error"] = "Only TON currency is supported";
		result["success"] = false;
		return result;
	}

	if (!_tonWallet->hasWallet()) {
		result["error"] = "No wallet configured. Use action='create_wallet' or action='import_wallet' first.";
		result["success"] = false;
		return result;
	}

	auto paymentResult = _tonWallet->sendPayment(
		recipientAddress, amount, comment);

	result["success"] = paymentResult.success;
	if (paymentResult.success) {
		result["status"] = paymentResult.status;
		result["tx_hash"] = paymentResult.txHash;
		result["amount"] = amount;
		result["currency"] = currency;
		result["recipient"] = recipientAddress;
		result["network"] = _tonWallet->network();
		if (!comment.isEmpty()) {
			result["comment"] = comment;
		}
	} else {
		result["error"] = paymentResult.error;
		result["status"] = paymentResult.status;
	}
	return result;
}

QJsonObject Server::toolGenerateFinancialReport(const QJsonObject &args) {
	QString period = args.value("period").toString("month");

	QJsonObject report;
	report["generated_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
	report["period"] = period;

	// Income
	QJsonObject incomeResult = toolGetIncomeAnalytics(args);
	report["total_income"] = incomeResult["total_income"];
	report["income_by_category"] = incomeResult["by_category"];

	// Spending
	QJsonObject spendingResult = toolGetSpendingAnalytics(args);
	report["total_spent"] = spendingResult["total_spent"];
	report["spending_by_category"] = spendingResult["by_category"];

	// Budget status
	QJsonObject budgetResult = toolGetBudgetStatus(QJsonObject());
	report["budget_status"] = budgetResult;

	QJsonObject result;
	result["success"] = true;
	result["report"] = report;
	return result;
}

// ============================================================================
// COLLECTIBLES/PORTFOLIO - delegate to stars tools
// ============================================================================

QJsonObject Server::toolGetCollectiblesPortfolio(const QJsonObject &args) {
	return toolGetPortfolio(args);
}

QJsonObject Server::toolGetCollectionDetails(const QJsonObject &args) {
	qint64 collectionId = args["collection_id"].toVariant().toLongLong();

	QSqlQuery query(_db);
	query.prepare("SELECT id, name, description, is_public, created_at FROM gift_collections WHERE id = ?");
	query.addBindValue(collectionId);

	QJsonObject result;
	if (query.exec() && query.next()) {
		result["id"] = query.value(0).toLongLong();
		result["name"] = query.value(1).toString();
		result["description"] = query.value(2).toString();
		result["is_public"] = query.value(3).toBool();
		result["created_at"] = query.value(4).toString();
		result["success"] = true;
	} else {
		result["success"] = false;
		result["error"] = "Collection not found";
	}
	return result;
}

QJsonObject Server::toolGetCollectionCompletion(const QJsonObject &args) {
	qint64 collectionId = args["collection_id"].toVariant().toLongLong();

	// Count items in collection
	QSqlQuery itemQuery(_db);
	itemQuery.prepare("SELECT COUNT(*) FROM collection_items WHERE collection_id = ?");
	itemQuery.addBindValue(collectionId);

	int totalItems = 0;
	if (itemQuery.exec() && itemQuery.next()) {
		totalItems = itemQuery.value(0).toInt();
	}

	// Count items that exist in portfolio (owned)
	QSqlQuery ownedQuery(_db);
	ownedQuery.prepare(
		"SELECT COUNT(*) FROM collection_items ci "
		"INNER JOIN portfolio p ON ci.gift_id = p.gift_type "
		"WHERE ci.collection_id = ? AND p.quantity > 0");
	ownedQuery.addBindValue(collectionId);

	int ownedItems = 0;
	if (ownedQuery.exec() && ownedQuery.next()) {
		ownedItems = ownedQuery.value(0).toInt();
	}

	double completion = totalItems > 0 ? (static_cast<double>(ownedItems) / totalItems * 100.0) : 0.0;

	QJsonObject result;
	result["success"] = true;
	result["collection_id"] = collectionId;
	result["total_items"] = totalItems;
	result["owned_items"] = ownedItems;
	result["completion_percentage"] = completion;
	return result;
}

// ============================================================================
// AUCTION TOOLS - delegate to stars tools
// ============================================================================

QJsonObject Server::toolListActiveAuctions(const QJsonObject &args) {
	return toolListAuctions(args);
}

QJsonObject Server::toolPlaceAuctionBid(const QJsonObject &args) {
	return toolPlaceBid(args);
}

QJsonObject Server::toolGetAuctionDetails(const QJsonObject &args) {
	return toolGetAuctionStatus(args);
}

QJsonObject Server::toolCreateAuctionAlert(const QJsonObject &args) {
	QString auctionId = args["auction_id"].toString();
	double priceThreshold = args.value("price_threshold").toDouble(0);

	QSqlQuery query(_db);
	query.prepare("INSERT INTO price_alerts (gift_type, target_price, direction, triggered, created_at) "
				  "VALUES (?, ?, 'below', 0, datetime('now'))");
	query.addBindValue("auction:" + auctionId);
	query.addBindValue(priceThreshold);

	QJsonObject result;
	if (query.exec()) {
		result["success"] = true;
		result["alert_id"] = query.lastInsertId().toLongLong();
		result["auction_id"] = auctionId;
		result["price_threshold"] = priceThreshold;
	} else {
		result["success"] = false;
		result["error"] = "Failed to create auction alert";
	}
	return result;
}

QJsonObject Server::toolGetAuctionAlerts(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT id, gift_type, target_price, direction, triggered, created_at FROM price_alerts "
				  "WHERE gift_type LIKE 'auction:%' ORDER BY created_at DESC");

	QJsonArray alerts;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject alert;
			alert["id"] = query.value(0).toLongLong();
			alert["auction_id"] = query.value(1).toString().mid(8); // Remove "auction:" prefix
			alert["price_threshold"] = query.value(2).toDouble();
			alert["direction"] = query.value(3).toString();
			alert["triggered"] = query.value(4).toBool();
			alert["created_at"] = query.value(5).toString();
			alerts.append(alert);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["alerts"] = alerts;
	result["count"] = alerts.size();
	return result;
}

// ============================================================================
// FRAGMENT/MARKETPLACE - delegate or local tracking
// ============================================================================

QJsonObject Server::toolGetFragmentListings(const QJsonObject &args) {
	return toolListMarketplace(args);
}

QJsonObject Server::toolUpdateListing(const QJsonObject &args) {
	const auto slug = args.value("slug").toString().trimmed();
	const auto msgId = args.value("msg_id").toInt();
	const auto price = qint64(args.value("price").toVariant().toLongLong());
	if (!_session) {
		return toolError("No active session");
	}
	if (slug.isEmpty() && !msgId) {
		return toolError("either slug or msg_id is required to identify the "
			"saved gift being relisted");
	}
	if (price < 0) {
		return toolError("price must not be negative");
	}

	// Sets the real resale price. This used to validate listing_id and then
	// return success with the note "Listing updates require marketplace API",
	// having changed nothing.
	const auto gift = slug.isEmpty()
		? MTP_inputSavedStarGiftUser(MTP_int(msgId))
		: MTP_inputSavedStarGiftSlug(MTP_string(slug));
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_UpdateStarGiftPrice(
			gift,
			StarsAmountToTL(CreditsAmount(price))
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			QJsonObject value;
			value["success"] = true;
			value["price"] = price;
			if (!slug.isEmpty()) value["slug"] = slug;
			if (msgId) value["msg_id"] = msgId;
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("payments.updateStarGiftPrice failed: " + error.type());
		}).send();
	});
}

QJsonObject Server::toolGetMarketTrends(const QJsonObject &args) {
	QString giftType = args.value("gift_type").toString();
	int days = args.value("days").toInt(7);

	QSqlQuery query(_db);
	query.prepare("SELECT date, AVG(price) as avg_price, COUNT(*) as volume "
				  "FROM price_history WHERE date >= date('now', '-' || ? || ' days') "
				  "GROUP BY date ORDER BY date");
	query.addBindValue(days);

	QJsonArray trends;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject point;
			point["date"] = query.value(0).toString();
			point["avg_price"] = query.value(1).toDouble();
			point["volume"] = query.value(2).toInt();
			trends.append(point);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["trends"] = trends;
	result["days"] = days;
	return result;
}

QJsonObject Server::toolCreatePriceAlert(const QJsonObject &args) {
	return toolSetPriceAlert(args);
}

QJsonObject Server::toolBacktestStrategy(const QJsonObject &args) {
	QString strategy = args["strategy"].toString();
	int days = args.value("days").toInt(30);
	double initialInvestment = args.value("initial_investment").toDouble(1000);
	QString giftType = args.value("gift_type").toString();

	// Fetch historical prices
	QSqlQuery query(_db);
	if (giftType.isEmpty()) {
		query.prepare("SELECT date, price, gift_type FROM price_history "
					  "WHERE date >= date('now', '-' || ? || ' days') "
					  "ORDER BY date ASC");
		query.addBindValue(days);
	} else {
		query.prepare("SELECT date, price, gift_type FROM price_history "
					  "WHERE gift_type = ? AND date >= date('now', '-' || ? || ' days') "
					  "ORDER BY date ASC");
		query.addBindValue(giftType);
		query.addBindValue(days);
	}

	QJsonObject result;
	result["strategy"] = strategy;
	result["days"] = days;
	result["initial_investment"] = initialInvestment;

	if (!query.exec()) {
		result["success"] = false;
		result["error"] = "Failed to query price history";
		return result;
	}

	QVector<double> prices;
	QJsonArray pricePoints;
	while (query.next()) {
		double price = query.value(1).toDouble();
		prices.append(price);
		QJsonObject pt;
		pt["date"] = query.value(0).toString();
		pt["price"] = price;
		pricePoints.append(pt);
	}

	if (prices.size() < 2) {
		result["success"] = true;
		result["final_value"] = initialInvestment;
		result["profit_loss"] = 0.0;
		result["trades"] = 0;
		result["note"] = "Insufficient price data for backtesting (need 2+ data points)";
		return result;
	}

	// Simulate strategies
	double portfolio = initialInvestment;
	double holdings = 0;
	int trades = 0;
	double maxDrawdown = 0;
	double peakValue = initialInvestment;

	if (strategy == "buy_and_hold") {
		holdings = initialInvestment / prices.first();
		portfolio = holdings * prices.last();
		trades = 1;
	} else if (strategy == "moving_average") {
		// Buy when price > 5-day MA, sell when below
		bool holding = false;
		double cash = initialInvestment;
		for (int i = 5; i < prices.size(); ++i) {
			double ma5 = 0;
			for (int j = i - 5; j < i; ++j) ma5 += prices[j];
			ma5 /= 5.0;

			if (!holding && prices[i] > ma5) {
				holdings = cash / prices[i];
				cash = 0;
				holding = true;
				trades++;
			} else if (holding && prices[i] < ma5) {
				cash = holdings * prices[i];
				holdings = 0;
				holding = false;
				trades++;
			}

			double currentValue = holding ? holdings * prices[i] : cash;
			if (currentValue > peakValue) peakValue = currentValue;
			double drawdown = (peakValue - currentValue) / peakValue;
			if (drawdown > maxDrawdown) maxDrawdown = drawdown;
		}
		portfolio = holdings > 0 ? holdings * prices.last() : cash;
	} else {
		// Default: buy_and_hold
		holdings = initialInvestment / prices.first();
		portfolio = holdings * prices.last();
		trades = 1;
	}

	result["success"] = true;
	result["final_value"] = portfolio;
	result["profit_loss"] = portfolio - initialInvestment;
	result["return_percent"] = (portfolio - initialInvestment) / initialInvestment * 100.0;
	result["trades"] = trades;
	result["max_drawdown_percent"] = maxDrawdown * 100.0;
	result["data_points"] = prices.size();
	result["price_range"] = pricePoints;
	return result;
}

// ============================================================================
// STAR REACTIONS - delegate to stars tools
// ============================================================================

QJsonObject Server::toolGetReactionStats(const QJsonObject &args) {
	return toolGetReactionAnalytics(args);
}

QJsonObject Server::toolGetStarReactionsReceived(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT chat_id, message_id, stars_count, created_at FROM star_reactions "
				  "ORDER BY created_at DESC LIMIT 100");

	QJsonArray reactions;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject r;
			r["chat_id"] = query.value(0).toLongLong();
			r["message_id"] = query.value(1).toLongLong();
			r["stars_count"] = query.value(2).toInt();
			r["created_at"] = query.value(3).toString();
			reactions.append(r);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["reactions"] = reactions;
	result["count"] = reactions.size();
	return result;
}

QJsonObject Server::toolGetStarReactionsSent(const QJsonObject &args) {
	Q_UNUSED(args);
	// Sent reactions tracked locally
	QSqlQuery query(_db);
	query.prepare("SELECT chat_id, message_id, stars_count, created_at FROM star_reactions "
				  "ORDER BY created_at DESC LIMIT 100");

	QJsonArray reactions;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject r;
			r["chat_id"] = query.value(0).toLongLong();
			r["message_id"] = query.value(1).toLongLong();
			r["stars_count"] = query.value(2).toInt();
			r["created_at"] = query.value(3).toString();
			reactions.append(r);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["reactions"] = reactions;
	result["count"] = reactions.size();
	return result;
}

QJsonObject Server::toolGetTopSupporters(const QJsonObject &args) {
	int limit = args.value("limit").toInt(10);

	// Aggregate from star_reactions by chat_id (content where reactions were received)
	// and from gift_transfers for gift-based support
	QJsonArray supporters;

	// Stars received per chat
	QSqlQuery query(_db);
	query.prepare("SELECT chat_id, SUM(stars_count) as total_stars, COUNT(*) as reaction_count "
				  "FROM star_reactions GROUP BY chat_id ORDER BY total_stars DESC LIMIT ?");
	query.addBindValue(limit);

	if (query.exec()) {
		int rank = 1;
		while (query.next()) {
			QJsonObject supporter;
			supporter["rank"] = rank++;
			supporter["chat_id"] = query.value(0).toLongLong();
			supporter["total_stars"] = query.value(1).toInt();
			supporter["reaction_count"] = query.value(2).toInt();
			supporter["source"] = "star_reactions";
			supporters.append(supporter);
		}
	}

	// Also include gift senders
	QSqlQuery giftQuery(_db);
	giftQuery.prepare("SELECT peer_id, SUM(stars_amount) as total_gifted, COUNT(*) as gift_count "
					  "FROM gift_transfers WHERE direction = 'received' "
					  "GROUP BY peer_id ORDER BY total_gifted DESC LIMIT ?");
	giftQuery.addBindValue(limit);

	if (giftQuery.exec()) {
		while (giftQuery.next()) {
			QJsonObject supporter;
			supporter["peer_id"] = giftQuery.value(0).toLongLong();
			supporter["total_gifted"] = giftQuery.value(1).toInt();
			supporter["gift_count"] = giftQuery.value(2).toInt();
			supporter["source"] = "gifts";
			supporters.append(supporter);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["supporters"] = supporters;
	result["count"] = supporters.size();
	return result;
}

// ============================================================================
// STAR RATING - local tracking
// ============================================================================

QJsonObject Server::toolGetStarRatingDetails(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT SUM(stars_count), COUNT(*) FROM star_reactions");

	QJsonObject result;
	if (query.exec() && query.next()) {
		int totalStars = query.value(0).toInt();
		int reactionCount = query.value(1).toInt();
		result["total_stars_received"] = totalStars;
		result["reaction_count"] = reactionCount;
		result["avg_stars_per_reaction"] = reactionCount > 0 ? (double)totalStars / reactionCount : 0;
	}
	result["success"] = true;
	return result;
}

QJsonObject Server::toolSimulateRatingChange(const QJsonObject &args) {
	int additionalStars = args["additional_stars"].toInt();
	int additionalReactions = args.value("additional_reactions").toInt(1);

	QSqlQuery query(_db);
	query.prepare("SELECT SUM(stars_count), COUNT(*) FROM star_reactions");

	QJsonObject result;
	if (query.exec() && query.next()) {
		int currentStars = query.value(0).toInt();
		int currentCount = query.value(1).toInt();
		int newStars = currentStars + additionalStars;
		int newCount = currentCount + additionalReactions;
		result["current_avg"] = currentCount > 0 ? (double)currentStars / currentCount : 0;
		result["simulated_avg"] = newCount > 0 ? (double)newStars / newCount : 0;
		result["change"] = (newCount > 0 ? (double)newStars / newCount : 0) - (currentCount > 0 ? (double)currentStars / currentCount : 0);
	}
	result["success"] = true;
	return result;
}

QJsonObject Server::toolGetRatingHistory(const QJsonObject &args) {
	int days = args.value("days").toInt(30);

	QSqlQuery query(_db);
	query.prepare("SELECT date(created_at) as day, SUM(stars_count), COUNT(*) "
				  "FROM star_reactions WHERE created_at >= date('now', '-' || ? || ' days') "
				  "GROUP BY day ORDER BY day");
	query.addBindValue(days);

	QJsonArray history;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject entry;
			entry["date"] = query.value(0).toString();
			entry["stars"] = query.value(1).toInt();
			entry["reactions"] = query.value(2).toInt();
			history.append(entry);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["history"] = history;
	result["days"] = days;
	return result;
}

// ============================================================================
// PROFILE GIFTS - local tracking
// ============================================================================

QJsonObject Server::toolGetProfileGifts(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT gift_type, quantity, avg_price FROM portfolio WHERE quantity > 0");

	QJsonArray gifts;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject gift;
			gift["gift_type"] = query.value(0).toString();
			gift["quantity"] = query.value(1).toInt();
			gift["value"] = query.value(2).toDouble();
			gifts.append(gift);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["gifts"] = gifts;
	result["count"] = gifts.size();
	return result;
}

QJsonObject Server::toolUpdateGiftDisplay(const QJsonObject &args) {
	const auto chatId = args.value("chat_id").toVariant().toLongLong();
	const auto msgIds = args.value("msg_ids").toArray();
	if (!_session) {
		return toolError("No active session");
	}
	if (msgIds.isEmpty()) {
		return toolError("msg_ids is required: pass the saved-gift message ids "
			"to pin, in the order they should appear");
	}
	const auto peer = chatId
		? _session->data().peerLoaded(PeerId(chatId))
		: _session->user().get();
	if (!peer) {
		return toolError("Chat not found");
	}

	auto gifts = QVector<MTPInputSavedStarGift>();
	gifts.reserve(msgIds.size());
	for (const auto &value : msgIds) {
		gifts.push_back(MTP_inputSavedStarGiftUser(MTP_int(value.toInt())));
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_ToggleStarGiftsPinnedToTop(
			peer->input(),
			MTP_vector<MTPInputSavedStarGift>(gifts)
		)).done([=](const MTPBool &result) {
			QJsonObject value;
			value["success"] = mtpIsTrue(result);
			value["pinned_count"] = msgIds.size();
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("payments.toggleStarGiftsPinnedToTop failed: " + error.type());
		}).send();
	});
}

QJsonObject Server::toolReorderProfileGifts(const QJsonObject &args) {
	const auto chatId = args.value("chat_id").toVariant().toLongLong();
	const auto order = args.value("collection_ids").toArray();
	if (!_session) {
		return toolError("No active session");
	}
	if (order.isEmpty()) {
		return toolError("collection_ids is required: Telegram reorders gift "
			"collections, not individual gifts. Use update_gift_display to pin "
			"specific gifts to the top.");
	}
	const auto peer = chatId
		? _session->data().peerLoaded(PeerId(chatId))
		: _session->user().get();
	if (!peer) {
		return toolError("Chat not found");
	}

	auto ids = QVector<MTPint>();
	ids.reserve(order.size());
	for (const auto &value : order) {
		ids.push_back(MTP_int(value.toInt()));
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_ReorderStarGiftCollections(
			peer->input(),
			MTP_vector<MTPint>(ids)
		)).done([=](const MTPBool &result) {
			QJsonObject value;
			value["success"] = mtpIsTrue(result);
			value["collection_count"] = order.size();
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("payments.reorderStarGiftCollections failed: " + error.type());
		}).send();
	});
}

QJsonObject Server::toolToggleGiftNotifications(const QJsonObject &args) {
	const auto chatId = args["chat_id"].toVariant().toLongLong();
	const auto enabled = args.value("enabled").toBool(true);
	if (!_session) {
		return toolError("No active session");
	}
	if (!chatId) {
		return toolError("chat_id is required and must be non-zero");
	}
	const auto peer = resolvePeer(chatId);
	const auto channel = peer ? peer->asChannel() : nullptr;
	if (!channel) {
		return toolError("Chat not found");
	}

	// Only broadcast channels have this setting. Telegram rejects a megagroup
	// with PEER_ID_INVALID, which reads as a malformed chat_id and sends the
	// caller looking at the wrong thing -- measured directly: the same call
	// succeeds on a broadcast channel and fails on a supergroup created
	// moments apart. canManageGifts() does not separate them, since it only
	// asks whether we can post.
	if (!channel->isBroadcast()) {
		return toolError(QString(
			"Chat %1 is a supergroup; gift notifications exist only for "
			"broadcast channels").arg(chatId));
	}
	if (!channel->canManageGifts()) {
		return toolError("This account cannot manage gifts for that channel");
	}

	using Flag = MTPpayments_ToggleChatStarGiftNotifications::Flag;
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_ToggleChatStarGiftNotifications(
			MTP_flags(enabled ? Flag::f_enabled : Flag(0)),
			channel->input()
		)).done([=](const MTPBool &result) {
			QJsonObject value;
			value["success"] = mtpIsTrue(result);
			value["chat_id"] = chatId;
			value["enabled"] = enabled;
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("payments.toggleChatStarGiftNotifications failed: "
				+ error.type());
		}).send();
	});
}

// ============================================================================
// GIFT INVESTMENT/ANALYTICS - delegate to stars tools
// ============================================================================

QJsonObject Server::toolGetGiftInvestmentAdvice(const QJsonObject &args) {
	Q_UNUSED(args);
	QJsonObject portfolioData = toolGetPortfolioValue(QJsonObject());

	QJsonObject result;
	result["success"] = true;
	result["portfolio_value"] = portfolioData["current_value"];
	result["profit_loss"] = portfolioData["profit_loss"];
	result["advice"] = "Monitor market trends and diversify gift types for optimal returns";
	return result;
}

QJsonObject Server::toolGetPortfolioPerformance(const QJsonObject &args) {
	return toolGetPortfolioValue(args);
}

// ============================================================================
// STAR GIFTS - delegate to stars tools or local tracking
// ============================================================================

QJsonObject Server::toolListStarGifts(const QJsonObject &args) {
	return toolListAvailableGifts(args);
}

QJsonObject Server::toolGetStarGiftDetails(const QJsonObject &args) {
	return toolGetGiftPriceHistory(args);
}

QJsonObject Server::toolBrowseGiftMarketplace(const QJsonObject &args) {
	return toolListMarketplace(args);
}

QJsonObject Server::toolGetGiftDetails(const QJsonObject &args) {
	const auto slug = args["slug"].toString().trimmed();
	if (!_session) {
		return toolError("No active session");
	}
	if (slug.isEmpty()) {
		return toolError("slug is required: unique gifts are addressed by slug, "
			"not by numeric id");
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetUniqueStarGift(
			MTP_string(slug)
		)).done([=](const MTPpayments_UniqueStarGift &result) {
			const auto &data = result.data();
			QJsonObject value;
			value["success"] = true;
			data.vgift().match([&](const MTPDstarGiftUnique &gift) {
				value["id"] = qint64(gift.vid().v);
				value["gift_id"] = qint64(gift.vgift_id().v);
				value["title"] = qs(gift.vtitle());
				value["slug"] = qs(gift.vslug());
				value["num"] = gift.vnum().v;
				value["availability_issued"] = gift.vavailability_issued().v;
				value["availability_total"] = gift.vavailability_total().v;
				value["attribute_count"] = gift.vattributes().v.size();
				if (const auto owner = gift.vowner_name()) {
					value["owner_name"] = qs(*owner);
				}
			}, [&](const MTPDstarGift &gift) {
				value["id"] = qint64(gift.vid().v);
				value["note"] = "slug resolved to a non-unique gift";
			});
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("payments.getUniqueStarGift failed: " + error.type());
		}).send();
	});
}

QJsonObject Server::toolGetUpgradeOptions(const QJsonObject &args) {
	const auto giftId = args["gift_id"].toVariant().toLongLong();
	if (!_session) {
		return toolError("No active session");
	}
	if (!giftId) {
		return toolError("gift_id is required and must be non-zero");
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetStarGiftUpgradePreview(
			MTP_long(giftId)
		)).done([=](const MTPpayments_StarGiftUpgradePreview &result) {
			const auto &data = result.data();
			QJsonArray prices;
			for (const auto &price : data.vprices().v) {
				const auto &pd = price.data();
				QJsonObject entry;
				entry["upgrade_stars"] = qint64(pd.vupgrade_stars().v);
				entry["date"] = pd.vdate().v;
				prices.append(entry);
			}
			QJsonObject value;
			value["success"] = true;
			value["gift_id"] = giftId;
			value["prices"] = prices;
			value["sample_attribute_count"] = data.vsample_attributes().v.size();
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("payments.getStarGiftUpgradePreview failed: " + error.type());
		}).send();
	});
}

QJsonObject Server::toolGetGiftTransferHistory(const QJsonObject &args) {
	int limit = args.value("limit").toInt(50);

	QSqlQuery query(_db);
	query.prepare("SELECT id, gift_id, direction, peer_id, stars_amount, created_at "
				  "FROM gift_transfers ORDER BY created_at DESC LIMIT ?");
	query.addBindValue(limit);

	QJsonArray transfers;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject transfer;
			transfer["id"] = query.value(0).toLongLong();
			transfer["gift_id"] = query.value(1).toString();
			transfer["direction"] = query.value(2).toString();
			transfer["peer_id"] = query.value(3).toLongLong();
			transfer["stars_amount"] = query.value(4).toInt();
			transfer["created_at"] = query.value(5).toString();
			transfers.append(transfer);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["transfers"] = transfers;
	result["count"] = transfers.size();
	return result;
}

QJsonObject Server::toolGetGiftAnalytics(const QJsonObject &args) {
	return toolGetReactionAnalytics(args);
}

QJsonObject Server::toolGetUniqueGiftAnalytics(const QJsonObject &args) {
	const auto giftType = args["gift_type"].toString().trimmed();
	if (giftType.isEmpty()) {
		return toolError("gift_type is required");
	}

	// Reports the requested gift type. It used to aggregate the whole
	// portfolio and return that under any gift_type the caller passed.
	QSqlQuery query(_db);
	query.prepare("SELECT quantity, avg_price, current_value "
		"FROM portfolio WHERE gift_type = ?");
	query.addBindValue(giftType);
	if (!query.exec()) {
		return toolError("Could not read portfolio: "
			+ query.lastError().text());
	}

	QJsonObject result;
	result["success"] = true;
	result["gift_type"] = giftType;
	if (query.next()) {
		result["quantity"] = query.value(0).toInt();
		result["avg_price"] = query.value(1).toDouble();
		result["current_value"] = query.value(2).toDouble();
		result["held"] = true;
	} else {
		result["held"] = false;
		result["note"] = "No holding recorded locally for this gift type";
	}
	return result;
}

// ============================================================================
// SUBSCRIPTION TOOLS - delegate to wallet tools
// ============================================================================

QJsonObject Server::toolGetSubscriptionAlerts(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT id, gift_type, target_price, direction, triggered, created_at FROM price_alerts "
				  "WHERE gift_type LIKE 'sub:%' ORDER BY created_at DESC");

	QJsonArray alerts;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject alert;
			alert["id"] = query.value(0).toLongLong();
			alert["subscription"] = query.value(1).toString().mid(4);
			alert["threshold"] = query.value(2).toDouble();
			alert["triggered"] = query.value(4).toBool();
			alert["created_at"] = query.value(5).toString();
			alerts.append(alert);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["alerts"] = alerts;
	return result;
}

QJsonObject Server::toolCancelSubscription(const QJsonObject &args) {
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
		using Flag = MTPpayments_ChangeStarsSubscription::Flag;
		_session->api().request(MTPpayments_ChangeStarsSubscription(
			MTP_flags(Flag::f_canceled),
			MTP_inputPeerSelf(),
			MTP_string(subscriptionId),
			MTP_bool(true)
		)).done([subscriptionId]() {
			qWarning() << "MCP: Subscription" << subscriptionId << "cancelled successfully";
		}).fail([subscriptionId](const MTP::Error &error) {
			qWarning() << "MCP: Failed to cancel subscription" << subscriptionId << ":" << error.type();
		}).send();

		result["success"] = true;
		result["subscription_id"] = subscriptionId;
		result["status"] = "cancellation_submitted";
		result["api_request"] = "submitted";
		result["note"] = "Cancellation request sent via MTPpayments_ChangeStarsSubscription";
	} else {
		result["success"] = true;
		result["channel_id"] = channelId;
		result["status"] = "need_subscription_id";
		result["note"] = "To cancel, provide subscription_id from list_subscriptions. "
						 "channel_id alone is not sufficient for cancellation.";
	}

	return result;
}

// ============================================================================
// PAID CONTENT - delegate to stars tools
// ============================================================================

QJsonObject Server::toolGetUnlockedContent(const QJsonObject &args) {
	return toolListPurchasedContent(args);
}

QJsonObject Server::toolGetPaidContentEarnings(const QJsonObject &args) {
	return toolGetPaidContentStats(args);
}

QJsonObject Server::toolGetPaidMediaStats(const QJsonObject &args) {
	return toolGetPaidContentStats(args);
}

// ============================================================================
// CHANNEL EARNINGS - local tracking
// ============================================================================

QJsonObject Server::toolGetChannelEarnings(const QJsonObject &args) {
	qint64 channelId = args["channel_id"].toVariant().toLongLong();

	QSqlQuery query(_db);
	query.prepare("SELECT SUM(amount) FROM wallet_spending WHERE peer_id = ? AND amount > 0");
	query.addBindValue(channelId);

	QJsonObject result;
	if (query.exec() && query.next()) {
		result["channel_id"] = channelId;
		result["total_earnings"] = query.value(0).toDouble();
	}
	result["success"] = true;
	return result;
}

QJsonObject Server::toolGetAllChannelsEarnings(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT peer_id, SUM(amount) as total FROM wallet_spending "
				  "WHERE amount > 0 AND peer_id IS NOT NULL "
				  "GROUP BY peer_id ORDER BY total DESC");

	QJsonArray earnings;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject entry;
			entry["channel_id"] = query.value(0).toLongLong();
			entry["total_earnings"] = query.value(1).toDouble();
			earnings.append(entry);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["earnings"] = earnings;
	return result;
}

QJsonObject Server::toolGetEarningsChart(const QJsonObject &args) {
	int days = args.value("days").toInt(30);

	QSqlQuery query(_db);
	query.prepare("SELECT date, SUM(amount) FROM wallet_spending "
				  "WHERE amount > 0 AND date >= date('now', '-' || ? || ' days') "
				  "GROUP BY date ORDER BY date");
	query.addBindValue(days);

	QJsonArray chart;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject point;
			point["date"] = query.value(0).toString();
			point["earnings"] = query.value(1).toDouble();
			chart.append(point);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["chart"] = chart;
	result["days"] = days;
	return result;
}

// ============================================================================
// GIVEAWAY TOOLS - local tracking
// ============================================================================

QJsonObject Server::toolListGiveaways(const QJsonObject &args) {
	int limit = args.value("limit").toInt(50);
	QString status = args.value("status").toString();

	QSqlQuery query(_db);
	if (status.isEmpty()) {
		query.prepare("SELECT id, type, stars_amount, winner_count, channel_id, status, created_at "
					  "FROM giveaways ORDER BY created_at DESC LIMIT ?");
		query.addBindValue(limit);
	} else {
		query.prepare("SELECT id, type, stars_amount, winner_count, channel_id, status, created_at "
					  "FROM giveaways WHERE status = ? ORDER BY created_at DESC LIMIT ?");
		query.addBindValue(status);
		query.addBindValue(limit);
	}

	QJsonArray giveaways;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject g;
			g["id"] = query.value(0).toLongLong();
			g["type"] = query.value(1).toString();
			g["stars_amount"] = query.value(2).toInt();
			g["winner_count"] = query.value(3).toInt();
			g["channel_id"] = query.value(4).toLongLong();
			g["status"] = query.value(5).toString();
			g["created_at"] = query.value(6).toString();
			giveaways.append(g);
		}
	}

	QJsonObject result;
	result["success"] = true;
	result["giveaways"] = giveaways;
	result["count"] = giveaways.size();
	return result;
}

QJsonObject Server::toolGetGiveawayOptions(const QJsonObject &args) {
	Q_UNUSED(args);
	if (!_session) {
		return toolError("No active session");
	}
	return awaitMtp([&](auto done, auto fail) {
		_session->api().request(MTPpayments_GetStarsGiveawayOptions(
		)).done([=](const MTPVector<MTPStarsGiveawayOption> &result) {
			QJsonArray options;
			for (const auto &option : result.v) {
				const auto &data = option.data();
				QJsonObject entry;
				entry["stars"] = qint64(data.vstars().v);
				entry["currency"] = qs(data.vcurrency());
				entry["amount"] = qint64(data.vamount().v);
				entry["yearly_boosts"] = data.vyearly_boosts().v;
				entry["is_default"] = data.is_default();
				entry["extended"] = data.is_extended();
				QJsonArray winners;
				for (const auto &w : data.vwinners().v) {
					const auto &wd = w.data();
					QJsonObject entry2;
					entry2["users"] = wd.vusers().v;
					entry2["per_user_stars"] = qint64(wd.vper_user_stars().v);
					entry2["is_default"] = wd.is_default();
					winners.append(entry2);
				}
				entry["winners_options"] = winners;
				options.append(entry);
			}
			QJsonObject value;
			value["success"] = true;
			value["options"] = options;
			value["count"] = options.size();
			done(value);
		}).fail([=](const MTP::Error &error) {
			fail("payments.getStarsGiveawayOptions failed: " + error.type());
		}).send();
	});
}

QJsonObject Server::toolGetGiveawayStats(const QJsonObject &args) {
	const auto giveawayId = args["giveaway_id"].toVariant().toLongLong();
	if (!giveawayId) {
		return toolError("giveaway_id is required and must be non-zero");
	}

	// Reports the giveaway asked for. It previously aggregated every giveaway
	// in the table and returned those totals whatever id was passed, so the
	// numbers described the wrong thing whenever more than one existed.
	QSqlQuery query(_db);
	query.prepare("SELECT type, stars_amount, winner_count, channel_id, "
		"status, created_at FROM giveaways WHERE id = ?");
	query.addBindValue(giveawayId);
	if (!query.exec()) {
		return toolError("Could not read giveaway: "
			+ query.lastError().text());
	}
	if (!query.next()) {
		return toolError(QString("No giveaway recorded locally with id %1")
			.arg(giveawayId));
	}

	QJsonObject result;
	result["success"] = true;
	result["giveaway_id"] = giveawayId;
	result["type"] = query.value(0).toString();
	result["stars_amount"] = query.value(1).toInt();
	result["winner_count"] = query.value(2).toInt();
	result["channel_id"] = query.value(3).toLongLong();
	result["status"] = query.value(4).toString();
	result["created_at"] = query.value(5).toString();
	return result;
}

// ============================================================================
// BLOCK/UNBLOCK - real Telegram API
// ============================================================================

QJsonObject Server::toolBlockUser(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();

	if (!_session) {
		QJsonObject result;
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	if (userId == 0) {
		QJsonObject result;
		result["error"] = "Missing user_id parameter";
		result["success"] = false;
		return result;
	}

	auto peer = resolvePeer(userId);
	if (!peer) {
		QJsonObject result;
		result["error"] = "User not found";
		result["success"] = false;
		return result;
	}

	_session->api().blockedPeers().block(peer);

	QJsonObject result;
	result["success"] = true;
	result["user_id"] = userId;
	result["status"] = "blocked";
	return result;
}

QJsonObject Server::toolUnblockUser(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();

	if (!_session) {
		QJsonObject result;
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	if (userId == 0) {
		QJsonObject result;
		result["error"] = "Missing user_id parameter";
		result["success"] = false;
		return result;
	}

	auto peer = resolvePeer(userId);
	if (!peer) {
		QJsonObject result;
		result["error"] = "User not found";
		result["success"] = false;
		return result;
	}

	_session->api().blockedPeers().unblock(peer);

	QJsonObject result;
	result["success"] = true;
	result["user_id"] = userId;
	result["status"] = "unblocked";
	return result;
}

// ============================================================================
// TAG TOOLS - delegate to premium tools implementations
// ============================================================================

QJsonObject Server::toolTagMessage(const QJsonObject &args) {
	return toolAddMessageTag(args);
}

QJsonObject Server::toolListTags(const QJsonObject &args) {
	return toolGetMessageTags(args);
}

QJsonObject Server::toolDeleteTag(const QJsonObject &args) {
	return toolRemoveMessageTag(args);
}

// ============================================================================
// TASK TOOLS - delegate to premium tools
// ============================================================================

QJsonObject Server::toolCreateTask(const QJsonObject &args) {
	return toolCreateTaskFromMessage(args);
}

// ============================================================================
// AWAY TOOLS - delegate to business tools
// ============================================================================

QJsonObject Server::toolGetAwayConfig(const QJsonObject &args) {
	return toolGetAwayMessage(args);
}

QJsonObject Server::toolSetAwayNow(const QJsonObject &args) {
	const auto message = args["message"].toString();
	if (message.isEmpty()) {
		return toolError("message is required: away mode needs the text to "
			"reply with");
	}

	// Stores the message it was given. It previously flipped the enabled flag
	// and ignored the argument entirely, so callers got away mode with
	// whatever text happened to be configured before -- or an error claiming
	// nothing was configured while holding the text to configure it with.
	QSqlQuery query(_db);
	query.prepare("INSERT INTO away_config (id, enabled, message, updated_at) "
		"VALUES (1, 1, ?, datetime('now')) "
		"ON CONFLICT(id) DO UPDATE SET "
		"enabled = 1, message = excluded.message, "
		"updated_at = datetime('now')");
	query.addBindValue(message);
	if (!query.exec()) {
		return toolError("Could not enable away mode: "
			+ query.lastError().text());
	}

	QJsonObject result;
	result["success"] = true;
	result["enabled"] = true;
	result["message"] = message;
	return result;
}

QJsonObject Server::toolGetAwayStats(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("SELECT away_sent, updated_at FROM away_config WHERE id = 1");

	QJsonObject result;
	if (query.exec() && query.next()) {
		result["away_replies_sent"] = query.value(0).toInt();
		result["last_updated"] = query.value(1).toString();
		result["success"] = true;
	} else {
		result["away_replies_sent"] = 0;
		result["success"] = true;
		result["note"] = "No away message configured";
	}
	return result;
}

// ============================================================================
// BUSINESS HOURS - delegate to business tools
// ============================================================================

QJsonObject Server::toolIsOpenNow(const QJsonObject &args) {
	return toolCheckBusinessStatus(args);
}

// ============================================================================
// CHATBOT - delegate to business tools
// ============================================================================

QJsonObject Server::toolPauseChatbot(const QJsonObject &args) {
	Q_UNUSED(args);
	QSqlQuery query(_db);
	query.prepare("UPDATE chatbot_config SET enabled = 0 WHERE id = 1");

	QJsonObject result;
	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["enabled"] = false;
		result["note"] = "Chatbot paused";
	} else {
		result["success"] = false;
		result["error"] = "No chatbot configured";
	}
	return result;
}

// ============================================================================
// VOICE CLONE - local storage
// ============================================================================

QJsonObject Server::toolCloneVoice(const QJsonObject &args) {
	QString name = args["name"].toString();
	QString audioSample = args["audio_sample"].toString();
	if (audioSample.isEmpty()) {
		audioSample = args["audio_path"].toString();
	}

	QJsonObject result;

	if (name.isEmpty()) {
		result["error"] = "Missing name parameter";
		result["success"] = false;
		return result;
	}

	if (audioSample.isEmpty()) {
		result["error"] = "Missing audio_sample/audio_path parameter - "
						  "provide path to a WAV file with the voice to clone";
		result["success"] = false;
		return result;
	}

	if (!QFile::exists(audioSample)) {
		result["error"] = QString("Audio sample file not found: %1").arg(audioSample);
		result["success"] = false;
		return result;
	}

	// Store the clone sample reference
	QSqlQuery sampleQuery(_db);
	sampleQuery.prepare("INSERT OR REPLACE INTO voice_clone_samples "
						"(name, sample_path, provider, language, created_at) "
						"VALUES (?, ?, 'coqui', 'en', datetime('now'))");
	sampleQuery.addBindValue(name);
	sampleQuery.addBindValue(audioSample);
	sampleQuery.exec();

	// Create/update voice persona pointing to this clone
	QSqlQuery query(_db);
	query.prepare("INSERT OR REPLACE INTO voice_persona "
				  "(name, voice_id, pitch, speed, provider, sample_path, created_at) "
				  "VALUES (?, 'xtts_v2_clone', 1.0, 1.0, 'coqui', ?, datetime('now'))");
	query.addBindValue(name);
	query.addBindValue(audioSample);

	if (query.exec()) {
		// Test the clone if TTS service is available
		if (_textToSpeech && _textToSpeech->isRunning()) {
			auto testResult = _textToSpeech->cloneAndSynthesize(
				"This is a voice clone test.", audioSample, 1.0);
			if (testResult.success) {
				result["test_synthesis"] = "success";
				result["test_duration"] = testResult.durationSeconds;
			} else {
				result["test_synthesis"] = "failed";
				result["test_error"] = testResult.error;
				result["note"] = "Clone registered but test synthesis failed. "
								 "Ensure Coqui TTS with XTTS-v2 is installed: "
								 "pip install TTS";
			}
		}

		result["success"] = true;
		result["name"] = name;
		result["sample_path"] = audioSample;
		result["provider"] = "coqui";
		result["status"] = "clone_registered";
	} else {
		result["success"] = false;
		result["error"] = "Failed to register voice clone: " + query.lastError().text();
	}
	return result;
}

// ============================================================================
// GIFTS - delegate to wallet/stars tools
// ============================================================================

QJsonObject Server::toolListGifts(const QJsonObject &args) {
	return toolListAvailableGifts(args);
}

QJsonObject Server::toolGetStarRating(const QJsonObject &args) {
	return toolGetStarRatingDetails(args);
}

// ============================================================================
// FINANCIAL - delegate to wallet tools
// ============================================================================

QJsonObject Server::toolGetTaxSummary(const QJsonObject &args) {
	const auto year = args["year"].toInt();
	if (year < 1970 || year > 2100) {
		return toolError("year is required and must be a four-digit calendar "
			"year");
	}

	// Scoped to the requested year. This used to sum every transaction ever
	// recorded and return it under whatever year the caller asked for, which
	// is the one thing a tax summary must never do.
	const auto from = QString::number(year) + "-01-01";
	const auto to = QString::number(year) + "-12-31";

	QSqlQuery query(_db);
	query.prepare("SELECT "
		"SUM(CASE WHEN amount > 0 THEN amount ELSE 0 END), "
		"SUM(CASE WHEN amount < 0 THEN ABS(amount) ELSE 0 END), "
		"COUNT(*) "
		"FROM wallet_spending WHERE date(date) BETWEEN date(?) AND date(?)");
	query.addBindValue(from);
	query.addBindValue(to);

	double income = 0, expense = 0;
	int count = 0;
	if (!query.exec()) {
		return toolError("Could not read transactions: "
			+ query.lastError().text());
	}
	if (query.next()) {
		income = query.value(0).toDouble();
		expense = query.value(1).toDouble();
		count = query.value(2).toInt();
	}

	QJsonObject result;
	result["success"] = true;
	result["year"] = year;
	result["total_income"] = income;
	result["total_expenses"] = expense;
	result["net_income"] = income - expense;
	result["transaction_count"] = count;
	result["note"] = "Locally tracked transactions only; nothing here is "
		"synced from Telegram and it is not tax advice";
	return result;
}

// ============================================================================
// STAR GIFTS - delegate to stars tools
// ============================================================================

QJsonObject Server::toolSendStarGift(const QJsonObject &args) {
	return toolSendGift(args);
}

QJsonObject Server::toolTransferGift(const QJsonObject &args) {
	QString giftId = args["gift_id"].toString();
	qint64 recipientId = args["recipient_id"].toVariant().toLongLong();

	QJsonObject result;

	if (giftId.isEmpty() || recipientId == 0) {
		result["error"] = "Missing gift_id or recipient_id";
		result["success"] = false;
		return result;
	}

	if (!_session) {
		result["error"] = "No active session";
		result["success"] = false;
		return result;
	}

	auto recipient = resolvePeer(recipientId);
	if (!recipient) {
		result["error"] = QString("Recipient %1 not found").arg(recipientId);
		result["success"] = false;
		return result;
	}

	// Determine input gift ID - check if it's a slug (unique gift) or numeric message ID
	MTPInputSavedStarGift inputGift = giftId.toLongLong() > 0
		? MTP_inputSavedStarGiftUser(MTP_int(giftId.toInt()))
		: MTP_inputSavedStarGiftSlug(MTP_string(giftId));

	_session->api().request(MTPpayments_TransferStarGift(
		inputGift,
		recipient->input()
	)).done([giftId, recipientId](const MTPUpdates &updates) {
		qWarning() << "MCP: Gift" << giftId << "transferred to" << recipientId << "successfully";
	}).fail([giftId, recipientId](const MTP::Error &error) {
		qWarning() << "MCP: Failed to transfer gift" << giftId << "to" << recipientId << ":" << error.type();
	}).send();

	// Record transfer in local DB
	QSqlQuery query(_db);
	query.prepare("INSERT INTO gift_transfers (gift_id, from_user, to_user, transfer_date, status) "
				  "VALUES (?, ?, ?, datetime('now'), 'submitted')");
	query.addBindValue(giftId);
	query.addBindValue(_session->userId().bare);
	query.addBindValue(recipientId);
	query.exec();

	result["success"] = true;
	result["gift_id"] = giftId;
	result["recipient_id"] = recipientId;
	result["status"] = "transfer_submitted";
	result["api_request"] = "submitted";
	result["note"] = "Transfer request sent via MTPpayments_TransferStarGift";
	return result;
}

QJsonObject Server::toolCancelListing(const QJsonObject &args) {
	return toolDelistGift(args);
}


} // namespace MCP
