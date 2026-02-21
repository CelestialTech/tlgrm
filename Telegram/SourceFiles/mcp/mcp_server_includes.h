// Common includes for all MCP server implementation files.
// This file is part of Telegram Desktop MCP integration.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#pragma once

#include "mcp_server.h"
#include "chat_archiver.h"
#include "gradual_archiver.h"
#include "analytics.h"
#include "semantic_search.h"
#include "batch_operations.h"
#include "message_scheduler.h"
#include "audit_logger.h"
#include "rbac.h"
#include "voice_transcription.h"
#include "text_to_speech.h"
#include "local_llm.h"
#include "video_generator.h"
#include "ton_wallet.h"
#include "bot_manager.h"
#include "context_assistant_bot.h"
#include "cache_manager.h"

#include <QtCore/QTimer>
#include <QtCore/QRandomGenerator>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtSql/QSqlError>

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_thread.h"
#include "data/data_histories.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_media_types.h"
#include "data/data_file_origin.h"
#include "history/view/history_view_element.h"
#include "api/api_common.h"
#include "api/api_editing.h"
#include "api/api_user_privacy.h"
#include "api/api_authorizations.h"
#include "api/api_self_destruct.h"
#include "api/api_blocked_peers.h"
#include "apiwrap.h"
#include "storage/storage_account.h"
#include "core/file_utilities.h"
#include "mtproto/mtp_instance.h"
#include "api/api_credits.h"
#include "api/api_premium.h"
#include "data/data_credits.h"
#include "data/data_credits_earn.h"
#include "data/data_subscriptions.h"
#include "data/data_star_gift.h"
#include "data/components/credits.h"
#include "core/credits_amount.h"
