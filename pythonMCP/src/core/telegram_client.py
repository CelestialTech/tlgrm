"""
Telegram client for MCP Server using Pyrogram (MTProto).

This provides FULL Telegram access (not limited Bot API):
- Complete message history retrieval
- User account integration
- All chat types (private, groups, channels)
- Media handling (photos, videos, voice, documents)
- Same capabilities as Telegram Desktop (tdesktop)
"""

import asyncio
from typing import Dict, List, Optional, Any

import structlog
from pyrogram import Client
from pyrogram.types import Dialog, Message, Chat, User
from pyrogram.errors import FloodWait, RPCError

from .config import Config

logger = structlog.get_logger()


class TelegramClient:
    """
    Async Telegram client using Pyrogram (MTProto).

    Features:
    - Full MTProto access (like Telegram Desktop)
    - Complete message history retrieval
    - User account or bot mode
    - Media download/upload
    - Chat management
    - Real-time updates
    """

    def __init__(self, config: Config):
        """
        Initialize Telegram client.

        Args:
            config: Configuration object
        """
        self.config = config

        # Get credentials from environment
        api_id = config.telegram.api_id
        api_hash = config.telegram.api_hash

        # Optional: bot token for bot mode, or user session for user mode
        bot_token = config.telegram.bot_token if hasattr(config.telegram, 'bot_token') else None

        # Create Pyrogram client
        # Session name determines the session file location
        session_name = config.telegram.session_name if hasattr(config.telegram, 'session_name') else "telegram_mcp"

        self.app = Client(
            name=session_name,
            api_id=api_id,
            api_hash=api_hash,
            bot_token=bot_token,  # If None, will use user mode
            workdir=".",  # Session file location
        )

        # Cache
        self._dialogs_cache: Dict[int, Dialog] = {}
        self._chats_cache: Dict[int, Chat] = {}
        self._users_cache: Dict[int, User] = {}
        self._cache_lock = asyncio.Lock()

        logger.info(
            "telegram_client.initialized",
            server_name=config.mcp.server_name,
            mode="bot" if bot_token else "user",
        )

    async def start(self) -> None:
        """Start the Telegram client and cache dialogs."""
        await self.app.start()

        # Cache all dialogs (chats) for quick access
        await self._cache_dialogs()

        logger.info(
            "telegram_client.started",
            cached_dialogs=len(self._dialogs_cache),
        )

    async def stop(self) -> None:
        """Stop the Telegram client."""
        await self.app.stop()
        logger.info("telegram_client.stopped")

    async def _cache_dialogs(self) -> None:
        """Cache all dialogs for quick access."""
        async with self._cache_lock:
            async for dialog in self.app.get_dialogs():
                self._dialogs_cache[dialog.chat.id] = dialog
                self._chats_cache[dialog.chat.id] = dialog.chat

        logger.debug(
            "telegram_client.dialogs_cached",
            count=len(self._dialogs_cache),
        )

    async def get_chats(self) -> List[Dict[str, Any]]:
        """
        Get list of all accessible chats.

        Returns:
            List of chat dictionaries with id, title, type, etc.
        """
        # Apply whitelist filter if configured
        allowed_chats = self.config.telegram.allowed_chats

        chats = []
        async with self._cache_lock:
            for chat_id, chat in self._chats_cache.items():
                # Apply whitelist
                if allowed_chats and str(chat_id) not in allowed_chats:
                    continue

                chats.append({
                    "id": chat.id,
                    "title": chat.title or chat.first_name or "Unknown",
                    "type": str(chat.type),
                    "username": chat.username,
                    "is_verified": getattr(chat, 'is_verified', False),
                    "is_scam": getattr(chat, 'is_scam', False),
                    "members_count": getattr(chat, 'members_count', None),
                })

        return chats

    async def get_chat_info(self, chat_id: int) -> Dict[str, Any]:
        """
        Get detailed information about a chat.

        Args:
            chat_id: Telegram chat ID

        Returns:
            Chat information dictionary
        """
        try:
            chat = await self.app.get_chat(chat_id)

            return {
                "id": chat.id,
                "title": chat.title or chat.first_name or "Unknown",
                "type": str(chat.type),
                "username": chat.username,
                "description": chat.description,
                "members_count": chat.members_count,
                "is_verified": getattr(chat, 'is_verified', False),
                "is_restricted": getattr(chat, 'is_restricted', False),
                "is_creator": getattr(chat, 'is_creator', False),
                "photo": {
                    "small_file_id": chat.photo.small_file_id if chat.photo else None,
                    "big_file_id": chat.photo.big_file_id if chat.photo else None,
                } if chat.photo else None,
            }

        except RPCError as e:
            logger.error(
                "telegram.get_chat_info_failed",
                chat_id=chat_id,
                error=str(e),
            )
            raise

    async def read_messages(
        self,
        chat_id: int,
        limit: int = 20,
        offset_id: int = 0,
    ) -> List[Dict[str, Any]]:
        """
        Read messages from a chat.

        This is the FULL message history access that Bot API doesn't provide!

        Args:
            chat_id: Telegram chat ID
            limit: Number of messages to fetch (default: 20)
            offset_id: Message ID to start from (0 = most recent)

        Returns:
            List of message dictionaries
        """
        # Apply limit constraints
        limit = min(limit, self.config.limits.max_message_limit)

        try:
            messages = []

            # Get messages using Pyrogram's get_chat_history
            # This works for ANY chat you're a member of (not just bot chats)
            async for message in self.app.get_chat_history(
                chat_id=chat_id,
                limit=limit,
                offset_id=offset_id,
            ):
                messages.append(self._format_message(message))

            logger.info(
                "telegram.messages_read",
                chat_id=chat_id,
                count=len(messages),
            )

            return messages

        except FloodWait as e:
            logger.warning(
                "telegram.flood_wait",
                chat_id=chat_id,
                wait_seconds=e.value,
            )
            # Wait and retry
            await asyncio.sleep(e.value)
            return await self.read_messages(chat_id, limit, offset_id)

        except RPCError as e:
            logger.error(
                "telegram.read_messages_failed",
                chat_id=chat_id,
                error=str(e),
            )
            raise

    def _format_message(self, message: Message) -> Dict[str, Any]:
        """
        Format a Pyrogram Message object to dictionary.

        Args:
            message: Pyrogram Message

        Returns:
            Formatted message dictionary
        """
        return {
            "message_id": message.id,
            "date": message.date.isoformat() if message.date else None,
            "chat": {
                "id": message.chat.id,
                "title": message.chat.title or message.chat.first_name,
                "type": str(message.chat.type),
            } if message.chat else None,
            "from_user": {
                "id": message.from_user.id,
                "first_name": message.from_user.first_name,
                "last_name": message.from_user.last_name,
                "username": message.from_user.username,
                "is_bot": message.from_user.is_bot,
            } if message.from_user else None,
            "text": message.text,
            "caption": message.caption,
            "media": self._format_media(message),
            "reply_to_message_id": message.reply_to_message_id,
            "edit_date": message.edit_date.isoformat() if message.edit_date else None,
            "views": message.views,
            "forwards": message.forwards,
        }

    def _format_media(self, message: Message) -> Optional[Dict[str, Any]]:
        """Format message media information."""
        if message.photo:
            return {
                "type": "photo",
                "file_id": message.photo.file_id,
                "file_size": message.photo.file_size,
                "width": message.photo.width,
                "height": message.photo.height,
            }
        elif message.video:
            return {
                "type": "video",
                "file_id": message.video.file_id,
                "file_size": message.video.file_size,
                "duration": message.video.duration,
                "width": message.video.width,
                "height": message.video.height,
            }
        elif message.voice:
            return {
                "type": "voice",
                "file_id": message.voice.file_id,
                "file_size": message.voice.file_size,
                "duration": message.voice.duration,
            }
        elif message.document:
            return {
                "type": "document",
                "file_id": message.document.file_id,
                "file_name": message.document.file_name,
                "file_size": message.document.file_size,
                "mime_type": message.document.mime_type,
            }
        elif message.audio:
            return {
                "type": "audio",
                "file_id": message.audio.file_id,
                "file_size": message.audio.file_size,
                "duration": message.audio.duration,
                "title": message.audio.title,
                "performer": message.audio.performer,
            }
        elif message.sticker:
            return {
                "type": "sticker",
                "file_id": message.sticker.file_id,
                "emoji": message.sticker.emoji,
                "set_name": message.sticker.set_name,
            }

        return None

    async def send_message(
        self,
        chat_id: int,
        text: str,
        parse_mode: str = "Markdown",
        reply_to_message_id: Optional[int] = None,
    ) -> Dict[str, Any]:
        """
        Send a message to a chat.

        Args:
            chat_id: Telegram chat ID
            text: Message text
            parse_mode: Parse mode (Markdown, HTML, or None)
            reply_to_message_id: Message ID to reply to

        Returns:
            Sent message information
        """
        try:
            message = await self.app.send_message(
                chat_id=chat_id,
                text=text,
                parse_mode=parse_mode if parse_mode else None,
                reply_to_message_id=reply_to_message_id,
            )

            logger.info(
                "telegram.message_sent",
                chat_id=chat_id,
                message_id=message.id,
            )

            return self._format_message(message)

        except FloodWait as e:
            logger.warning(
                "telegram.flood_wait",
                chat_id=chat_id,
                wait_seconds=e.value,
            )
            await asyncio.sleep(e.value)
            return await self.send_message(chat_id, text, parse_mode, reply_to_message_id)

        except RPCError as e:
            logger.error(
                "telegram.send_message_failed",
                chat_id=chat_id,
                error=str(e),
            )
            raise

    async def get_user_info(self, user_id: int) -> Dict[str, Any]:
        """
        Get information about a user.

        Args:
            user_id: Telegram user ID

        Returns:
            User information dictionary
        """
        try:
            user = await self.app.get_users(user_id)

            return {
                "id": user.id,
                "first_name": user.first_name,
                "last_name": user.last_name,
                "username": user.username,
                "phone_number": user.phone_number,
                "is_bot": user.is_bot,
                "is_verified": user.is_verified,
                "is_restricted": user.is_restricted,
                "is_scam": user.is_scam,
                "status": str(user.status) if user.status else None,
                "photo": {
                    "small_file_id": user.photo.small_file_id if user.photo else None,
                    "big_file_id": user.photo.big_file_id if user.photo else None,
                } if user.photo else None,
            }

        except RPCError as e:
            logger.error(
                "telegram.get_user_info_failed",
                user_id=user_id,
                error=str(e),
            )
            raise

    async def get_chat_members(
        self,
        chat_id: int,
        limit: int = 100,
    ) -> List[Dict[str, Any]]:
        """
        Get members of a chat (group or channel).

        Args:
            chat_id: Telegram chat ID
            limit: Maximum number of members to fetch

        Returns:
            List of chat member dictionaries
        """
        try:
            members = []

            async for member in self.app.get_chat_members(chat_id, limit=limit):
                members.append({
                    "user": {
                        "id": member.user.id,
                        "first_name": member.user.first_name,
                        "last_name": member.user.last_name,
                        "username": member.user.username,
                        "is_bot": member.user.is_bot,
                    },
                    "status": str(member.status),
                    "joined_date": member.joined_date.isoformat() if member.joined_date else None,
                })

            logger.info(
                "telegram.chat_members_retrieved",
                chat_id=chat_id,
                count=len(members),
            )

            return members

        except RPCError as e:
            logger.error(
                "telegram.get_chat_members_failed",
                chat_id=chat_id,
                error=str(e),
            )
            raise

    async def search_messages(
        self,
        chat_id: int,
        query: str,
        limit: int = 50,
    ) -> List[Dict[str, Any]]:
        """
        Search messages in a chat.

        Args:
            chat_id: Telegram chat ID
            query: Search query
            limit: Maximum number of results

        Returns:
            List of matching messages
        """
        try:
            messages = []

            async for message in self.app.search_messages(
                chat_id=chat_id,
                query=query,
                limit=limit,
            ):
                messages.append(self._format_message(message))

            logger.info(
                "telegram.messages_searched",
                chat_id=chat_id,
                query=query,
                count=len(messages),
            )

            return messages

        except RPCError as e:
            logger.error(
                "telegram.search_messages_failed",
                chat_id=chat_id,
                error=str(e),
            )
            raise

    async def download_media(
        self,
        message: Message,
        file_name: Optional[str] = None,
    ) -> Optional[str]:
        """
        Download media from a message.

        Args:
            message: Pyrogram Message object
            file_name: Optional custom file name

        Returns:
            Path to downloaded file, or None if no media
        """
        try:
            if not message.media:
                return None

            file_path = await self.app.download_media(
                message=message,
                file_name=file_name,
            )

            logger.info(
                "telegram.media_downloaded",
                message_id=message.id,
                file_path=file_path,
            )

            return file_path

        except RPCError as e:
            logger.error(
                "telegram.download_media_failed",
                message_id=message.id,
                error=str(e),
            )
            raise
