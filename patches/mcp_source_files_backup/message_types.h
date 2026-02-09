// Message Types for MCP Bot Framework
// Shared types used across multiple MCP components
// This file is part of Telegram Desktop MCP integration.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

namespace MCP {

enum class MessageIntent {
	Question,
	Request,
	Statement,
	Unknown
};

} // namespace MCP
