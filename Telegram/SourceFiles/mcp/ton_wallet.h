// TON/Fragment wallet integration for MCP crypto payment tools.
// Uses tonsdk Python library via QProcess subprocess for TON transactions.
//
// This file is part of Telegram Desktop MCP integration.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QDateTime>
#include <QtSql/QSqlDatabase>

namespace MCP {

// TON wallet provider / backend
enum class TonProvider {
	TonSdk,       // tonsdk Python library (pip install tonsdk)
	TonUtils,     // tonutils Python library (higher-level)
	TonCli,       // ton CLI tools (lite-client, etc.)
};

// Result of a wallet operation
struct WalletResult {
	bool success = false;
	QString error;

	// Wallet info (from create/import)
	QString address;           // Friendly address (EQ...)
	QString rawAddress;        // Raw address (0:hex...)
	QStringList mnemonics;     // 24-word seed phrase (only on create)

	// Balance
	double balanceTon = 0.0;
	qint64 balanceNano = 0;   // nanoTON (1 TON = 1e9 nanoTON)

	// Transaction
	QString txHash;
	QString bocBase64;         // Serialized BOC for broadcasting
	double amountTon = 0.0;
	QString recipient;
	double feeTon = 0.0;
	QString status;            // "prepared", "broadcast", "confirmed"

	QDateTime timestamp;
};

// TON transaction history entry
struct TonTransaction {
	QString hash;
	qint64 lt = 0;             // Logical time
	double amountTon = 0.0;
	QString from;
	QString to;
	QString comment;           // Transfer comment/memo
	QDateTime timestamp;
	bool isIncoming = false;
};

// TON wallet service (subprocess-based, uses Python tonsdk)
class TonWallet : public QObject {
	Q_OBJECT

public:
	explicit TonWallet(QObject *parent = nullptr);
	~TonWallet();

	// Lifecycle
	bool start(QSqlDatabase *db);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }
	[[nodiscard]] TonProvider provider() const { return _provider; }

	// Configuration
	void setProvider(TonProvider provider) { _provider = provider; }
	void setPythonPath(const QString &path) { _pythonPath = path; }
	void setTonCenterUrl(const QString &url) { _tonCenterUrl = url; }
	void setTonCenterApiKey(const QString &key) { _tonCenterApiKey = key; }
	void setNetwork(const QString &network) { _network = network; }

	[[nodiscard]] QString network() const { return _network; }

	// ============================================================
	// WALLET MANAGEMENT
	// ============================================================

	// Create a new TON wallet (generates mnemonic + address)
	WalletResult createWallet();

	// Import wallet from 24-word mnemonic
	WalletResult importWallet(const QStringList &mnemonics);

	// Get wallet address (from stored wallet)
	WalletResult getWalletAddress();

	// Check if wallet is configured
	[[nodiscard]] bool hasWallet() const;

	// ============================================================
	// BALANCE & INFO
	// ============================================================

	// Get current balance from TON network
	WalletResult getBalance();

	// Get balance from cache (no network)
	WalletResult getCachedBalance();

	// ============================================================
	// TRANSACTIONS
	// ============================================================

	// Create and sign a TON transfer (does NOT broadcast)
	WalletResult createTransfer(
		const QString &recipientAddress,
		double amountTon,
		const QString &comment = QString());

	// Broadcast a signed transaction BOC to the network
	WalletResult broadcastTransaction(const QString &bocBase64);

	// Create + broadcast in one step
	WalletResult sendPayment(
		const QString &recipientAddress,
		double amountTon,
		const QString &comment = QString());

	// Get transaction history
	QList<TonTransaction> getTransactionHistory(int limit = 20);

	// ============================================================
	// JETTON (TOKEN) SUPPORT
	// ============================================================

	// Get jetton (token) balances
	QJsonArray getJettonBalances();

	// Transfer jettons
	WalletResult sendJetton(
		const QString &jettonMaster,
		const QString &recipientAddress,
		double amount,
		const QString &comment = QString());

	// ============================================================
	// STATISTICS
	// ============================================================

	struct WalletStats {
		int totalTransactions = 0;
		int successfulTransactions = 0;
		int failedTransactions = 0;
		double totalSentTon = 0.0;
		double totalReceivedTon = 0.0;
		QDateTime lastTransaction;
	};

	[[nodiscard]] WalletStats getStats() const { return _stats; }

Q_SIGNALS:
	void paymentCompleted(const WalletResult &result);
	void paymentFailed(const QString &error);
	void balanceUpdated(double balanceTon);

private:
	// Run a Python tonsdk script and return JSON output
	QJsonObject runTonScript(const QString &script);

	// Run a tonutils Python snippet
	QJsonObject runTonUtilsScript(const QString &script);

	// Find Python binary with tonsdk installed
	QString findPython() const;

	// Auto-detect which provider is available
	void autoDetectProvider();

	// Store/retrieve wallet credentials (encrypted in DB)
	bool storeWalletKey(const QString &key, const QString &value);
	QString getWalletKey(const QString &key) const;

	// Store transaction in local DB
	void recordTransaction(const WalletResult &result);

	// Validate TON address format
	static bool isValidTonAddress(const QString &address);

	QSqlDatabase *_db = nullptr;
	bool _isRunning = false;
	TonProvider _provider = TonProvider::TonSdk;

	QString _pythonPath;        // Path to python3 binary
	QString _tonCenterUrl;      // TonCenter API URL (mainnet/testnet)
	QString _tonCenterApiKey;   // Optional API key for TonCenter
	QString _network = "mainnet"; // "mainnet" or "testnet"

	WalletStats _stats;
};

} // namespace MCP
