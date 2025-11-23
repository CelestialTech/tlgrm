#!/usr/bin/env python3
"""
Unified Telegram MCP Server

Provides MCP tools for Telegram with optional AI/ML enhancements.

Modes:
- standalone: Use Telegram Bot API directly (rate limited)
- bridge: Connect to C++ server via IPC (fast, unlimited)
- hybrid: Bridge + AI/ML enhancements (default)

Features:
- Core: Message retrieval, search, chat stats (always available)
- AI/ML: Semantic search, intent classification (optional)
"""

import asyncio
import argparse
import json
from typing import Optional, List, Dict, Any
from pathlib import Path

import structlog
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent

# Core modules (always available)
from .core.config import Config
from .core.telegram_client import TelegramClient
from .core.cache import MessageCache
from .ipc_bridge import TDesktopBridge, get_bridge

# AI/ML modules (optional)
try:
    from .aiml.service import get_aiml_service, is_aiml_available
    AIML_ENABLED = is_aiml_available()
except ImportError:
    AIML_ENABLED = False

logger = structlog.get_logger()


class UnifiedMCPServer:
    """Unified MCP server with optional AI/ML features"""

    def __init__(self, config: Config, mode: str = "hybrid", enable_aiml: bool = True):
        self.config = config
        self.mode = mode
        self.aiml_enabled = enable_aiml and AIML_ENABLED

        # Create MCP server
        self.server = Server(config.mcp.server_name)

        # Initialize core components
        if mode in ["standalone", "hybrid"]:
            self.telegram = TelegramClient(config)
        else:
            self.telegram = None

        self.cache = MessageCache(max_size=config.cache.max_messages)
        self.bridge: Optional[TDesktopBridge] = None
        self.aiml_service = None

        # Register handlers
        self._register_tools()

        logger.info(
            "mcp_server.initialized",
            mode=mode,
            aiml_enabled=self.aiml_enabled
        )

    async def start(self):
        """Start the MCP server and all components"""
        # Initialize bridge if needed
        if self.mode in ["bridge", "hybrid"]:
            try:
                self.bridge = await get_bridge()
                logger.info("mcp_server.bridge_connected")
            except Exception as e:
                logger.error("mcp_server.bridge_failed", error=str(e))
                if self.mode == "bridge":
                    raise  # Bridge mode requires bridge

        # Initialize AI/ML if enabled
        if self.aiml_enabled:
            try:
                self.aiml_service = await get_aiml_service(
                    self.config.aiml.__dict__ if hasattr(self.config, "aiml") else {}
                )
                logger.info("mcp_server.aiml_ready")
            except Exception as e:
                logger.error("mcp_server.aiml_failed", error=str(e))
                self.aiml_enabled = False

        logger.info("mcp_server.started")

    def _register_tools(self):
        """Register all MCP tools"""
        # Core tools (always available)
        self._register_core_tools()

        # AI/ML tools (conditional)
        if self.aiml_enabled:
            self._register_aiml_tools()

    def _register_core_tools(self):
        """Register core MCP tools"""

        @self.server.call_tool()
        async def get_messages(chat_id: int, limit: int = 50) -> List[TextContent]:
            """
            Get recent messages from a chat.

            Args:
                chat_id: Chat ID (negative for groups/channels)
                limit: Maximum number of messages to retrieve

            Returns:
                List of messages with metadata
            """
            try:
                # Try bridge first (fastest)
                if self.bridge:
                    messages = await self.bridge.get_messages(chat_id, limit)
                # Fallback to Telegram API
                elif self.telegram:
                    messages = await self.telegram.get_messages(chat_id, limit)
                else:
                    return [TextContent(
                        type="text",
                        text=json.dumps({"error": "No data source available"})
                    )]

                # Cache messages
                for msg in messages:
                    self.cache.add(msg)

                # Index for AI/ML if enabled
                if self.aiml_service:
                    for msg in messages:
                        if msg.get("text"):
                            await self.aiml_service.index_message(
                                msg["message_id"],
                                chat_id,
                                msg["text"],
                                metadata=msg
                            )

                return [TextContent(
                    type="text",
                    text=json.dumps({"messages": messages, "count": len(messages)}, indent=2)
                )]

            except Exception as e:
                logger.error("tool.get_messages_failed", error=str(e))
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": str(e)})
                )]

        @self.server.call_tool()
        async def search_messages(chat_id: int, query: str, limit: int = 50) -> List[TextContent]:
            """
            Search messages in a chat (keyword-based).

            Args:
                chat_id: Chat ID to search in
                query: Search query (keywords)
                limit: Maximum results

            Returns:
                Matching messages
            """
            try:
                if self.bridge:
                    results = await self.bridge.search_messages(chat_id, query, limit)
                elif self.telegram:
                    results = await self.telegram.search_messages(chat_id, query, limit)
                else:
                    return [TextContent(
                        type="text",
                        text=json.dumps({"error": "No data source available"})
                    )]

                return [TextContent(
                    type="text",
                    text=json.dumps({"results": results, "count": len(results)}, indent=2)
                )]

            except Exception as e:
                logger.error("tool.search_messages_failed", error=str(e))
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": str(e)})
                )]

        @self.server.call_tool()
        async def list_chats() -> List[TextContent]:
            """
            List all available chats.

            Returns:
                List of chats with metadata
            """
            try:
                if self.bridge:
                    chats = await self.bridge.list_archived_chats()
                elif self.telegram:
                    chats = await self.telegram.list_chats()
                else:
                    return [TextContent(
                        type="text",
                        text=json.dumps({"error": "No data source available"})
                    )]

                return [TextContent(
                    type="text",
                    text=json.dumps({"chats": chats, "count": len(chats)}, indent=2)
                )]

            except Exception as e:
                logger.error("tool.list_chats_failed", error=str(e))
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": str(e)})
                )]

    def _register_aiml_tools(self):
        """Register AI/ML enhanced tools"""

        @self.server.call_tool()
        async def semantic_search(
            query: str,
            chat_id: Optional[int] = None,
            limit: int = 10
        ) -> List[TextContent]:
            """
            Semantic search using AI embeddings (meaning-based, not keywords).

            Args:
                query: Natural language query
                chat_id: Optional chat ID to limit scope
                limit: Maximum results

            Returns:
                Semantically similar messages
            """
            if not self.aiml_service:
                return [TextContent(
                    type="text",
                    text=json.dumps({
                        "error": "AI/ML features not available. Install: pip install -r requirements.txt"
                    })
                )]

            try:
                results = await self.aiml_service.semantic_search(query, chat_id, limit)

                # Enrich with full message data from bridge
                if self.bridge:
                    for result in results:
                        msg_chat_id = result["metadata"].get("chat_id")
                        msg_id = result["metadata"].get("message_id")
                        if msg_chat_id and msg_id:
                            messages = await self.bridge.get_messages(msg_chat_id, limit=100)
                            full_msg = next((m for m in messages if m.get("message_id") == msg_id), None)
                            if full_msg:
                                result["full_context"] = full_msg

                return [TextContent(
                    type="text",
                    text=json.dumps({
                        "query": query,
                        "results": results,
                        "count": len(results)
                    }, indent=2)
                )]

            except Exception as e:
                logger.error("tool.semantic_search_failed", error=str(e))
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": str(e)})
                )]

        @self.server.call_tool()
        async def classify_intent(text: str) -> List[TextContent]:
            """
            Classify the intent of a message using AI.

            Args:
                text: Message text to analyze

            Returns:
                Intent classification (question, request, command, etc.)
            """
            if not self.aiml_service:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "AI/ML features not available"})
                )]

            try:
                intent = await self.aiml_service.classify_intent(text)

                return [TextContent(
                    type="text",
                    text=json.dumps({
                        "text": text,
                        "intent": intent
                    }, indent=2)
                )]

            except Exception as e:
                logger.error("tool.classify_intent_failed", error=str(e))
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": str(e)})
                )]


async def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description="Unified Telegram MCP Server")
    parser.add_argument(
        "--mode",
        choices=["standalone", "bridge", "hybrid"],
        default="hybrid",
        help="Server mode"
    )
    parser.add_argument(
        "--no-aiml",
        action="store_true",
        help="Disable AI/ML features"
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=Path("config.toml"),
        help="Configuration file path"
    )

    args = parser.parse_args()

    # Load configuration
    config = Config.from_file(args.config)

    # Create and start server
    server = UnifiedMCPServer(
        config=config,
        mode=args.mode,
        enable_aiml=not args.no_aiml
    )

    await server.start()

    # Run MCP server
    async with stdio_server() as (read_stream, write_stream):
        await server.server.run(
            read_stream,
            write_stream,
            server.server.create_initialization_options()
        )


if __name__ == "__main__":
    asyncio.run(main())
