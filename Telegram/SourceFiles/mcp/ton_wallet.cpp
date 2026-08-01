// TON/Fragment wallet integration for MCP crypto payment tools.
// Uses tonsdk Python library via QProcess subprocess for TON transactions.
//
// This file is part of Telegram Desktop MCP integration.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "ton_wallet.h"

#include <QtCore/QProcess>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QCryptographicHash>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

namespace {

constexpr auto kTonCenterMainnet = "https://toncenter.com/api/v2/";
constexpr auto kTonCenterTestnet = "https://testnet.toncenter.com/api/v2/";
constexpr auto kProcessTimeoutMs = 30000; // 30 seconds
constexpr auto kBroadcastTimeoutMs = 15000;

} // namespace

TonWallet::TonWallet(QObject *parent)
	: QObject(parent) {
}

TonWallet::~TonWallet() {
	stop();
}

bool TonWallet::start(QSqlDatabase *db) {
	if (_isRunning) {
		return true;
	}
	_db = db;
	if (!_db || !_db->isOpen()) {
		return false;
	}

	// Create tables
	QSqlQuery query(*_db);
	query.exec(
		"CREATE TABLE IF NOT EXISTS ton_wallet ("
		"  key TEXT PRIMARY KEY,"
		"  value TEXT NOT NULL"
		")");
	query.exec(
		"CREATE TABLE IF NOT EXISTS ton_transactions ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  tx_hash TEXT,"
		"  recipient TEXT,"
		"  amount_ton REAL,"
		"  fee_ton REAL,"
		"  comment TEXT,"
		"  status TEXT,"
		"  boc_base64 TEXT,"
		"  created_at INTEGER DEFAULT (strftime('%s','now'))"
		")");
	query.exec(
		"CREATE TABLE IF NOT EXISTS ton_config ("
		"  key TEXT PRIMARY KEY,"
		"  value TEXT"
		")");

	// Load stored config
	query.prepare("SELECT value FROM ton_config WHERE key = ?");
	query.addBindValue("python_path");
	if (query.exec() && query.next()) {
		_pythonPath = query.value(0).toString();
	}
	query.prepare("SELECT value FROM ton_config WHERE key = ?");
	query.addBindValue("network");
	if (query.exec() && query.next()) {
		_network = query.value(0).toString();
	}
	query.prepare("SELECT value FROM ton_config WHERE key = ?");
	query.addBindValue("toncenter_api_key");
	if (query.exec() && query.next()) {
		_tonCenterApiKey = query.value(0).toString();
	}
	query.prepare("SELECT value FROM ton_config WHERE key = ?");
	query.addBindValue("toncenter_url");
	if (query.exec() && query.next()) {
		_tonCenterUrl = query.value(0).toString();
	}

	// Auto-detect provider
	autoDetectProvider();

	// Set default TonCenter URL based on network
	if (_tonCenterUrl.isEmpty()) {
		_tonCenterUrl = (_network == "testnet")
			? QString::fromLatin1(kTonCenterTestnet)
			: QString::fromLatin1(kTonCenterMainnet);
	}

	// Load stats from DB
	query.exec(
		"SELECT COUNT(*), "
		"  SUM(CASE WHEN status='broadcast' OR status='confirmed' THEN 1 ELSE 0 END), "
		"  SUM(CASE WHEN status='failed' THEN 1 ELSE 0 END), "
		"  SUM(CASE WHEN amount_ton > 0 THEN amount_ton ELSE 0 END), "
		"  MAX(created_at) "
		"FROM ton_transactions");
	if (query.next()) {
		_stats.totalTransactions = query.value(0).toInt();
		_stats.successfulTransactions = query.value(1).toInt();
		_stats.failedTransactions = query.value(2).toInt();
		_stats.totalSentTon = query.value(3).toDouble();
		auto lastTs = query.value(4).toLongLong();
		if (lastTs > 0) {
			_stats.lastTransaction = QDateTime::fromSecsSinceEpoch(lastTs);
		}
	}

	_isRunning = true;
	return true;
}

void TonWallet::stop() {
	_isRunning = false;
}

void TonWallet::autoDetectProvider() {
	// Check if tonsdk is available
	const auto python = findPython();
	if (python.isEmpty()) {
		fprintf(stderr, "[MCP] TonWallet: No Python3 found\n");
		fflush(stderr);
		return;
	}
	_pythonPath = python;

	QProcess proc;
	proc.setProgram(python);
	proc.setArguments({"-c", "import tonsdk; print('tonsdk_ok')"});
	proc.start();
	if (proc.waitForFinished(5000) && proc.exitCode() == 0) {
		auto out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
		if (out.contains("tonsdk_ok")) {
			_provider = TonProvider::TonSdk;
			fprintf(stderr, "[MCP] TonWallet: Using tonsdk provider\n");
			fflush(stderr);
			return;
		}
	}

	// Try tonutils
	proc.setArguments({"-c", "import tonutils; print('tonutils_ok')"});
	proc.start();
	if (proc.waitForFinished(5000) && proc.exitCode() == 0) {
		auto out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
		if (out.contains("tonutils_ok")) {
			_provider = TonProvider::TonUtils;
			fprintf(stderr, "[MCP] TonWallet: Using tonutils provider\n");
			fflush(stderr);
			return;
		}
	}

	fprintf(stderr, "[MCP] TonWallet: No TON library found (install: pip install tonsdk)\n");
	fflush(stderr);
}

QString TonWallet::findPython() const {
	if (!_pythonPath.isEmpty()) {
		QFileInfo fi(_pythonPath);
		if (fi.exists() && fi.isExecutable()) {
			return _pythonPath;
		}
	}

	// Check common paths
	const QStringList candidates = {
		"/opt/homebrew/bin/python3",
		"/usr/local/bin/python3",
		"/usr/bin/python3",
		QDir::homePath() + "/.local/bin/python3",
	};
	for (const auto &path : candidates) {
		QFileInfo fi(path);
		if (fi.exists() && fi.isExecutable()) {
			return path;
		}
	}

	// Try which
	QProcess proc;
	proc.setProgram("which");
	proc.setArguments({"python3"});
	proc.start();
	if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
		auto path = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
		if (!path.isEmpty()) {
			return path;
		}
	}
	return QString();
}

QJsonObject TonWallet::runTonScript(const QString &script) {
	const auto python = findPython();
	if (python.isEmpty()) {
		QJsonObject err;
		err["success"] = false;
		err["error"] = "Python3 not found";
		return err;
	}

	QProcess proc;
	proc.setProgram(python);
	proc.setArguments({"-c", script});

	// Pass environment variables
	auto env = QProcessEnvironment::systemEnvironment();
	if (!_tonCenterApiKey.isEmpty()) {
		env.insert("TONCENTER_API_KEY", _tonCenterApiKey);
	}
	env.insert("TONCENTER_URL", _tonCenterUrl);
	env.insert("TON_NETWORK", _network);
	proc.setProcessEnvironment(env);

	proc.start();
	if (!proc.waitForFinished(kProcessTimeoutMs)) {
		proc.kill();
		QJsonObject err;
		err["success"] = false;
		err["error"] = "Python script timed out";
		return err;
	}

	if (proc.exitCode() != 0) {
		auto stderr_out = QString::fromUtf8(proc.readAllStandardError()).trimmed();
		QJsonObject err;
		err["success"] = false;
		err["error"] = "Script failed: " + stderr_out;
		return err;
	}

	auto stdout_out = proc.readAllStandardOutput();
	auto doc = QJsonDocument::fromJson(stdout_out);
	if (doc.isNull()) {
		QJsonObject err;
		err["success"] = false;
		err["error"] = "Invalid JSON output from script";
		return err;
	}
	return doc.object();
}

bool TonWallet::hasWallet() const {
	if (!_db || !_db->isOpen()) {
		return false;
	}
	QSqlQuery query(*_db);
	query.prepare("SELECT value FROM ton_wallet WHERE key = 'address'");
	if (query.exec() && query.next()) {
		return !query.value(0).toString().isEmpty();
	}
	return false;
}

bool TonWallet::storeWalletKey(const QString &key, const QString &value) {
	if (!_db || !_db->isOpen()) {
		return false;
	}
	QSqlQuery query(*_db);
	query.prepare(
		"INSERT OR REPLACE INTO ton_wallet (key, value) VALUES (?, ?)");
	query.addBindValue(key);
	query.addBindValue(value);
	return query.exec();
}

QString TonWallet::getWalletKey(const QString &key) const {
	if (!_db || !_db->isOpen()) {
		return QString();
	}
	QSqlQuery query(*_db);
	query.prepare("SELECT value FROM ton_wallet WHERE key = ?");
	query.addBindValue(key);
	if (query.exec() && query.next()) {
		return query.value(0).toString();
	}
	return QString();
}

bool TonWallet::isValidTonAddress(const QString &address) {
	// User-friendly address: starts with EQ or UQ, base64, 48 chars
	if ((address.startsWith("EQ") || address.startsWith("UQ"))
		&& address.length() == 48) {
		return true;
	}
	// Raw address: 0:hex (66 chars)
	if (address.startsWith("0:") && address.length() == 66) {
		return true;
	}
	// Also accept -1: for masterchain
	if (address.startsWith("-1:") && address.length() == 67) {
		return true;
	}
	return false;
}

WalletResult TonWallet::createWallet() {
	WalletResult result;
	result.timestamp = QDateTime::currentDateTimeUtc();

	if (hasWallet()) {
		result.success = false;
		result.error = "Wallet already exists. Use importWallet to replace.";
		result.address = getWalletKey("address");
		return result;
	}

	const auto script = QStringLiteral(
		"import json\n"
		"from tonsdk.contract.wallet import Wallets, WalletVersionEnum\n"
		"from tonsdk.utils import bytes_to_b64str\n"
		"\n"
		"mnemonics, pub_k, priv_k, wallet = Wallets.create(\n"
		"    WalletVersionEnum.v4r2, workchain=0)\n"
		"\n"
		"address = wallet.address.to_string(True, True, True)\n"
		"raw_address = wallet.address.to_string(False)\n"
		"\n"
		"print(json.dumps({\n"
		"    'success': True,\n"
		"    'address': address,\n"
		"    'raw_address': raw_address,\n"
		"    'mnemonics': mnemonics,\n"
		"}))\n"
	);

	auto json = runTonScript(script);
	if (!json["success"].toBool()) {
		result.success = false;
		result.error = json["error"].toString("Failed to create wallet");
		return result;
	}

	result.success = true;
	result.address = json["address"].toString();
	result.rawAddress = json["raw_address"].toString();

	auto mnArray = json["mnemonics"].toArray();
	for (const auto &m : mnArray) {
		result.mnemonics.append(m.toString());
	}

	// Store wallet data in DB
	storeWalletKey("address", result.address);
	storeWalletKey("raw_address", result.rawAddress);
	storeWalletKey("mnemonics", result.mnemonics.join(" "));
	storeWalletKey("version", "v4r2");
	storeWalletKey("created_at",
		QString::number(QDateTime::currentSecsSinceEpoch()));

	return result;
}

WalletResult TonWallet::importWallet(const QStringList &mnemonics) {
	WalletResult result;
	result.timestamp = QDateTime::currentDateTimeUtc();

	if (mnemonics.size() != 24) {
		result.success = false;
		result.error = "Expected 24 mnemonic words, got "
			+ QString::number(mnemonics.size());
		return result;
	}

	// Build Python list string from mnemonics
	QString mnPythonList = "[";
	for (int i = 0; i < mnemonics.size(); ++i) {
		if (i > 0) mnPythonList += ", ";
		auto word = mnemonics[i];
		word.replace("'", "\\'");
		mnPythonList += "'" + word + "'";
	}
	mnPythonList += "]";

	const auto script = QStringLiteral(
		"import json\n"
		"from tonsdk.contract.wallet import Wallets, WalletVersionEnum\n"
		"\n"
		"mnemonics = %1\n"
		"_mnemonics, pub_k, priv_k, wallet = Wallets.from_mnemonics(\n"
		"    mnemonics, WalletVersionEnum.v4r2, workchain=0)\n"
		"\n"
		"address = wallet.address.to_string(True, True, True)\n"
		"raw_address = wallet.address.to_string(False)\n"
		"\n"
		"print(json.dumps({\n"
		"    'success': True,\n"
		"    'address': address,\n"
		"    'raw_address': raw_address,\n"
		"}))\n"
	).arg(mnPythonList);

	auto json = runTonScript(script);
	if (!json["success"].toBool()) {
		result.success = false;
		result.error = json["error"].toString("Failed to import wallet");
		return result;
	}

	result.success = true;
	result.address = json["address"].toString();
	result.rawAddress = json["raw_address"].toString();
	result.mnemonics = mnemonics;

	// Store in DB (replace any existing)
	storeWalletKey("address", result.address);
	storeWalletKey("raw_address", result.rawAddress);
	storeWalletKey("mnemonics", mnemonics.join(" "));
	storeWalletKey("version", "v4r2");
	storeWalletKey("imported_at",
		QString::number(QDateTime::currentSecsSinceEpoch()));

	return result;
}

WalletResult TonWallet::getWalletAddress() {
	WalletResult result;
	result.timestamp = QDateTime::currentDateTimeUtc();

	if (!hasWallet()) {
		result.success = false;
		result.error = "No wallet configured";
		return result;
	}

	result.success = true;
	result.address = getWalletKey("address");
	result.rawAddress = getWalletKey("raw_address");
	return result;
}

WalletResult TonWallet::getBalance() {
	WalletResult result;
	result.timestamp = QDateTime::currentDateTimeUtc();

	const auto address = getWalletKey("address");
	if (address.isEmpty()) {
		result.success = false;
		result.error = "No wallet configured";
		return result;
	}

	auto escapedAddr = address;
	escapedAddr.replace("'", "\\'");

	auto escapedUrl = _tonCenterUrl;
	escapedUrl.replace("'", "\\'");

	const auto script = QStringLiteral(
		"import json, urllib.request, os\n"
		"\n"
		"address = '%1'\n"
		"base_url = '%2'\n"
		"api_key = os.environ.get('TONCENTER_API_KEY', '')\n"
		"\n"
		"url = base_url + 'getAddressBalance?address=' + address\n"
		"req = urllib.request.Request(url)\n"
		"if api_key:\n"
		"    req.add_header('X-API-Key', api_key)\n"
		"\n"
		"resp = urllib.request.urlopen(req, timeout=10)\n"
		"data = json.loads(resp.read())\n"
		"\n"
		"if data.get('ok'):\n"
		"    balance_nano = int(data['result'])\n"
		"    balance_ton = balance_nano / 1e9\n"
		"    print(json.dumps({\n"
		"        'success': True,\n"
		"        'balance_ton': balance_ton,\n"
		"        'balance_nano': balance_nano,\n"
		"        'address': address,\n"
		"    }))\n"
		"else:\n"
		"    print(json.dumps({\n"
		"        'success': False,\n"
		"        'error': data.get('error', 'Unknown error'),\n"
		"    }))\n"
	).arg(escapedAddr, escapedUrl);

	auto json = runTonScript(script);
	if (!json["success"].toBool()) {
		result.success = false;
		result.error = json["error"].toString("Failed to get balance");
		return result;
	}

	result.success = true;
	result.address = address;
	result.balanceTon = json["balance_ton"].toDouble();
	result.balanceNano = json["balance_nano"].toVariant().toLongLong();

	// Cache in DB
	storeWalletKey("cached_balance_ton",
		QString::number(result.balanceTon, 'f', 9));
	storeWalletKey("cached_balance_at",
		QString::number(QDateTime::currentSecsSinceEpoch()));

	Q_EMIT balanceUpdated(result.balanceTon);
	return result;
}

WalletResult TonWallet::getCachedBalance() {
	WalletResult result;
	result.timestamp = QDateTime::currentDateTimeUtc();

	if (!hasWallet()) {
		result.success = false;
		result.error = "No wallet configured";
		return result;
	}

	result.success = true;
	result.address = getWalletKey("address");
	result.balanceTon = getWalletKey("cached_balance_ton").toDouble();
	result.balanceNano = static_cast<qint64>(result.balanceTon * 1e9);
	return result;
}

WalletResult TonWallet::createTransfer(
		const QString &recipientAddress,
		double amountTon,
		const QString &comment) {
	WalletResult result;
	result.timestamp = QDateTime::currentDateTimeUtc();
	result.recipient = recipientAddress;
	result.amountTon = amountTon;

	if (!isValidTonAddress(recipientAddress)) {
		result.success = false;
		result.error = "Invalid TON address format";
		return result;
	}

	if (amountTon <= 0) {
		result.success = false;
		result.error = "Amount must be positive";
		return result;
	}

	const auto mnemonicsStr = getWalletKey("mnemonics");
	if (mnemonicsStr.isEmpty()) {
		result.success = false;
		result.error = "No wallet mnemonics stored";
		return result;
	}

	// Build Python mnemonic list
	auto words = mnemonicsStr.split(' ');
	QString mnPythonList = "[";
	for (int i = 0; i < words.size(); ++i) {
		if (i > 0) mnPythonList += ", ";
		auto word = words[i];
		word.replace("'", "\\'");
		mnPythonList += "'" + word + "'";
	}
	mnPythonList += "]";

	qint64 amountNano = static_cast<qint64>(amountTon * 1e9);

	auto escapedRecipient = recipientAddress;
	escapedRecipient.replace("'", "\\'");
	auto escapedComment = comment;
	escapedComment.replace("'", "\\'");
	escapedComment.replace("\\", "\\\\");

	const auto script = QStringLiteral(
		"import json\n"
		"from tonsdk.contract.wallet import Wallets, WalletVersionEnum\n"
		"from tonsdk.utils import bytes_to_b64str, to_nano\n"
		"\n"
		"mnemonics = %1\n"
		"_mn, pub_k, priv_k, wallet = Wallets.from_mnemonics(\n"
		"    mnemonics, WalletVersionEnum.v4r2, workchain=0)\n"
		"\n"
		"# Create transfer message\n"
		"query = wallet.create_transfer_message(\n"
		"    to_addr='%2',\n"
		"    amount=to_nano(%3, 'ton'),\n"
		"    seqno=0,\n"
		"    payload='%4' if '%4' else None,\n"
		")\n"
		"\n"
		"boc = bytes_to_b64str(query['message'].to_boc(False))\n"
		"\n"
		"print(json.dumps({\n"
		"    'success': True,\n"
		"    'boc_base64': boc,\n"
		"    'recipient': '%2',\n"
		"    'amount_nano': %5,\n"
		"    'status': 'prepared',\n"
		"}))\n"
	).arg(mnPythonList, escapedRecipient,
		  QString::number(amountTon, 'f', 9),
		  escapedComment,
		  QString::number(amountNano));

	auto json = runTonScript(script);
	if (!json["success"].toBool()) {
		result.success = false;
		result.error = json["error"].toString("Failed to create transfer");
		return result;
	}

	result.success = true;
	result.bocBase64 = json["boc_base64"].toString();
	result.status = "prepared";

	return result;
}

WalletResult TonWallet::broadcastTransaction(const QString &bocBase64) {
	WalletResult result;
	result.timestamp = QDateTime::currentDateTimeUtc();

	if (bocBase64.isEmpty()) {
		result.success = false;
		result.error = "Empty BOC data";
		return result;
	}

	auto escapedBoc = bocBase64;
	escapedBoc.replace("'", "\\'");

	auto escapedUrl = _tonCenterUrl;
	escapedUrl.replace("'", "\\'");

	const auto script = QStringLiteral(
		"import json, urllib.request, os\n"
		"\n"
		"boc = '%1'\n"
		"base_url = '%2'\n"
		"api_key = os.environ.get('TONCENTER_API_KEY', '')\n"
		"\n"
		"url = base_url + 'sendBoc'\n"
		"payload = json.dumps({'boc': boc}).encode()\n"
		"req = urllib.request.Request(url, data=payload,\n"
		"    headers={'Content-Type': 'application/json'})\n"
		"if api_key:\n"
		"    req.add_header('X-API-Key', api_key)\n"
		"\n"
		"resp = urllib.request.urlopen(req, timeout=15)\n"
		"data = json.loads(resp.read())\n"
		"\n"
		"if data.get('ok'):\n"
		"    print(json.dumps({\n"
		"        'success': True,\n"
		"        'status': 'broadcast',\n"
		"        'hash': data.get('result', {}).get('hash', ''),\n"
		"    }))\n"
		"else:\n"
		"    print(json.dumps({\n"
		"        'success': False,\n"
		"        'error': data.get('error', 'Broadcast failed'),\n"
		"    }))\n"
	).arg(escapedBoc, escapedUrl);

	auto json = runTonScript(script);
	if (!json["success"].toBool()) {
		result.success = false;
		result.error = json["error"].toString("Broadcast failed");
		result.status = "failed";
		return result;
	}

	result.success = true;
	result.status = "broadcast";
	result.txHash = json["hash"].toString();
	result.bocBase64 = bocBase64;

	return result;
}

WalletResult TonWallet::sendPayment(
		const QString &recipientAddress,
		double amountTon,
		const QString &comment) {
	WalletResult result;
	result.timestamp = QDateTime::currentDateTimeUtc();
	result.recipient = recipientAddress;
	result.amountTon = amountTon;

	// Step 1: Get current seqno from the network
	const auto address = getWalletKey("address");
	if (address.isEmpty()) {
		result.success = false;
		result.error = "No wallet configured";
		return result;
	}

	if (!isValidTonAddress(recipientAddress)) {
		result.success = false;
		result.error = "Invalid TON address format";
		return result;
	}

	if (amountTon <= 0) {
		result.success = false;
		result.error = "Amount must be positive";
		return result;
	}

	const auto mnemonicsStr = getWalletKey("mnemonics");
	if (mnemonicsStr.isEmpty()) {
		result.success = false;
		result.error = "No wallet mnemonics stored";
		return result;
	}

	// Build Python mnemonic list
	auto words = mnemonicsStr.split(' ');
	QString mnPythonList = "[";
	for (int i = 0; i < words.size(); ++i) {
		if (i > 0) mnPythonList += ", ";
		auto word = words[i];
		word.replace("'", "\\'");
		mnPythonList += "'" + word + "'";
	}
	mnPythonList += "]";

	auto escapedAddr = address;
	escapedAddr.replace("'", "\\'");
	auto escapedRecipient = recipientAddress;
	escapedRecipient.replace("'", "\\'");
	auto escapedComment = comment;
	escapedComment.replace("'", "\\'");
	escapedComment.replace("\\", "\\\\");
	auto escapedUrl = _tonCenterUrl;
	escapedUrl.replace("'", "\\'");

	// Full pipeline: get seqno → create transfer → broadcast
	const auto script = QStringLiteral(
		"import json, urllib.request, os\n"
		"from tonsdk.contract.wallet import Wallets, WalletVersionEnum\n"
		"from tonsdk.utils import bytes_to_b64str, to_nano\n"
		"\n"
		"mnemonics = %1\n"
		"address = '%2'\n"
		"recipient = '%3'\n"
		"amount = %4\n"
		"comment = '%5'\n"
		"base_url = '%6'\n"
		"api_key = os.environ.get('TONCENTER_API_KEY', '')\n"
		"\n"
		"# Step 1: Get seqno\n"
		"url = base_url + 'runGetMethod'\n"
		"payload = json.dumps({\n"
		"    'address': address,\n"
		"    'method': 'seqno',\n"
		"    'stack': []\n"
		"}).encode()\n"
		"req = urllib.request.Request(url, data=payload,\n"
		"    headers={'Content-Type': 'application/json'})\n"
		"if api_key:\n"
		"    req.add_header('X-API-Key', api_key)\n"
		"\n"
		"seqno = 0\n"
		"try:\n"
		"    resp = urllib.request.urlopen(req, timeout=10)\n"
		"    data = json.loads(resp.read())\n"
		"    if data.get('ok'):\n"
		"        stack = data.get('result', {}).get('stack', [])\n"
		"        if stack:\n"
		"            seqno = int(stack[0][1], 16)\n"
		"except Exception:\n"
		"    seqno = 0  # New wallet, seqno=0\n"
		"\n"
		"# Step 2: Create and sign transfer\n"
		"_mn, pub_k, priv_k, wallet = Wallets.from_mnemonics(\n"
		"    mnemonics, WalletVersionEnum.v4r2, workchain=0)\n"
		"\n"
		"query = wallet.create_transfer_message(\n"
		"    to_addr=recipient,\n"
		"    amount=to_nano(amount, 'ton'),\n"
		"    seqno=seqno,\n"
		"    payload=comment if comment else None,\n"
		")\n"
		"\n"
		"boc = bytes_to_b64str(query['message'].to_boc(False))\n"
		"\n"
		"# Step 3: Broadcast\n"
		"url = base_url + 'sendBoc'\n"
		"payload = json.dumps({'boc': boc}).encode()\n"
		"req = urllib.request.Request(url, data=payload,\n"
		"    headers={'Content-Type': 'application/json'})\n"
		"if api_key:\n"
		"    req.add_header('X-API-Key', api_key)\n"
		"\n"
		"resp = urllib.request.urlopen(req, timeout=15)\n"
		"data = json.loads(resp.read())\n"
		"\n"
		"if data.get('ok'):\n"
		"    tx_hash = data.get('result', {}).get('hash', '')\n"
		"    print(json.dumps({\n"
		"        'success': True,\n"
		"        'status': 'broadcast',\n"
		"        'tx_hash': tx_hash,\n"
		"        'boc_base64': boc,\n"
		"        'seqno': seqno,\n"
		"        'amount_ton': amount,\n"
		"        'recipient': recipient,\n"
		"    }))\n"
		"else:\n"
		"    print(json.dumps({\n"
		"        'success': False,\n"
		"        'error': data.get('error', 'Broadcast failed'),\n"
		"        'boc_base64': boc,\n"
		"    }))\n"
	).arg(mnPythonList, escapedAddr, escapedRecipient,
		  QString::number(amountTon, 'f', 9),
		  escapedComment, escapedUrl);

	auto json = runTonScript(script);
	if (!json["success"].toBool()) {
		result.success = false;
		result.error = json["error"].toString("Payment failed");
		result.status = "failed";
		result.bocBase64 = json["boc_base64"].toString();
		_stats.failedTransactions++;
		recordTransaction(result);
		Q_EMIT paymentFailed(result.error);
		return result;
	}

	result.success = true;
	result.status = "broadcast";
	result.txHash = json["tx_hash"].toString();
	result.bocBase64 = json["boc_base64"].toString();

	_stats.totalTransactions++;
	_stats.successfulTransactions++;
	_stats.totalSentTon += amountTon;
	_stats.lastTransaction = QDateTime::currentDateTimeUtc();

	recordTransaction(result);
	Q_EMIT paymentCompleted(result);
	return result;
}

QList<TonTransaction> TonWallet::getTransactionHistory(int limit) {
	QList<TonTransaction> transactions;

	const auto address = getWalletKey("address");
	if (address.isEmpty()) {
		return transactions;
	}

	auto escapedAddr = address;
	escapedAddr.replace("'", "\\'");
	auto escapedUrl = _tonCenterUrl;
	escapedUrl.replace("'", "\\'");

	const auto script = QStringLiteral(
		"import json, urllib.request, os\n"
		"\n"
		"address = '%1'\n"
		"base_url = '%2'\n"
		"limit = %3\n"
		"api_key = os.environ.get('TONCENTER_API_KEY', '')\n"
		"\n"
		"url = base_url + 'getTransactions?address=' + address + '&limit=' + str(limit)\n"
		"req = urllib.request.Request(url)\n"
		"if api_key:\n"
		"    req.add_header('X-API-Key', api_key)\n"
		"\n"
		"resp = urllib.request.urlopen(req, timeout=10)\n"
		"data = json.loads(resp.read())\n"
		"\n"
		"txs = []\n"
		"if data.get('ok'):\n"
		"    for tx in data.get('result', []):\n"
		"        in_msg = tx.get('in_msg', {})\n"
		"        out_msgs = tx.get('out_msgs', [])\n"
		"        \n"
		"        # Incoming\n"
		"        if in_msg and in_msg.get('value', '0') != '0':\n"
		"            txs.append({\n"
		"                'hash': tx.get('transaction_id', {}).get('hash', ''),\n"
		"                'lt': tx.get('transaction_id', {}).get('lt', '0'),\n"
		"                'amount_ton': int(in_msg.get('value', '0')) / 1e9,\n"
		"                'from': in_msg.get('source', ''),\n"
		"                'to': address,\n"
		"                'comment': in_msg.get('message', ''),\n"
		"                'timestamp': tx.get('utime', 0),\n"
		"                'is_incoming': True,\n"
		"            })\n"
		"        \n"
		"        # Outgoing\n"
		"        for out_msg in out_msgs:\n"
		"            if out_msg.get('value', '0') != '0':\n"
		"                txs.append({\n"
		"                    'hash': tx.get('transaction_id', {}).get('hash', ''),\n"
		"                    'lt': tx.get('transaction_id', {}).get('lt', '0'),\n"
		"                    'amount_ton': int(out_msg.get('value', '0')) / 1e9,\n"
		"                    'from': address,\n"
		"                    'to': out_msg.get('destination', ''),\n"
		"                    'comment': out_msg.get('message', ''),\n"
		"                    'timestamp': tx.get('utime', 0),\n"
		"                    'is_incoming': False,\n"
		"                })\n"
		"\n"
		"print(json.dumps({'success': True, 'transactions': txs}))\n"
	).arg(escapedAddr, escapedUrl, QString::number(limit));

	auto json = runTonScript(script);
	if (!json["success"].toBool()) {
		return transactions;
	}

	auto txArray = json["transactions"].toArray();
	for (const auto &txVal : txArray) {
		auto tx = txVal.toObject();
		TonTransaction t;
		t.hash = tx["hash"].toString();
		t.lt = tx["lt"].toVariant().toLongLong();
		t.amountTon = tx["amount_ton"].toDouble();
		t.from = tx["from"].toString();
		t.to = tx["to"].toString();
		t.comment = tx["comment"].toString();
		t.timestamp = QDateTime::fromSecsSinceEpoch(
			tx["timestamp"].toVariant().toLongLong());
		t.isIncoming = tx["is_incoming"].toBool();
		transactions.append(t);
	}

	return transactions;
}

QJsonArray TonWallet::getJettonBalances() {
	const auto address = getWalletKey("address");
	if (address.isEmpty()) {
		return QJsonArray();
	}

	auto escapedAddr = address;
	escapedAddr.replace("'", "\\'");
	auto escapedUrl = _tonCenterUrl;
	escapedUrl.replace("'", "\\'");

	const auto script = QStringLiteral(
		"import json, urllib.request, os\n"
		"\n"
		"address = '%1'\n"
		"base_url = '%2'\n"
		"api_key = os.environ.get('TONCENTER_API_KEY', '')\n"
		"\n"
		"url = base_url + 'getJettonWallets?owner_address=' + address\n"
		"req = urllib.request.Request(url)\n"
		"if api_key:\n"
		"    req.add_header('X-API-Key', api_key)\n"
		"\n"
		"try:\n"
		"    resp = urllib.request.urlopen(req, timeout=10)\n"
		"    data = json.loads(resp.read())\n"
		"    if data.get('ok'):\n"
		"        print(json.dumps({'success': True, 'jettons': data['result']}))\n"
		"    else:\n"
		"        print(json.dumps({'success': True, 'jettons': []}))\n"
		"except Exception as e:\n"
		"    print(json.dumps({'success': True, 'jettons': []}))\n"
	).arg(escapedAddr, escapedUrl);

	auto json = runTonScript(script);
	return json["jettons"].toArray();
}

WalletResult TonWallet::sendJetton(
		const QString &jettonMaster,
		const QString &recipientAddress,
		double amount,
		const QString &comment) {
	WalletResult result;
	result.timestamp = QDateTime::currentDateTimeUtc();
	result.recipient = recipientAddress;
	result.amountTon = amount;

	// Jetton transfers are more complex — use tonutils if available
	result.success = false;
	result.error = "Jetton transfers require tonutils library (pip install tonutils)";
	result.status = "unsupported";
	return result;
}

void TonWallet::recordTransaction(const WalletResult &result) {
	if (!_db || !_db->isOpen()) {
		return;
	}
	QSqlQuery query(*_db);
	query.prepare(
		"INSERT INTO ton_transactions "
		"(tx_hash, recipient, amount_ton, fee_ton, comment, status, boc_base64) "
		"VALUES (?, ?, ?, ?, ?, ?, ?)");
	query.addBindValue(result.txHash);
	query.addBindValue(result.recipient);
	query.addBindValue(result.amountTon);
	query.addBindValue(result.feeTon);
	query.addBindValue(QString()); // comment not in WalletResult
	query.addBindValue(result.status);
	query.addBindValue(result.bocBase64);
	query.exec();
}

} // namespace MCP
