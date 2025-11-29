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
from typing import Optional, List
from pathlib import Path

import structlog
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import TextContent

# Core modules (always available)
from .core.config import Config
from .core.telegram_client import TelegramClient
from .core.cache import MessageCache
from .ipc_bridge import TDesktopBridge, get_tdesktop_bridge
from .monitoring import HealthCheckServer, PrometheusMetrics

# AI/ML modules (optional)
try:
    from .aiml.service import get_aiml_service, is_aiml_available
    AIML_ENABLED = is_aiml_available()
except ImportError:
    AIML_ENABLED = False

# FAISS semantic search (lightweight alternative to ChromaDB)
try:
    from .aiml.faiss_search import FAISSSemanticSearch, is_faiss_available
    FAISS_ENABLED = is_faiss_available()
except ImportError:
    FAISSSemanticSearch = None
    FAISS_ENABLED = False

# Whisper voice transcription
try:
    from .aiml.whisper_transcription import WhisperTranscription, is_whisper_available
    WHISPER_ENABLED = is_whisper_available()
except ImportError:
    WhisperTranscription = None
    WHISPER_ENABLED = False

# Wallet/Stars service
try:
    from .core.wallet_service import WalletService
    WALLET_ENABLED = True
except ImportError:
    WalletService = None
    WALLET_ENABLED = False

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

        self.cache = MessageCache(config=config.cache)
        self.bridge: Optional[TDesktopBridge] = None
        self.aiml_service = None
        self.faiss_search: Optional[FAISSSemanticSearch] = None
        self.whisper: Optional[WhisperTranscription] = None
        self.wallet_service: Optional[WalletService] = None

        # Initialize health check server if enabled
        self.health_server: Optional[HealthCheckServer] = None
        if config.monitoring.enable_health_check:
            self.health_server = HealthCheckServer(port=config.monitoring.health_check_port)
            self._register_health_checks()

        # Initialize Prometheus metrics if enabled
        self.metrics: Optional[PrometheusMetrics] = None
        if config.monitoring.enable_prometheus:
            self.metrics = PrometheusMetrics(port=config.monitoring.metrics_port)

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
                self.bridge = await get_tdesktop_bridge()
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

        # Initialize FAISS semantic search if enabled
        if FAISS_ENABLED:
            try:
                self.faiss_search = FAISSSemanticSearch()
                logger.info("mcp_server.faiss_ready")
            except Exception as e:
                logger.error("mcp_server.faiss_failed", error=str(e))

        # Initialize Whisper transcription if enabled
        if WHISPER_ENABLED:
            try:
                self.whisper = WhisperTranscription()
                logger.info("mcp_server.whisper_ready")
            except Exception as e:
                logger.error("mcp_server.whisper_failed", error=str(e))

        # Initialize Wallet service if Telegram client is available
        if WALLET_ENABLED and self.telegram and hasattr(self.telegram, '_client'):
            try:
                self.wallet_service = WalletService(self.telegram._client)
                logger.info("mcp_server.wallet_ready")
            except Exception as e:
                logger.error("mcp_server.wallet_failed", error=str(e))

        # Start health check server if enabled
        if self.health_server:
            try:
                self.health_server.start()
                logger.info("mcp_server.health_check_started", port=self.health_server.port)
            except Exception as e:
                logger.error("mcp_server.health_check_failed", error=str(e))

        # Start Prometheus metrics server if enabled
        if self.metrics:
            try:
                self.metrics.start()
                logger.info("mcp_server.metrics_started", port=self.metrics.port)
            except Exception as e:
                logger.error("mcp_server.metrics_failed", error=str(e))

        logger.info("mcp_server.started")

    def _register_tools(self):
        """Register all MCP tools"""
        # Core tools (always available)
        self._register_core_tools()

        # Bridge-dependent tools (require tdesktop bridge)
        self._register_batch_tools()
        self._register_scheduler_tools()
        self._register_tags_tools()
        self._register_translation_tools()
        self._register_transcription_tools()

        # AI/ML tools (conditional)
        if self.aiml_enabled:
            self._register_aiml_tools()

        # FAISS semantic search tools (local Python)
        if FAISS_ENABLED:
            self._register_faiss_tools()

        # Whisper transcription tools (local Python)
        if WHISPER_ENABLED:
            self._register_whisper_tools()

        # Wallet/Stars tools (local Python via Pyrogram)
        if WALLET_ENABLED:
            self._register_wallet_tools()

    # Tool implementations (testable methods)

    async def tool_get_messages(self, chat_id: int, limit: int = 50) -> List[TextContent]:
        """
        Get recent messages from a chat.

        Args:
            chat_id: Chat ID (negative for groups/channels)
            limit: Maximum number of messages to retrieve

        Returns:
            List of messages with metadata
        """
        import time
        start_time = time.time()
        status = "success"
        error_type = None
        source = None

        try:
            # Try bridge first (fastest)
            if self.bridge:
                source = "bridge"
                bridge_start = time.time()
                messages = await self.bridge.get_messages(chat_id, limit)
                if self.metrics:
                    self.metrics.record_bridge_request(
                        "get_messages",
                        time.time() - bridge_start,
                        "success"
                    )
            # Fallback to Telegram API
            elif self.telegram:
                source = "telegram_api"
                api_start = time.time()
                messages = await self.telegram.get_messages(chat_id, limit)
                if self.metrics:
                    self.metrics.record_telegram_api_request(
                        "get_messages",
                        time.time() - api_start,
                        "success"
                    )
            else:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "No data source available"})
                )]

            # Record messages retrieved
            if self.metrics and source:
                self.metrics.record_messages_retrieved(len(messages), source)

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
            status = "error"
            error_type = type(e).__name__
            logger.error("tool.get_messages_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]
        finally:
            # Record tool call metrics
            if self.metrics:
                duration = time.time() - start_time
                self.metrics.record_tool_call(
                    "get_messages",
                    duration,
                    status,
                    error_type
                )

    async def tool_search_messages(self, chat_id: int, query: str, limit: int = 50) -> List[TextContent]:
        """
        Search messages in a chat (keyword-based).

        Args:
            chat_id: Chat ID to search in
            query: Search query (keywords)
            limit: Maximum results

        Returns:
            Matching messages
        """
        import time
        start_time = time.time()
        status = "success"
        error_type = None

        try:
            if self.bridge:
                bridge_start = time.time()
                results = await self.bridge.search_messages(chat_id, query, limit)
                if self.metrics:
                    self.metrics.record_bridge_request(
                        "search_messages",
                        time.time() - bridge_start,
                        "success"
                    )
            elif self.telegram:
                api_start = time.time()
                results = await self.telegram.search_messages(chat_id, query, limit)
                if self.metrics:
                    self.metrics.record_telegram_api_request(
                        "search_messages",
                        time.time() - api_start,
                        "success"
                    )
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
            status = "error"
            error_type = type(e).__name__
            logger.error("tool.search_messages_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]
        finally:
            if self.metrics:
                duration = time.time() - start_time
                self.metrics.record_tool_call(
                    "search_messages",
                    duration,
                    status,
                    error_type
                )

    async def tool_list_chats(self) -> List[TextContent]:
        """
        List all available chats.

        Returns:
            List of chats with metadata
        """
        import time
        start_time = time.time()
        status = "success"
        error_type = None

        try:
            if self.bridge:
                bridge_start = time.time()
                chats = await self.bridge.get_dialogs()
                if self.metrics:
                    self.metrics.record_bridge_request(
                        "list_chats",
                        time.time() - bridge_start,
                        "success"
                    )
            elif self.telegram:
                api_start = time.time()
                chats = await self.telegram.list_chats()
                if self.metrics:
                    self.metrics.record_telegram_api_request(
                        "list_chats",
                        time.time() - api_start,
                        "success"
                    )
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
            status = "error"
            error_type = type(e).__name__
            logger.error("tool.list_chats_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]
        finally:
            if self.metrics:
                duration = time.time() - start_time
                self.metrics.record_tool_call(
                    "list_chats",
                    duration,
                    status,
                    error_type
                )

    def _register_core_tools(self):
        """Register core MCP tools"""

        @self.server.call_tool()
        async def get_messages(chat_id: int, limit: int = 50) -> List[TextContent]:
            """Get recent messages from a chat."""
            return await self.tool_get_messages(chat_id, limit)

        @self.server.call_tool()
        async def search_messages(chat_id: int, query: str, limit: int = 50) -> List[TextContent]:
            """Search messages in a chat (keyword-based)."""
            return await self.tool_search_messages(chat_id, query, limit)

        @self.server.call_tool()
        async def list_chats() -> List[TextContent]:
            """List all available chats."""
            return await self.tool_list_chats()

    async def tool_semantic_search(
        self,
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
        import time

        if not self.aiml_service:
            return [TextContent(
                type="text",
                text=json.dumps({
                    "error": "AI/ML features not available. Install: pip install -r requirements.txt"
                })
            )]

        start_time = time.time()
        status = "success"
        error_type = None

        try:
            aiml_start = time.time()
            results = await self.aiml_service.semantic_search(query, chat_id, limit)
            if self.metrics:
                self.metrics.record_aiml_request(
                    "semantic_search",
                    time.time() - aiml_start,
                    "success"
                )

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
            status = "error"
            error_type = type(e).__name__
            logger.error("tool.semantic_search_failed", error=str(e))
            if self.metrics:
                self.metrics.record_aiml_request(
                    "semantic_search",
                    time.time() - start_time,
                    "error"
                )
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]
        finally:
            if self.metrics:
                duration = time.time() - start_time
                self.metrics.record_tool_call(
                    "semantic_search",
                    duration,
                    status,
                    error_type
                )

    async def tool_classify_intent(self, text: str) -> List[TextContent]:
        """
        Classify the intent of a message using AI.

        Args:
            text: Message text to analyze

        Returns:
            Intent classification (question, request, command, etc.)
        """
        import time

        if not self.aiml_service:
            return [TextContent(
                type="text",
                text=json.dumps({"error": "AI/ML features not available"})
            )]

        start_time = time.time()
        status = "success"
        error_type = None

        try:
            aiml_start = time.time()
            intent = await self.aiml_service.classify_intent(text)
            if self.metrics:
                self.metrics.record_aiml_request(
                    "classify_intent",
                    time.time() - aiml_start,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps({
                    "text": text,
                    "intent": intent
                }, indent=2)
            )]

        except Exception as e:
            status = "error"
            error_type = type(e).__name__
            logger.error("tool.classify_intent_failed", error=str(e))
            if self.metrics:
                self.metrics.record_aiml_request(
                    "classify_intent",
                    time.time() - start_time,
                    "error"
                )
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]
        finally:
            if self.metrics:
                duration = time.time() - start_time
                self.metrics.record_tool_call(
                    "classify_intent",
                    duration,
                    status,
                    error_type
                )

    def _register_aiml_tools(self):
        """Register AI/ML enhanced tools"""

        @self.server.call_tool()
        async def semantic_search(
            query: str,
            chat_id: Optional[int] = None,
            limit: int = 10
        ) -> List[TextContent]:
            """Semantic search using AI embeddings (meaning-based, not keywords)."""
            return await self.tool_semantic_search(query, chat_id, limit)

        @self.server.call_tool()
        async def classify_intent(text: str) -> List[TextContent]:
            """Classify the intent of a message using AI."""
            return await self.tool_classify_intent(text)

    # ==================== Batch Operations Tools ====================

    async def tool_batch_operation(
        self,
        operation: str,
        targets: List[int],
        options: Optional[dict] = None
    ) -> List[TextContent]:
        """
        Execute a batch operation on multiple targets.

        Args:
            operation: Operation type (mark_read, archive, mute, delete)
            targets: List of chat IDs to apply operation to
            options: Additional options for the operation

        Returns:
            Results with success/failure per target
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.batch_operation(operation, targets, options)

            if self.metrics:
                self.metrics.record_tool_call(
                    "batch_operation",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.batch_operation_failed", error=str(e))
            if self.metrics:
                self.metrics.record_tool_call(
                    "batch_operation",
                    time.time() - start_time,
                    "error",
                    type(e).__name__
                )
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_batch_forward_messages(
        self,
        source_chat_id: int,
        message_ids: List[int],
        target_chat_ids: List[int]
    ) -> List[TextContent]:
        """
        Forward multiple messages to multiple chats.

        Args:
            source_chat_id: Source chat ID
            message_ids: List of message IDs to forward
            target_chat_ids: List of target chat IDs

        Returns:
            Forward results per target
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.batch_forward_messages(
                source_chat_id, message_ids, target_chat_ids
            )

            if self.metrics:
                self.metrics.record_tool_call(
                    "batch_forward_messages",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.batch_forward_messages_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_batch_delete_messages(
        self,
        chat_id: int,
        message_ids: List[int],
        revoke: bool = True
    ) -> List[TextContent]:
        """
        Delete multiple messages at once.

        Args:
            chat_id: Chat ID containing the messages
            message_ids: List of message IDs to delete
            revoke: Whether to delete for everyone

        Returns:
            Deletion results
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.batch_delete_messages(chat_id, message_ids, revoke)

            if self.metrics:
                self.metrics.record_tool_call(
                    "batch_delete_messages",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.batch_delete_messages_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    def _register_batch_tools(self):
        """Register batch operation tools"""

        @self.server.call_tool()
        async def batch_operation(
            operation: str,
            targets: List[int],
            options: Optional[dict] = None
        ) -> List[TextContent]:
            """Execute batch operation (mark_read, archive, mute, delete) on multiple chats."""
            return await self.tool_batch_operation(operation, targets, options)

        @self.server.call_tool()
        async def batch_forward_messages(
            source_chat_id: int,
            message_ids: List[int],
            target_chat_ids: List[int]
        ) -> List[TextContent]:
            """Forward multiple messages to multiple chats at once."""
            return await self.tool_batch_forward_messages(
                source_chat_id, message_ids, target_chat_ids
            )

        @self.server.call_tool()
        async def batch_delete_messages(
            chat_id: int,
            message_ids: List[int],
            revoke: bool = True
        ) -> List[TextContent]:
            """Delete multiple messages at once."""
            return await self.tool_batch_delete_messages(chat_id, message_ids, revoke)

    # ==================== Message Scheduler Tools ====================

    async def tool_schedule_message(
        self,
        chat_id: int,
        text: str,
        send_at: int
    ) -> List[TextContent]:
        """
        Schedule a message for later delivery.

        Args:
            chat_id: Target chat ID
            text: Message text
            send_at: Unix timestamp for delivery

        Returns:
            Scheduled message info
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.schedule_message(chat_id, text, send_at)

            if self.metrics:
                self.metrics.record_tool_call(
                    "schedule_message",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.schedule_message_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_list_scheduled_messages(self, chat_id: int) -> List[TextContent]:
        """
        List all scheduled messages for a chat.

        Args:
            chat_id: Chat ID to list scheduled messages for

        Returns:
            List of scheduled messages
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            messages = await self.bridge.list_scheduled_messages(chat_id)

            if self.metrics:
                self.metrics.record_tool_call(
                    "list_scheduled_messages",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps({
                    "scheduled_messages": messages,
                    "count": len(messages)
                }, indent=2)
            )]

        except Exception as e:
            logger.error("tool.list_scheduled_messages_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_cancel_scheduled_message(
        self,
        chat_id: int,
        message_id: int
    ) -> List[TextContent]:
        """
        Cancel a scheduled message.

        Args:
            chat_id: Chat ID
            message_id: Scheduled message ID

        Returns:
            Cancellation result
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.cancel_scheduled_message(chat_id, message_id)

            if self.metrics:
                self.metrics.record_tool_call(
                    "cancel_scheduled_message",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.cancel_scheduled_message_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    def _register_scheduler_tools(self):
        """Register message scheduler tools"""

        @self.server.call_tool()
        async def schedule_message(
            chat_id: int,
            text: str,
            send_at: int
        ) -> List[TextContent]:
            """Schedule a message for later delivery (send_at is Unix timestamp)."""
            return await self.tool_schedule_message(chat_id, text, send_at)

        @self.server.call_tool()
        async def list_scheduled_messages(chat_id: int) -> List[TextContent]:
            """List all scheduled messages for a chat."""
            return await self.tool_list_scheduled_messages(chat_id)

        @self.server.call_tool()
        async def cancel_scheduled_message(
            chat_id: int,
            message_id: int
        ) -> List[TextContent]:
            """Cancel a scheduled message."""
            return await self.tool_cancel_scheduled_message(chat_id, message_id)

    # ==================== Message Tags Tools ====================

    async def tool_tag_message(
        self,
        chat_id: int,
        message_id: int,
        tag: str
    ) -> List[TextContent]:
        """
        Add a tag to a message.

        Args:
            chat_id: Chat ID
            message_id: Message ID
            tag: Tag name to add

        Returns:
            Tagging result
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.tag_message(chat_id, message_id, tag)

            if self.metrics:
                self.metrics.record_tool_call(
                    "tag_message",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.tag_message_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_get_message_tags(
        self,
        chat_id: int,
        message_id: int
    ) -> List[TextContent]:
        """
        Get all tags for a message.

        Args:
            chat_id: Chat ID
            message_id: Message ID

        Returns:
            List of tag names
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            tags = await self.bridge.get_message_tags(chat_id, message_id)

            if self.metrics:
                self.metrics.record_tool_call(
                    "get_message_tags",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps({"tags": tags, "count": len(tags)}, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_message_tags_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_list_tags(self) -> List[TextContent]:
        """
        List all available tags.

        Returns:
            List of tags with name, color, count
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            tags = await self.bridge.list_tags()

            if self.metrics:
                self.metrics.record_tool_call(
                    "list_tags",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps({"tags": tags, "count": len(tags)}, indent=2)
            )]

        except Exception as e:
            logger.error("tool.list_tags_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_remove_message_tag(
        self,
        chat_id: int,
        message_id: int,
        tag: str
    ) -> List[TextContent]:
        """
        Remove a tag from a message.

        Args:
            chat_id: Chat ID
            message_id: Message ID
            tag: Tag name to remove

        Returns:
            Removal result
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.remove_message_tag(chat_id, message_id, tag)

            if self.metrics:
                self.metrics.record_tool_call(
                    "remove_message_tag",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.remove_message_tag_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_get_tagged_messages(
        self,
        tag: str,
        limit: int = 50
    ) -> List[TextContent]:
        """
        Get all messages with a specific tag.

        Args:
            tag: Tag name to search for
            limit: Maximum number of results

        Returns:
            List of tagged messages
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            messages = await self.bridge.get_tagged_messages(tag, limit)

            if self.metrics:
                self.metrics.record_tool_call(
                    "get_tagged_messages",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps({
                    "tag": tag,
                    "messages": messages,
                    "count": len(messages)
                }, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_tagged_messages_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    def _register_tags_tools(self):
        """Register message tag tools"""

        @self.server.call_tool()
        async def tag_message(
            chat_id: int,
            message_id: int,
            tag: str
        ) -> List[TextContent]:
            """Add a tag to a message for organization."""
            return await self.tool_tag_message(chat_id, message_id, tag)

        @self.server.call_tool()
        async def get_message_tags(
            chat_id: int,
            message_id: int
        ) -> List[TextContent]:
            """Get all tags for a specific message."""
            return await self.tool_get_message_tags(chat_id, message_id)

        @self.server.call_tool()
        async def list_tags() -> List[TextContent]:
            """List all available tags with usage counts."""
            return await self.tool_list_tags()

        @self.server.call_tool()
        async def remove_message_tag(
            chat_id: int,
            message_id: int,
            tag: str
        ) -> List[TextContent]:
            """Remove a tag from a message."""
            return await self.tool_remove_message_tag(chat_id, message_id, tag)

        @self.server.call_tool()
        async def get_tagged_messages(
            tag: str,
            limit: int = 50
        ) -> List[TextContent]:
            """Get all messages with a specific tag."""
            return await self.tool_get_tagged_messages(tag, limit)

    # ==================== Translation Tools ====================

    async def tool_translate_message(
        self,
        chat_id: int,
        message_id: int,
        target_language: str
    ) -> List[TextContent]:
        """
        Translate a message to another language.

        Args:
            chat_id: Chat ID
            message_id: Message ID
            target_language: Target language code (e.g., 'en', 'es', 'ru')

        Returns:
            Translation result with original and translated text
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.translate_message(
                chat_id, message_id, target_language
            )

            if self.metrics:
                self.metrics.record_tool_call(
                    "translate_message",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.translate_message_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_configure_auto_translate(
        self,
        chat_id: int,
        enabled: bool,
        target_language: Optional[str] = None
    ) -> List[TextContent]:
        """
        Configure auto-translation for a chat.

        Args:
            chat_id: Chat ID
            enabled: Whether to enable auto-translation
            target_language: Target language for auto-translation

        Returns:
            Configuration result
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.configure_auto_translate(
                chat_id, enabled, target_language
            )

            if self.metrics:
                self.metrics.record_tool_call(
                    "configure_auto_translate",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.configure_auto_translate_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_get_translation_languages(self) -> List[TextContent]:
        """
        Get list of supported translation languages.

        Returns:
            List of languages with code and name
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            languages = await self.bridge.get_translation_languages()

            if self.metrics:
                self.metrics.record_tool_call(
                    "get_translation_languages",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps({
                    "languages": languages,
                    "count": len(languages)
                }, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_translation_languages_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    def _register_translation_tools(self):
        """Register translation tools"""

        @self.server.call_tool()
        async def translate_message(
            chat_id: int,
            message_id: int,
            target_language: str
        ) -> List[TextContent]:
            """Translate a message to another language (e.g., 'en', 'es', 'ru')."""
            return await self.tool_translate_message(
                chat_id, message_id, target_language
            )

        @self.server.call_tool()
        async def get_translation_languages() -> List[TextContent]:
            """Get list of supported translation languages."""
            return await self.tool_get_translation_languages()

        @self.server.call_tool()
        async def configure_auto_translate(
            chat_id: int,
            enabled: bool,
            target_language: Optional[str] = None
        ) -> List[TextContent]:
            """Configure auto-translation for a chat."""
            return await self.tool_configure_auto_translate(
                chat_id, enabled, target_language
            )

    # ==================== Voice Transcription Tools ====================

    async def tool_transcribe_voice_message(
        self,
        chat_id: int,
        message_id: int
    ) -> List[TextContent]:
        """
        Transcribe a voice message.

        Args:
            chat_id: Chat ID
            message_id: Voice message ID

        Returns:
            Transcription result with text and confidence
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.transcribe_voice_message(chat_id, message_id)

            if self.metrics:
                self.metrics.record_tool_call(
                    "transcribe_voice_message",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.transcribe_voice_message_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_get_transcription_status(
        self,
        task_id: str
    ) -> List[TextContent]:
        """
        Get status of a transcription task.

        Args:
            task_id: Transcription task ID

        Returns:
            Task status and result if complete
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            result = await self.bridge.get_transcription_status(task_id)

            if self.metrics:
                self.metrics.record_tool_call(
                    "get_transcription_status",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_transcription_status_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_get_transcription_history(
        self,
        chat_id: Optional[int] = None,
        limit: int = 50
    ) -> List[TextContent]:
        """
        Get history of transcribed messages.

        Args:
            chat_id: Optional chat ID to filter by
            limit: Maximum number of results

        Returns:
            List of transcription records
        """
        import time
        start_time = time.time()

        try:
            if not self.bridge:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "Bridge not available"})
                )]

            history = await self.bridge.get_transcription_history(chat_id, limit)

            if self.metrics:
                self.metrics.record_tool_call(
                    "get_transcription_history",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps({
                    "transcriptions": history,
                    "count": len(history)
                }, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_transcription_history_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    def _register_transcription_tools(self):
        """Register voice transcription tools"""

        @self.server.call_tool()
        async def transcribe_voice_message(
            chat_id: int,
            message_id: int
        ) -> List[TextContent]:
            """Transcribe a voice message to text."""
            return await self.tool_transcribe_voice_message(chat_id, message_id)

        @self.server.call_tool()
        async def get_transcription_status(task_id: str) -> List[TextContent]:
            """Get status of a transcription task."""
            return await self.tool_get_transcription_status(task_id)

        @self.server.call_tool()
        async def get_transcription_history(
            chat_id: Optional[int] = None,
            limit: int = 50
        ) -> List[TextContent]:
            """Get history of transcribed messages."""
            return await self.tool_get_transcription_history(chat_id, limit)

    # ==================== FAISS Semantic Search Tools ====================

    async def tool_faiss_search(
        self,
        query: str,
        chat_id: Optional[int] = None,
        limit: int = 10
    ) -> List[TextContent]:
        """
        Local FAISS semantic search (meaning-based, not keywords).

        Args:
            query: Natural language query
            chat_id: Optional chat ID to filter
            limit: Maximum results

        Returns:
            Semantically similar messages
        """
        import time
        start_time = time.time()

        if not self.faiss_search:
            return [TextContent(
                type="text",
                text=json.dumps({
                    "error": "FAISS not available. Install: pip install faiss-cpu sentence-transformers"
                })
            )]

        try:
            results = await self.faiss_search.search(
                query,
                chat_id=str(chat_id) if chat_id else None,
                top_k=limit
            )

            if self.metrics:
                self.metrics.record_tool_call(
                    "faiss_search",
                    time.time() - start_time,
                    "success"
                )

            return [TextContent(
                type="text",
                text=json.dumps({
                    "query": query,
                    "results": results,
                    "count": len(results)
                }, indent=2)
            )]

        except Exception as e:
            logger.error("tool.faiss_search_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_faiss_index_messages(
        self,
        chat_id: int,
        limit: int = 100
    ) -> List[TextContent]:
        """
        Index messages from a chat for FAISS semantic search.

        Args:
            chat_id: Chat ID to index
            limit: Maximum messages to index

        Returns:
            Indexing results
        """
        if not self.faiss_search:
            return [TextContent(
                type="text",
                text=json.dumps({"error": "FAISS not available"})
            )]

        try:
            # Get messages from bridge or telegram
            messages = []
            if self.bridge:
                messages = await self.bridge.get_messages_local(chat_id, limit)
            elif self.telegram:
                messages = await self.telegram.get_messages(chat_id, limit)

            if not messages:
                return [TextContent(
                    type="text",
                    text=json.dumps({"error": "No messages found", "chat_id": chat_id})
                )]

            # Prepare for indexing
            index_data = []
            for msg in messages:
                text = msg.get("text", "")
                if text:
                    index_data.append({
                        "chat_id": str(chat_id),
                        "message_id": str(msg.get("message_id", msg.get("id", ""))),
                        "text": text,
                        "metadata": {
                            "date": msg.get("date"),
                            "sender": msg.get("sender", msg.get("from_id")),
                        }
                    })

            # Index messages
            added = await self.faiss_search.add_messages_batch(index_data)

            logger.info(
                "tool.faiss_index_complete",
                chat_id=chat_id,
                indexed=added,
                total=len(index_data)
            )

            return [TextContent(
                type="text",
                text=json.dumps({
                    "indexed": added,
                    "total_messages": len(messages),
                    "chat_id": chat_id,
                    "index_stats": self.faiss_search.get_stats()
                }, indent=2)
            )]

        except Exception as e:
            logger.error("tool.faiss_index_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    def _register_faiss_tools(self):
        """Register FAISS semantic search tools"""

        @self.server.call_tool()
        async def faiss_search(
            query: str,
            chat_id: Optional[int] = None,
            limit: int = 10
        ) -> List[TextContent]:
            """Semantic search using local FAISS index (meaning-based, fast)."""
            return await self.tool_faiss_search(query, chat_id, limit)

        @self.server.call_tool()
        async def faiss_index_messages(
            chat_id: int,
            limit: int = 100
        ) -> List[TextContent]:
            """Index messages from a chat for semantic search."""
            return await self.tool_faiss_index_messages(chat_id, limit)

    # ==================== Whisper Transcription Tools ====================

    async def tool_whisper_transcribe_file(
        self,
        audio_path: str,
        language: Optional[str] = None
    ) -> List[TextContent]:
        """
        Transcribe an audio file using local Whisper.

        Args:
            audio_path: Path to audio file
            language: Language code (auto-detect if None)

        Returns:
            Transcription with text, segments, language
        """
        if not self.whisper:
            return [TextContent(
                type="text",
                text=json.dumps({
                    "error": "Whisper not available. Install: pip install openai-whisper"
                })
            )]

        try:
            result = await self.whisper.transcribe_file(audio_path, language)

            logger.info(
                "tool.whisper_transcribe_complete",
                path=audio_path,
                language=result.get("language"),
                duration=result.get("duration")
            )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.whisper_transcribe_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_whisper_transcribe_telegram_voice(
        self,
        voice_path: str,
        language: Optional[str] = None
    ) -> List[TextContent]:
        """
        Transcribe a Telegram voice message using local Whisper.

        Args:
            voice_path: Path to voice message file (.oga, .ogg)
            language: Language code (auto-detect if None)

        Returns:
            Transcription with text and segments
        """
        if not self.whisper:
            return [TextContent(
                type="text",
                text=json.dumps({"error": "Whisper not available"})
            )]

        try:
            result = await self.whisper.transcribe_telegram_voice(voice_path, language)

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.whisper_voice_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    def _register_whisper_tools(self):
        """Register local Whisper transcription tools"""

        @self.server.call_tool()
        async def whisper_transcribe(
            audio_path: str,
            language: Optional[str] = None
        ) -> List[TextContent]:
            """Transcribe audio file using local Whisper (any format)."""
            return await self.tool_whisper_transcribe_file(audio_path, language)

        @self.server.call_tool()
        async def whisper_transcribe_voice(
            voice_path: str,
            language: Optional[str] = None
        ) -> List[TextContent]:
            """Transcribe Telegram voice message using local Whisper."""
            return await self.tool_whisper_transcribe_telegram_voice(voice_path, language)

    # ==================== Wallet/Stars Tools ====================

    async def tool_get_stars_balance(
        self,
        force_refresh: bool = False
    ) -> List[TextContent]:
        """
        Get Telegram Stars balance.

        Args:
            force_refresh: Force API refresh (ignore cache)

        Returns:
            Stars balance information
        """
        if not self.wallet_service:
            return [TextContent(
                type="text",
                text=json.dumps({
                    "error": "Wallet service not available. Requires Telegram client connection."
                })
            )]

        try:
            result = await self.wallet_service.get_stars_balance(force_refresh)

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_stars_balance_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_get_stars_transactions(
        self,
        limit: int = 50,
        inbound: bool = True,
        outbound: bool = True
    ) -> List[TextContent]:
        """
        Get Stars transaction history.

        Args:
            limit: Maximum transactions
            inbound: Include incoming
            outbound: Include outgoing

        Returns:
            Transaction history
        """
        if not self.wallet_service:
            return [TextContent(
                type="text",
                text=json.dumps({"error": "Wallet service not available"})
            )]

        try:
            result = await self.wallet_service.get_stars_transactions(
                limit=limit,
                inbound=inbound,
                outbound=outbound
            )

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_stars_transactions_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_get_premium_status(self) -> List[TextContent]:
        """
        Get Telegram Premium subscription status.

        Returns:
            Premium status information
        """
        if not self.wallet_service:
            return [TextContent(
                type="text",
                text=json.dumps({"error": "Wallet service not available"})
            )]

        try:
            result = await self.wallet_service.get_premium_status()

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_premium_status_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_get_gifts(
        self,
        user_id: Optional[int] = None
    ) -> List[TextContent]:
        """
        Get saved gifts for a user.

        Args:
            user_id: User ID (defaults to self)

        Returns:
            Gifts information
        """
        if not self.wallet_service:
            return [TextContent(
                type="text",
                text=json.dumps({"error": "Wallet service not available"})
            )]

        try:
            result = await self.wallet_service.get_gifts(user_id)

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_gifts_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    async def tool_get_subscriptions(self) -> List[TextContent]:
        """
        Get active Stars subscriptions.

        Returns:
            Active subscriptions
        """
        if not self.wallet_service:
            return [TextContent(
                type="text",
                text=json.dumps({"error": "Wallet service not available"})
            )]

        try:
            result = await self.wallet_service.get_subscriptions()

            return [TextContent(
                type="text",
                text=json.dumps(result, indent=2)
            )]

        except Exception as e:
            logger.error("tool.get_subscriptions_failed", error=str(e))
            return [TextContent(
                type="text",
                text=json.dumps({"error": str(e)})
            )]

    def _register_wallet_tools(self):
        """Register Wallet/Stars tools"""

        @self.server.call_tool()
        async def get_stars_balance(
            force_refresh: bool = False
        ) -> List[TextContent]:
            """Get your Telegram Stars balance."""
            return await self.tool_get_stars_balance(force_refresh)

        @self.server.call_tool()
        async def get_stars_transactions(
            limit: int = 50,
            inbound: bool = True,
            outbound: bool = True
        ) -> List[TextContent]:
            """Get Stars transaction history."""
            return await self.tool_get_stars_transactions(limit, inbound, outbound)

        @self.server.call_tool()
        async def get_premium_status() -> List[TextContent]:
            """Get your Telegram Premium subscription status."""
            return await self.tool_get_premium_status()

        @self.server.call_tool()
        async def get_gifts(
            user_id: Optional[int] = None
        ) -> List[TextContent]:
            """Get saved gifts (Stars gifts received)."""
            return await self.tool_get_gifts(user_id)

        @self.server.call_tool()
        async def get_subscriptions() -> List[TextContent]:
            """Get active Stars subscriptions."""
            return await self.tool_get_subscriptions()

    def _register_health_checks(self):
        """Register component health check functions"""
        if not self.health_server:
            return

        # Bridge component health check
        def check_bridge() -> bool:
            return self.bridge is not None

        # Telegram client health check
        def check_telegram() -> bool:
            return self.telegram is not None

        # AI/ML service health check
        def check_aiml() -> bool:
            return self.aiml_enabled and self.aiml_service is not None

        self.health_server.register_component("bridge", check_bridge)
        self.health_server.register_component("telegram", check_telegram)
        self.health_server.register_component("aiml", check_aiml)


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
