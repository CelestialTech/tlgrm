// MCP Server - Model Context Protocol implementation in C++
// Native MCP server embedded in Telegram Desktop
//
// This file is part of Telegram Desktop MCP integration,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#pragma once

#include <QtCore/QObject>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QTextStream>
#include <QtCore/QHash>
#include <QtNetwork/QTcpServer>
#include <QtSql/QSqlDatabase>

#include <functional>
#include <memory>

#include "mcp/mcp_helpers.h"
#include "base/basic_types.h"

namespace rpl {
class lifetime;
} // namespace rpl

namespace Main {
class Session;
} // namespace Main

class HistoryItem;

namespace Export {
class Controller;
} // namespace Export

namespace MCP {

// Forward declarations
class ChatArchiver;
class EphemeralArchiver;
class Analytics;
class SemanticSearch;
class BatchOperations;
class MessageScheduler;
class AuditLogger;
class RBAC;
class VoiceTranscription;
class TextToSpeech;
class LocalLLM;
class VideoGenerator;
class TonWallet;
class BotManager;
class CacheManager;
class GradualArchiver;

// MCP Protocol types
enum class TransportType {
	Stdio,    // Standard input/output (default for Claude Desktop)
	HTTP,     // HTTP with SSE for notifications
	WebSocket, // WebSocket (future)
	IPC       // IPC only mode (no stdin polling) - for GUI mode
};

// MCP Tool definition
struct Tool {
	QString name;
	QString description;
	QJsonObject inputSchema;
};

// MCP Resource definition
struct Resource {
	QString uri;
	QString name;
	QString description;
	QString mimeType;
};

// MCP Prompt definition
struct Prompt {
	QString name;
	QString description;
	QJsonArray arguments;
};

class Server : public QObject {
	Q_OBJECT

	friend class Bridge;  // Allow Bridge to access private tool methods

public:
	explicit Server(QObject *parent = nullptr);
	~Server();

	// Start MCP server
	bool start(TransportType transport = TransportType::Stdio);

	// Stop MCP server
	void stop();

	// Set session for live data access
	void setSession(Main::Session *session);

	// Check if session is set
	bool hasSession() const { return _session != nullptr; }

	// Server info
	struct ServerInfo {
		QString name = "Telegram Desktop MCP";
		QString version = "1.0.0";
		QJsonObject capabilities;
	};

	ServerInfo serverInfo() const { return _serverInfo; }

	// Call a tool by name (for Bridge delegation)
	QJsonObject callTool(const QString &toolName, const QJsonObject &args);

private:
	// Initialize server capabilities
	void initializeCapabilities();

	// Register tools, resources, prompts
	void registerTools();
	void registerResources();
	void registerPrompts();

	// Handle incoming JSON-RPC requests
	QJsonObject handleRequest(const QJsonObject &request);

	// JSON-RPC method handlers
	QJsonObject handleInitialize(const QJsonObject &params);
	QJsonObject handleListTools(const QJsonObject &params);
	QJsonObject handleCallTool(const QJsonObject &params);
	QJsonObject handleListResources(const QJsonObject &params);
	QJsonObject handleReadResource(const QJsonObject &params);
	QJsonObject handleListPrompts(const QJsonObject &params);
	QJsonObject handleGetPrompt(const QJsonObject &params);

	// Core tool implementations (original 6)
	QJsonObject toolListChats(const QJsonObject &args);
	QJsonObject toolGetChatInfo(const QJsonObject &args);
	QJsonObject toolReadMessages(const QJsonObject &args);
	QJsonObject toolSendMessage(const QJsonObject &args);
	QJsonObject toolSearchMessages(const QJsonObject &args);
	QJsonObject toolGetUserInfo(const QJsonObject &args);

	// Archive tools (10 tools - includes ephemeral capture + export status)
	QJsonObject toolArchiveChat(const QJsonObject &args);
	QJsonObject toolExportChat(const QJsonObject &args);
	QJsonObject toolGetExportStatus(const QJsonObject &args);
	QJsonObject toolListArchivedChats(const QJsonObject &args);
	QJsonObject toolGetArchiveStats(const QJsonObject &args);
	QJsonObject toolConfigureEphemeralCapture(const QJsonObject &args);
	QJsonObject toolGetEphemeralStats(const QJsonObject &args);
	QJsonObject toolGetEphemeralMessages(const QJsonObject &args);
	QJsonObject toolSearchArchive(const QJsonObject &args);
	QJsonObject toolPurgeArchive(const QJsonObject &args);

	// Analytics tools (8 tools)
	QJsonObject toolGetMessageStats(const QJsonObject &args);
	QJsonObject toolGetUserActivity(const QJsonObject &args);
	QJsonObject toolGetChatActivity(const QJsonObject &args);
	QJsonObject toolGetTimeSeries(const QJsonObject &args);
	QJsonObject toolGetTopUsers(const QJsonObject &args);
	QJsonObject toolGetTopWords(const QJsonObject &args);
	QJsonObject toolExportAnalytics(const QJsonObject &args);
	QJsonObject toolGetTrends(const QJsonObject &args);

	// Semantic search tools (5 tools)
	QJsonObject toolSemanticSearch(const QJsonObject &args);
	QJsonObject toolIndexMessages(const QJsonObject &args);
	QJsonObject toolDetectTopics(const QJsonObject &args);
	QJsonObject toolClassifyIntent(const QJsonObject &args);
	QJsonObject toolExtractEntities(const QJsonObject &args);

	// Message operations (6 tools)
	QJsonObject toolEditMessage(const QJsonObject &args);
	QJsonObject toolDeleteMessage(const QJsonObject &args);
	QJsonObject toolForwardMessage(const QJsonObject &args);
	QJsonObject toolPinMessage(const QJsonObject &args);
	QJsonObject toolUnpinMessage(const QJsonObject &args);
	QJsonObject toolAddReaction(const QJsonObject &args);

	// Batch operations (5 tools)
	QJsonObject toolBatchSend(const QJsonObject &args);
	QJsonObject toolBatchDelete(const QJsonObject &args);
	QJsonObject toolBatchForward(const QJsonObject &args);
	QJsonObject toolBatchPin(const QJsonObject &args);
	QJsonObject toolBatchReaction(const QJsonObject &args);

	// Scheduler tools (4 tools)
	QJsonObject toolScheduleMessage(const QJsonObject &args);
	QJsonObject toolCancelScheduled(const QJsonObject &args);
	QJsonObject toolListScheduled(const QJsonObject &args);
	QJsonObject toolUpdateScheduled(const QJsonObject &args);

	// System tools (4 tools)
	QJsonObject toolGetCacheStats(const QJsonObject &args);
	QJsonObject toolGetServerInfo(const QJsonObject &args);
	QJsonObject toolGetAuditLog(const QJsonObject &args);
	QJsonObject toolHealthCheck(const QJsonObject &args);

	// Voice tools (2 tools)
	QJsonObject toolTranscribeVoice(const QJsonObject &args);
	QJsonObject toolGetTranscription(const QJsonObject &args);

	// Bot framework tools (8 tools)
	QJsonObject toolListBots(const QJsonObject &args);
	QJsonObject toolGetBotInfo(const QJsonObject &args);
	QJsonObject toolStartBot(const QJsonObject &args);
	QJsonObject toolStopBot(const QJsonObject &args);
	QJsonObject toolConfigureBot(const QJsonObject &args);
	QJsonObject toolGetBotStats(const QJsonObject &args);
	QJsonObject toolSendBotCommand(const QJsonObject &args);
	QJsonObject toolGetBotSuggestions(const QJsonObject &args);

	// Profile settings tools (5 tools)
	QJsonObject toolGetProfileSettings(const QJsonObject &args);
	QJsonObject toolUpdateProfileName(const QJsonObject &args);
	QJsonObject toolUpdateProfileBio(const QJsonObject &args);
	QJsonObject toolUpdateProfileUsername(const QJsonObject &args);
	QJsonObject toolUpdateProfilePhone(const QJsonObject &args);

	// Privacy settings tools (8 tools)
	QJsonObject toolGetPrivacySettings(const QJsonObject &args);
	QJsonObject toolUpdateLastSeenPrivacy(const QJsonObject &args);
	QJsonObject toolUpdateProfilePhotoPrivacy(const QJsonObject &args);
	QJsonObject toolUpdatePhoneNumberPrivacy(const QJsonObject &args);
	QJsonObject toolUpdateForwardsPrivacy(const QJsonObject &args);
	QJsonObject toolUpdateBirthdayPrivacy(const QJsonObject &args);
	QJsonObject toolUpdateAboutPrivacy(const QJsonObject &args);
	QJsonObject toolGetBlockedUsers(const QJsonObject &args);

	// Security settings tools (6 tools)
	QJsonObject toolGetSecuritySettings(const QJsonObject &args);
	QJsonObject toolGetActiveSessions(const QJsonObject &args);
	QJsonObject toolTerminateSession(const QJsonObject &args);
	QJsonObject toolBlockUser(const QJsonObject &args);
	QJsonObject toolUnblockUser(const QJsonObject &args);
	QJsonObject toolUpdateAutoDeletePeriod(const QJsonObject &args);

	// ============================================================
	// PREMIUM EQUIVALENT FEATURES (17 tools)
	// ============================================================

	// Voice-to-Text (local Whisper) - 2 tools
	QJsonObject toolTranscribeVoiceMessage(const QJsonObject &args);
	QJsonObject toolGetTranscriptionStatus(const QJsonObject &args);

	// Translation (local) - 3 tools
	QJsonObject toolTranslateMessages(const QJsonObject &args);
	QJsonObject toolAutoTranslateChat(const QJsonObject &args);
	QJsonObject toolGetTranslationLanguages(const QJsonObject &args);

	// Message Tags - 4 tools
	QJsonObject toolTagMessage(const QJsonObject &args);
	QJsonObject toolGetTaggedMessages(const QJsonObject &args);
	QJsonObject toolListTags(const QJsonObject &args);
	QJsonObject toolDeleteTag(const QJsonObject &args);

	// Ad Filtering - 2 tools
	QJsonObject toolConfigureAdFilter(const QJsonObject &args);
	QJsonObject toolGetFilteredAds(const QJsonObject &args);

	// Chat Rules Engine - 4 tools
	QJsonObject toolCreateChatRule(const QJsonObject &args);
	QJsonObject toolListChatRules(const QJsonObject &args);
	QJsonObject toolExecuteChatRules(const QJsonObject &args);
	QJsonObject toolDeleteChatRule(const QJsonObject &args);

	// Local Task Management - 2 tools
	QJsonObject toolCreateTask(const QJsonObject &args);
	QJsonObject toolListTasks(const QJsonObject &args);

	// ============================================================
	// BUSINESS EQUIVALENT FEATURES (36 tools)
	// ============================================================

	// Quick Replies - 5 tools
	QJsonObject toolCreateQuickReply(const QJsonObject &args);
	QJsonObject toolListQuickReplies(const QJsonObject &args);
	QJsonObject toolSendQuickReply(const QJsonObject &args);
	QJsonObject toolEditQuickReply(const QJsonObject &args);
	QJsonObject toolDeleteQuickReply(const QJsonObject &args);

	// Greeting Messages - 4 tools
	QJsonObject toolConfigureGreeting(const QJsonObject &args);
	QJsonObject toolGetGreetingConfig(const QJsonObject &args);
	QJsonObject toolTestGreeting(const QJsonObject &args);
	QJsonObject toolGetGreetingStats(const QJsonObject &args);

	// Away Messages - 5 tools
	QJsonObject toolConfigureAwayMessage(const QJsonObject &args);
	QJsonObject toolGetAwayConfig(const QJsonObject &args);
	QJsonObject toolSetAwayNow(const QJsonObject &args);
	QJsonObject toolDisableAway(const QJsonObject &args);
	QJsonObject toolGetAwayStats(const QJsonObject &args);

	// Business Hours - 3 tools
	QJsonObject toolSetBusinessHours(const QJsonObject &args);
	QJsonObject toolGetBusinessHours(const QJsonObject &args);
	QJsonObject toolIsOpenNow(const QJsonObject &args);

	// Business Location - 2 tools
	QJsonObject toolSetBusinessLocation(const QJsonObject &args);
	QJsonObject toolGetBusinessLocation(const QJsonObject &args);

	// AI Chatbot - 7 tools
	QJsonObject toolConfigureAiChatbot(const QJsonObject &args);
	QJsonObject toolGetChatbotConfig(const QJsonObject &args);
	QJsonObject toolPauseChatbot(const QJsonObject &args);
	QJsonObject toolResumeChatbot(const QJsonObject &args);
	QJsonObject toolSetChatbotPrompt(const QJsonObject &args);
	QJsonObject toolGetChatbotStats(const QJsonObject &args);
	QJsonObject toolTrainChatbot(const QJsonObject &args);

	// AI Voice (TTS) - 5 tools
	QJsonObject toolConfigureVoicePersona(const QJsonObject &args);
	QJsonObject toolGenerateVoiceMessage(const QJsonObject &args);
	QJsonObject toolSendVoiceReply(const QJsonObject &args);
	QJsonObject toolListVoicePresets(const QJsonObject &args);
	QJsonObject toolCloneVoice(const QJsonObject &args);

	// AI Video Circles (TTV) - 5 tools
	QJsonObject toolConfigureVideoAvatar(const QJsonObject &args);
	QJsonObject toolGenerateVideoCircle(const QJsonObject &args);
	QJsonObject toolSendVideoReply(const QJsonObject &args);
	QJsonObject toolUploadAvatarSource(const QJsonObject &args);
	QJsonObject toolListAvatarPresets(const QJsonObject &args);

	// ============================================================
	// WALLET FEATURES (32 tools)
	// ============================================================

	// Balance & Analytics - 4 tools
	QJsonObject toolGetWalletBalance(const QJsonObject &args);
	QJsonObject toolGetBalanceHistory(const QJsonObject &args);
	QJsonObject toolGetSpendingAnalytics(const QJsonObject &args);
	QJsonObject toolGetIncomeAnalytics(const QJsonObject &args);

	// Transactions - 4 tools
	QJsonObject toolGetTransactions(const QJsonObject &args);
	QJsonObject toolGetTransactionDetails(const QJsonObject &args);
	QJsonObject toolExportTransactions(const QJsonObject &args);
	QJsonObject toolSearchTransactions(const QJsonObject &args);

	// Gifts - 4 tools
	QJsonObject toolListGifts(const QJsonObject &args);
	QJsonObject toolGetGiftDetails(const QJsonObject &args);
	QJsonObject toolGetGiftAnalytics(const QJsonObject &args);
	QJsonObject toolSendStars(const QJsonObject &args);

	// Subscriptions - 3 tools
	QJsonObject toolListSubscriptions(const QJsonObject &args);
	QJsonObject toolGetSubscriptionAlerts(const QJsonObject &args);
	QJsonObject toolCancelSubscription(const QJsonObject &args);

	// Monetization - 5 tools
	QJsonObject toolGetChannelEarnings(const QJsonObject &args);
	QJsonObject toolGetAllChannelsEarnings(const QJsonObject &args);
	QJsonObject toolGetEarningsChart(const QJsonObject &args);
	QJsonObject toolGetReactionStats(const QJsonObject &args);
	QJsonObject toolGetPaidContentEarnings(const QJsonObject &args);

	// Giveaways - 3 tools
	QJsonObject toolGetGiveawayOptions(const QJsonObject &args);
	QJsonObject toolListGiveaways(const QJsonObject &args);
	QJsonObject toolGetGiveawayStats(const QJsonObject &args);

	// Advanced - 4 tools
	QJsonObject toolGetTopupOptions(const QJsonObject &args);
	QJsonObject toolGetStarRating(const QJsonObject &args);
	QJsonObject toolGetWithdrawalStatus(const QJsonObject &args);
	QJsonObject toolCreateCryptoPayment(const QJsonObject &args);

	// Budget & Reporting - 5 tools
	QJsonObject toolSetWalletBudget(const QJsonObject &args);
	QJsonObject toolGetBudgetStatus(const QJsonObject &args);
	QJsonObject toolConfigureWalletAlerts(const QJsonObject &args);
	QJsonObject toolGenerateFinancialReport(const QJsonObject &args);
	QJsonObject toolGetTaxSummary(const QJsonObject &args);

	// ============================================================
	// STARS FEATURES (45 tools)
	// ============================================================

	// Star Gifts Management - 8 tools
	QJsonObject toolListStarGifts(const QJsonObject &args);
	QJsonObject toolGetStarGiftDetails(const QJsonObject &args);
	QJsonObject toolGetUniqueGiftAnalytics(const QJsonObject &args);
	QJsonObject toolGetCollectiblesPortfolio(const QJsonObject &args);
	QJsonObject toolSendStarGift(const QJsonObject &args);
	QJsonObject toolGetGiftTransferHistory(const QJsonObject &args);
	QJsonObject toolGetUpgradeOptions(const QJsonObject &args);
	QJsonObject toolTransferGift(const QJsonObject &args);

	// Gift Collections - 3 tools
	QJsonObject toolListGiftCollections(const QJsonObject &args);
	QJsonObject toolGetCollectionDetails(const QJsonObject &args);
	QJsonObject toolGetCollectionCompletion(const QJsonObject &args);

	// Auctions - 5 tools
	QJsonObject toolListActiveAuctions(const QJsonObject &args);
	QJsonObject toolGetAuctionDetails(const QJsonObject &args);
	QJsonObject toolGetAuctionAlerts(const QJsonObject &args);
	QJsonObject toolPlaceAuctionBid(const QJsonObject &args);
	QJsonObject toolGetAuctionHistory(const QJsonObject &args);

	// Marketplace - 5 tools
	QJsonObject toolBrowseGiftMarketplace(const QJsonObject &args);
	QJsonObject toolGetMarketTrends(const QJsonObject &args);
	QJsonObject toolListGiftForSale(const QJsonObject &args);
	QJsonObject toolUpdateListing(const QJsonObject &args);
	QJsonObject toolCancelListing(const QJsonObject &args);

	// Star Reactions - 3 tools
	QJsonObject toolGetStarReactionsReceived(const QJsonObject &args);
	QJsonObject toolGetStarReactionsSent(const QJsonObject &args);
	QJsonObject toolGetTopSupporters(const QJsonObject &args);

	// Paid Content - 4 tools
	QJsonObject toolGetPaidMessagesStats(const QJsonObject &args);
	QJsonObject toolConfigurePaidMessages(const QJsonObject &args);
	QJsonObject toolGetPaidMediaStats(const QJsonObject &args);
	QJsonObject toolGetUnlockedContent(const QJsonObject &args);

	// Mini Apps - 3 tools
	QJsonObject toolGetMiniappSpending(const QJsonObject &args);
	QJsonObject toolGetMiniappHistory(const QJsonObject &args);
	QJsonObject toolSetMiniappBudget(const QJsonObject &args);

	// Star Rating - 3 tools
	QJsonObject toolGetStarRatingDetails(const QJsonObject &args);
	QJsonObject toolGetRatingHistory(const QJsonObject &args);
	QJsonObject toolSimulateRatingChange(const QJsonObject &args);

	// Profile Display - 4 tools
	QJsonObject toolGetProfileGifts(const QJsonObject &args);
	QJsonObject toolUpdateGiftDisplay(const QJsonObject &args);
	QJsonObject toolReorderProfileGifts(const QJsonObject &args);
	QJsonObject toolToggleGiftNotifications(const QJsonObject &args);

	// AI & Analytics - 7 tools
	QJsonObject toolGetGiftInvestmentAdvice(const QJsonObject &args);
	QJsonObject toolBacktestStrategy(const QJsonObject &args);
	QJsonObject toolGetPortfolioPerformance(const QJsonObject &args);
	QJsonObject toolCreatePriceAlert(const QJsonObject &args);
	QJsonObject toolCreateAuctionAlert(const QJsonObject &args);
	QJsonObject toolGetFragmentListings(const QJsonObject &args);
	QJsonObject toolExportPortfolioReport(const QJsonObject &args);

	// ============================================================
	// ADDITIONAL TOOL IMPLEMENTATIONS (missing from original)
	// ============================================================

	// Premium feature tools
	QJsonObject toolGetVoiceTranscription(const QJsonObject &args);
	QJsonObject toolTranslateMessage(const QJsonObject &args);
	QJsonObject toolGetTranslationHistory(const QJsonObject &args);
	QJsonObject toolAddMessageTag(const QJsonObject &args);
	QJsonObject toolGetMessageTags(const QJsonObject &args);
	QJsonObject toolRemoveMessageTag(const QJsonObject &args);
	QJsonObject toolSearchByTag(const QJsonObject &args);
	QJsonObject toolGetTagSuggestions(const QJsonObject &args);
	QJsonObject toolGetAdFilterStats(const QJsonObject &args);
	QJsonObject toolSetChatRules(const QJsonObject &args);
	QJsonObject toolGetChatRules(const QJsonObject &args);
	QJsonObject toolTestChatRules(const QJsonObject &args);
	QJsonObject toolCreateTaskFromMessage(const QJsonObject &args);
	QJsonObject toolUpdateTask(const QJsonObject &args);

	// Business feature tools
	QJsonObject toolUpdateQuickReply(const QJsonObject &args);
	QJsonObject toolUseQuickReply(const QJsonObject &args);
	QJsonObject toolSetGreetingMessage(const QJsonObject &args);
	QJsonObject toolGetGreetingMessage(const QJsonObject &args);
	QJsonObject toolDisableGreeting(const QJsonObject &args);
	QJsonObject toolSetAwayMessage(const QJsonObject &args);
	QJsonObject toolGetAwayMessage(const QJsonObject &args);
	QJsonObject toolGetNextAvailableSlot(const QJsonObject &args);
	QJsonObject toolCheckBusinessStatus(const QJsonObject &args);
	QJsonObject toolConfigureChatbot(const QJsonObject &args);
	QJsonObject toolGetChatbotAnalytics(const QJsonObject &args);
	QJsonObject toolTestChatbot(const QJsonObject &args);
	QJsonObject toolCreateAutoReplyRule(const QJsonObject &args);
	QJsonObject toolListAutoReplyRules(const QJsonObject &args);
	QJsonObject toolUpdateAutoReplyRule(const QJsonObject &args);
	QJsonObject toolDeleteAutoReplyRule(const QJsonObject &args);
	QJsonObject toolTestAutoReplyRule(const QJsonObject &args);
	QJsonObject toolGetAutoReplyStats(const QJsonObject &args);

	// Voice/Video tools
	QJsonObject toolListVoicePersonas(const QJsonObject &args);
	QJsonObject toolTextToSpeech(const QJsonObject &args);
	QJsonObject toolTextToVideo(const QJsonObject &args);

	// Wallet feature tools
	QJsonObject toolCategorizeTransaction(const QJsonObject &args);
	QJsonObject toolSendGift(const QJsonObject &args);
	QJsonObject toolBuyGift(const QJsonObject &args);
	QJsonObject toolGetGiftHistory(const QJsonObject &args);
	QJsonObject toolGetGiftSuggestions(const QJsonObject &args);
	QJsonObject toolGetSubscriptionStats(const QJsonObject &args);
	QJsonObject toolGetSubscriberAnalytics(const QJsonObject &args);
	QJsonObject toolGetMonetizationAnalytics(const QJsonObject &args);
	QJsonObject toolSetMonetizationRules(const QJsonObject &args);
	QJsonObject toolGetEarnings(const QJsonObject &args);
	QJsonObject toolWithdrawEarnings(const QJsonObject &args);
	QJsonObject toolSetSpendingBudget(const QJsonObject &args);
	QJsonObject toolSetBudgetAlert(const QJsonObject &args);
	QJsonObject toolRequestStars(const QJsonObject &args);
	QJsonObject toolGetStarsHistory(const QJsonObject &args);
	QJsonObject toolConvertStars(const QJsonObject &args);
	QJsonObject toolGetStarsRate(const QJsonObject &args);

	// Stars feature tools
	QJsonObject toolCreateGiftCollection(const QJsonObject &args);
	QJsonObject toolAddToCollection(const QJsonObject &args);
	QJsonObject toolRemoveFromCollection(const QJsonObject &args);
	QJsonObject toolShareCollection(const QJsonObject &args);
	QJsonObject toolCreateGiftAuction(const QJsonObject &args);
	QJsonObject toolListAuctions(const QJsonObject &args);
	QJsonObject toolPlaceBid(const QJsonObject &args);
	QJsonObject toolCancelAuction(const QJsonObject &args);
	QJsonObject toolGetAuctionStatus(const QJsonObject &args);
	QJsonObject toolListMarketplace(const QJsonObject &args);
	QJsonObject toolDelistGift(const QJsonObject &args);
	QJsonObject toolListAvailableGifts(const QJsonObject &args);
	QJsonObject toolGetGiftPriceHistory(const QJsonObject &args);
	QJsonObject toolGetPricePredictions(const QJsonObject &args);
	QJsonObject toolSendStarReaction(const QJsonObject &args);
	QJsonObject toolGetStarReactions(const QJsonObject &args);
	QJsonObject toolGetReactionAnalytics(const QJsonObject &args);
	QJsonObject toolSetReactionPrice(const QJsonObject &args);
	QJsonObject toolGetTopReacted(const QJsonObject &args);
	QJsonObject toolCreatePaidPost(const QJsonObject &args);
	QJsonObject toolSetContentPrice(const QJsonObject &args);
	QJsonObject toolGetPaidContentStats(const QJsonObject &args);
	QJsonObject toolListPurchasedContent(const QJsonObject &args);
	QJsonObject toolUnlockContent(const QJsonObject &args);
	QJsonObject toolRefundContent(const QJsonObject &args);
	QJsonObject toolGetPortfolio(const QJsonObject &args);
	QJsonObject toolGetPortfolioHistory(const QJsonObject &args);
	QJsonObject toolGetPortfolioValue(const QJsonObject &args);
	QJsonObject toolSetPriceAlert(const QJsonObject &args);
	QJsonObject toolListAchievements(const QJsonObject &args);
	QJsonObject toolGetAchievementProgress(const QJsonObject &args);
	QJsonObject toolClaimAchievementReward(const QJsonObject &args);
	QJsonObject toolGetLeaderboard(const QJsonObject &args);
	QJsonObject toolShareAchievement(const QJsonObject &args);
	QJsonObject toolGetAchievementSuggestions(const QJsonObject &args);
	QJsonObject toolCreateExclusiveContent(const QJsonObject &args);
	QJsonObject toolSetSubscriberTiers(const QJsonObject &args);
	QJsonObject toolSendSubscriberMessage(const QJsonObject &args);
	QJsonObject toolGetCreatorDashboard(const QJsonObject &args);
	QJsonObject toolGetStarsLeaderboard(const QJsonObject &args);

	// Subscription tools
	QJsonObject toolSubscribeToChannel(const QJsonObject &args);
	QJsonObject toolUnsubscribeFromChannel(const QJsonObject &args);
	QJsonObject toolCreateGiveaway(const QJsonObject &args);

	// Miniapp tools
	QJsonObject toolListMiniappPermissions(const QJsonObject &args);
	QJsonObject toolApproveMiniappSpend(const QJsonObject &args);
	QJsonObject toolRevokeMiniappPermission(const QJsonObject &args);

	// Testing tools
	QJsonObject toolTestAway(const QJsonObject &args);

	// ============================================================
	// GRADUAL EXPORT TOOLS (7 tools)
	// ============================================================
	QJsonObject toolStartGradualExport(const QJsonObject &args);
	QJsonObject toolGetGradualExportStatus(const QJsonObject &args);
	QJsonObject toolPauseGradualExport(const QJsonObject &args);
	QJsonObject toolResumeGradualExport(const QJsonObject &args);
	QJsonObject toolCancelGradualExport(const QJsonObject &args);
	QJsonObject toolGetGradualExportConfig(const QJsonObject &args);
	QJsonObject toolSetGradualExportConfig(const QJsonObject &args);
	QJsonObject toolQueueGradualExport(const QJsonObject &args);
	QJsonObject toolGetGradualExportQueue(const QJsonObject &args);

	// ============================================================
	// LOCAL DATABASE MANAGEMENT
	// ============================================================
	void initializeLocalDatabase();
	bool ensureDatabaseSchema();

	// Stdio transport
	void startStdioTransport();
	void handleStdioInput();

	// HTTP transport
	void startHttpTransport(int port = 8000);

	// Error response helper
	QJsonObject errorResponse(
		const QJsonValue &id,
		int code,
		const QString &message
	);

	// Success response helper
	QJsonObject successResponse(
		const QJsonValue &id,
		const QJsonObject &result
	);

	// ============================================================
	// HELPER METHODS
	// ============================================================

	// Input validation helper - checks required fields exist and returns error message
	bool validateRequired(
		const QJsonObject &args,
		const QStringList &requiredFields,
		QString &errorMessage
	);

	// Create standardized error response for tools
	QJsonObject toolError(const QString &message, const QJsonObject &context = QJsonObject());

	// Extract message data to JSON - reduces code duplication
	QJsonObject extractMessageJson(HistoryItem *item);

	// Tool dispatcher type alias
	using ToolHandler = std::function<QJsonObject(const QJsonObject&)>;

	// Initialize tool dispatcher lookup table
	void initializeToolHandlers();

private:
	ServerInfo _serverInfo;
	TransportType _transport = TransportType::Stdio;

	// Registered MCP components
	QVector<Tool> _tools;
	QVector<Resource> _resources;
	QVector<Prompt> _prompts;

	// Transports (owned)
	std::unique_ptr<QTextStream> _stdin;
	std::unique_ptr<QTextStream> _stdout;
	std::unique_ptr<QTcpServer> _httpServer;

	// Feature components (owned)
	QSqlDatabase _db;
	std::unique_ptr<ChatArchiver> _archiver;
	std::unique_ptr<EphemeralArchiver> _ephemeralArchiver;
	std::unique_ptr<Analytics> _analytics;
	std::unique_ptr<SemanticSearch> _semanticSearch;
	std::unique_ptr<BatchOperations> _batchOps;
	std::unique_ptr<MessageScheduler> _scheduler;
	std::unique_ptr<AuditLogger> _auditLogger;
	std::unique_ptr<RBAC> _rbac;
	std::unique_ptr<VoiceTranscription> _voiceTranscription;
	std::unique_ptr<TextToSpeech> _textToSpeech;
	std::unique_ptr<LocalLLM> _localLLM;
	std::unique_ptr<VideoGenerator> _videoGenerator;
	std::unique_ptr<TonWallet> _tonWallet;
	std::unique_ptr<BotManager> _botManager;
	std::unique_ptr<CacheManager> _cache;
	std::unique_ptr<GradualArchiver> _gradualArchiver;

	// State
	bool _initialized = false;
	QString _databasePath;
	Main::Session *_session = nullptr;
	QDateTime _startTime = QDateTime::currentDateTime();

	// Tool dispatcher lookup table
	QHash<QString, ToolHandler> _toolHandlers;

	// RPL lifetime for session event subscriptions
	std::unique_ptr<rpl::lifetime> _lifetime;

	// Active export tracking (non-blocking export_chat)
	struct ActiveExport {
		qint64 chatId = 0;
		QString chatName;
		QString chatType;
		QString outputPath;
		bool finished = false;
		bool success = false;
		QString finishedPath;
		int filesCount = 0;
		int64_t bytesCount = 0;
		QString errorMessage;
		int currentStep = -1;
		QDateTime startTime;
	};
	std::unique_ptr<Export::Controller> _exportController;
	std::unique_ptr<ActiveExport> _activeExport;
};

} // namespace MCP
