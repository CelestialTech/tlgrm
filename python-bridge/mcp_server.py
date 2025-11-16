"""
MCP Server implementation for Telegram.

Exposes Telegram functionality through the Model Context Protocol:
- Tools: Callable functions (list_chats, send_message, etc.)
- Resources: URI-based data access
- Prompts: Pre-configured AI prompts
"""

from typing import Any, Dict, List, Optional

import structlog
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent, Resource, Prompt

from .config import Config
from .telegram_client import TelegramClient

logger = structlog.get_logger()


class TelegramMCPServer:
    """
    MCP Server for Telegram integration.

    Exposes Telegram operations as MCP tools, resources, and prompts.
    """

    def __init__(self, config: Config):
        """
        Initialize MCP server.

        Args:
            config: Configuration object
        """
        self.config = config
        self.telegram = TelegramClient(config)

        # Create MCP server instance
        self.server = Server(config.mcp.server_name)

        # Register handlers
        self._register_tools()
        self._register_resources()
        self._register_prompts()

        logger.info(
            "mcp_server.initialized",
            server_name=config.mcp.server_name,
            version=config.mcp.server_version,
        )

    def _register_tools(self) -> None:
        """Register MCP tools."""

        @self.server.list_tools()
        async def list_tools() -> List[Tool]:
            """List available tools."""
            return [
                Tool(
                    name="list_chats",
                    description="Get a list of all accessible Telegram chats (groups, channels, private chats)",
                    inputSchema={
                        "type": "object",
                        "properties": {},
                    },
                ),
                Tool(
                    name="get_chat_info",
                    description="Get detailed information about a specific Telegram chat",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "chat_id": {
                                "type": "integer",
                                "description": "Telegram chat ID (can be positive or negative)",
                            },
                        },
                        "required": ["chat_id"],
                    },
                ),
                Tool(
                    name="read_messages",
                    description="Read recent messages from a Telegram chat (FULL history access via MTProto)",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "chat_id": {
                                "type": "integer",
                                "description": "Telegram chat ID",
                            },
                            "limit": {
                                "type": "integer",
                                "description": f"Number of messages to read (default: {self.config.limits.default_message_limit}, max: {self.config.limits.max_message_limit})",
                                "default": self.config.limits.default_message_limit,
                            },
                        },
                        "required": ["chat_id"],
                    },
                ),
                Tool(
                    name="send_message",
                    description="Send a message to a Telegram chat",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "chat_id": {
                                "type": "integer",
                                "description": "Telegram chat ID",
                            },
                            "text": {
                                "type": "string",
                                "description": "Message text (supports Markdown formatting)",
                            },
                        },
                        "required": ["chat_id", "text"],
                    },
                ),
                Tool(
                    name="get_user_info",
                    description="Get information about a Telegram user",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "user_id": {
                                "type": "integer",
                                "description": "Telegram user ID",
                            },
                        },
                        "required": ["user_id"],
                    },
                ),
                Tool(
                    name="get_chat_members",
                    description="Get list of members in a Telegram group or channel",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "chat_id": {
                                "type": "integer",
                                "description": "Telegram chat ID",
                            },
                        },
                        "required": ["chat_id"],
                    },
                ),
                Tool(
                    name="search_messages",
                    description="Search for messages in a Telegram chat",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "chat_id": {
                                "type": "integer",
                                "description": "Telegram chat ID",
                            },
                            "query": {
                                "type": "string",
                                "description": "Search query",
                            },
                            "limit": {
                                "type": "integer",
                                "description": "Maximum number of results (default: 50)",
                                "default": 50,
                            },
                        },
                        "required": ["chat_id", "query"],
                    },
                ),
            ]

        @self.server.call_tool()
        async def call_tool(name: str, arguments: Dict[str, Any]) -> List[TextContent]:
            """Execute a tool."""
            try:
                if name == "list_chats":
                    result = await self.telegram.get_chats()

                elif name == "get_chat_info":
                    chat_id = arguments["chat_id"]
                    result = await self.telegram.get_chat_info(chat_id)

                elif name == "read_messages":
                    chat_id = arguments["chat_id"]
                    limit = arguments.get("limit", self.config.limits.default_message_limit)
                    result = await self.telegram.read_messages(chat_id, limit)

                elif name == "send_message":
                    chat_id = arguments["chat_id"]
                    text = arguments["text"]
                    result = await self.telegram.send_message(chat_id, text)

                elif name == "get_user_info":
                    user_id = arguments["user_id"]
                    result = await self.telegram.get_user_info(user_id)

                elif name == "get_chat_members":
                    chat_id = arguments["chat_id"]
                    result = await self.telegram.get_chat_members(chat_id)

                elif name == "search_messages":
                    chat_id = arguments["chat_id"]
                    query = arguments["query"]
                    limit = arguments.get("limit", 50)
                    result = await self.telegram.search_messages(chat_id, query, limit)

                else:
                    raise ValueError(f"Unknown tool: {name}")

                return [TextContent(type="text", text=str(result))]

            except Exception as e:
                logger.error(
                    "mcp_server.tool_execution_failed",
                    tool=name,
                    error=str(e),
                )
                return [TextContent(type="text", text=f"Error: {str(e)}")]

    def _register_resources(self) -> None:
        """Register MCP resources."""

        @self.server.list_resources()
        async def list_resources() -> List[Resource]:
            """List available resources."""
            return [
                Resource(
                    uri="telegram://chats",
                    name="Telegram Chats",
                    description="List of all accessible Telegram chats",
                    mimeType="application/json",
                ),
            ]

        @self.server.read_resource()
        async def read_resource(uri: str) -> str:
            """Read a resource."""
            if uri == "telegram://chats":
                chats = await self.telegram.get_chats()
                return str(chats)

            raise ValueError(f"Unknown resource URI: {uri}")

    def _register_prompts(self) -> None:
        """Register MCP prompts."""

        @self.server.list_prompts()
        async def list_prompts() -> List[Prompt]:
            """List available prompts."""
            return [
                Prompt(
                    name="telegram_summary",
                    description="Analyze and summarize recent messages in a Telegram chat",
                    arguments=[
                        {
                            "name": "chat_id",
                            "description": "Telegram chat ID",
                            "required": True,
                        },
                        {
                            "name": "limit",
                            "description": "Number of messages to analyze",
                            "required": False,
                        },
                    ],
                ),
            ]

        @self.server.get_prompt()
        async def get_prompt(name: str, arguments: Dict[str, str]) -> str:
            """Get a prompt."""
            if name == "telegram_summary":
                chat_id = arguments["chat_id"]
                limit = int(arguments.get("limit", "20"))

                return f"""Analyze the last {limit} messages in Telegram chat {chat_id} and provide:

1. A brief summary of the conversation
2. Key topics discussed
3. Main participants and their roles
4. Any action items or decisions made
5. Overall sentiment of the conversation

Use the read_messages tool to fetch the messages first."""

            raise ValueError(f"Unknown prompt: {name}")

    async def start(self) -> None:
        """Start the MCP server and Telegram client."""
        # Start Telegram client
        await self.telegram.start()

        logger.info("mcp_server.started")

    async def stop(self) -> None:
        """Stop the MCP server and Telegram client."""
        # Stop Telegram client
        await self.telegram.stop()

        logger.info("mcp_server.stopped")

    async def run(self) -> None:
        """Run the MCP server with stdio transport."""
        try:
            # Start services
            await self.start()

            # Run server
            async with stdio_server() as (read_stream, write_stream):
                await self.server.run(
                    read_stream,
                    write_stream,
                    self.server.create_initialization_options(),
                )

        finally:
            # Cleanup
            await self.stop()
