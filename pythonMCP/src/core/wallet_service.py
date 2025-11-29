"""
Telegram Wallet and Stars Service

Provides access to:
- Telegram Stars balance and transactions
- Premium subscription status
- Gift/monetization features
- Crypto wallet (TON) integration if available
"""

import asyncio
from datetime import datetime
from typing import Any, Dict, List, Optional

import structlog
from pyrogram import Client
from pyrogram.raw import functions, types
from pyrogram.errors import RPCError

logger = structlog.get_logger()


class WalletService:
    """
    Service for Telegram wallet, Stars, and premium features.

    Uses Pyrogram's raw MTProto API for full access to wallet features.
    """

    def __init__(self, client: Client):
        """
        Initialize wallet service.

        Args:
            client: Pyrogram client instance
        """
        self.client = client
        self._stars_balance_cache: Optional[int] = None
        self._cache_time: Optional[datetime] = None
        self._cache_ttl = 60  # Cache for 60 seconds

        logger.info("wallet_service.initialized")

    async def get_stars_balance(self, force_refresh: bool = False) -> Dict[str, Any]:
        """
        Get current Telegram Stars balance.

        Args:
            force_refresh: Force refresh from API

        Returns:
            Dict with balance information
        """
        # Check cache
        if not force_refresh and self._stars_balance_cache is not None:
            if self._cache_time and (datetime.now() - self._cache_time).seconds < self._cache_ttl:
                return {
                    "balance": self._stars_balance_cache,
                    "cached": True,
                    "cache_time": self._cache_time.isoformat(),
                }

        try:
            # Use raw API to get stars status
            # payments.getStarsStatus returns StarsStatus with balance
            result = await self.client.invoke(
                functions.payments.GetStarsStatus(
                    peer=types.InputPeerSelf()
                )
            )

            balance = getattr(result, 'balance', 0)

            # Update cache
            self._stars_balance_cache = balance
            self._cache_time = datetime.now()

            logger.info(
                "wallet_service.stars_balance_fetched",
                balance=balance,
            )

            return {
                "balance": balance,
                "cached": False,
                "fetched_at": self._cache_time.isoformat(),
            }

        except RPCError as e:
            logger.error(
                "wallet_service.get_stars_balance_failed",
                error=str(e),
            )
            # Return cached value if available
            if self._stars_balance_cache is not None:
                return {
                    "balance": self._stars_balance_cache,
                    "cached": True,
                    "error": str(e),
                }
            raise

    async def get_stars_transactions(
        self,
        limit: int = 50,
        offset: str = "",
        inbound: bool = True,
        outbound: bool = True,
    ) -> Dict[str, Any]:
        """
        Get Stars transaction history.

        Args:
            limit: Maximum number of transactions
            offset: Pagination offset
            inbound: Include incoming transactions
            outbound: Include outgoing transactions

        Returns:
            Dict with transactions list
        """
        try:
            result = await self.client.invoke(
                functions.payments.GetStarsTransactions(
                    peer=types.InputPeerSelf(),
                    offset=offset,
                    limit=limit,
                    inbound=inbound,
                    outbound=outbound,
                )
            )

            transactions = []
            for tx in getattr(result, 'history', []):
                transactions.append(self._format_transaction(tx))

            logger.info(
                "wallet_service.stars_transactions_fetched",
                count=len(transactions),
            )

            return {
                "transactions": transactions,
                "next_offset": getattr(result, 'next_offset', None),
                "balance": getattr(result, 'balance', 0),
            }

        except RPCError as e:
            logger.error(
                "wallet_service.get_stars_transactions_failed",
                error=str(e),
            )
            raise

    def _format_transaction(self, tx: Any) -> Dict[str, Any]:
        """Format a Stars transaction for output."""
        return {
            "id": getattr(tx, 'id', None),
            "stars": getattr(tx, 'stars', 0),
            "date": datetime.fromtimestamp(
                getattr(tx, 'date', 0)
            ).isoformat() if getattr(tx, 'date', 0) else None,
            "peer": self._format_peer(getattr(tx, 'peer', None)),
            "title": getattr(tx, 'title', None),
            "description": getattr(tx, 'description', None),
            "transaction_type": self._get_transaction_type(tx),
            "pending": getattr(tx, 'pending', False),
            "failed": getattr(tx, 'failed', False),
            "refund": getattr(tx, 'refund', False),
        }

    def _format_peer(self, peer: Any) -> Optional[Dict[str, Any]]:
        """Format peer information."""
        if peer is None:
            return None

        if isinstance(peer, types.StarsTransactionPeerUnsupported):
            return {"type": "unsupported"}
        elif isinstance(peer, types.StarsTransactionPeerPremiumBot):
            return {"type": "premium_bot"}
        elif isinstance(peer, types.StarsTransactionPeerAppStore):
            return {"type": "app_store"}
        elif isinstance(peer, types.StarsTransactionPeerPlayMarket):
            return {"type": "play_market"}
        elif isinstance(peer, types.StarsTransactionPeerFragment):
            return {"type": "fragment"}
        elif isinstance(peer, types.StarsTransactionPeerAds):
            return {"type": "ads"}
        elif hasattr(peer, 'peer'):
            # Has actual peer information
            return {
                "type": "peer",
                "peer_id": self._extract_peer_id(peer.peer),
            }

        return {"type": "unknown"}

    def _extract_peer_id(self, peer: Any) -> Optional[int]:
        """Extract peer ID from peer object."""
        if hasattr(peer, 'user_id'):
            return peer.user_id
        elif hasattr(peer, 'channel_id'):
            return peer.channel_id
        elif hasattr(peer, 'chat_id'):
            return peer.chat_id
        return None

    def _get_transaction_type(self, tx: Any) -> str:
        """Determine transaction type."""
        if getattr(tx, 'refund', False):
            return "refund"
        if getattr(tx, 'stars', 0) > 0:
            return "incoming"
        return "outgoing"

    async def get_premium_status(self) -> Dict[str, Any]:
        """
        Get current user's premium subscription status.

        Returns:
            Dict with premium status information
        """
        try:
            # Get full user info for self
            me = await self.client.get_me()

            premium = getattr(me, 'is_premium', False)

            result = {
                "is_premium": premium,
                "user_id": me.id,
                "username": me.username,
            }

            # Try to get more premium details if available
            try:
                full_user = await self.client.invoke(
                    functions.users.GetFullUser(
                        id=types.InputUserSelf()
                    )
                )
                if hasattr(full_user, 'full_user'):
                    fu = full_user.full_user
                    result.update({
                        "premium_gifts_allowed": getattr(fu, 'premium_gifts', False),
                        "birthday": getattr(fu, 'birthday', None),
                    })
            except Exception:
                pass  # Optional extended info

            logger.info(
                "wallet_service.premium_status_fetched",
                is_premium=premium,
            )

            return result

        except RPCError as e:
            logger.error(
                "wallet_service.get_premium_status_failed",
                error=str(e),
            )
            raise

    async def get_gifts(self, user_id: Optional[int] = None) -> Dict[str, Any]:
        """
        Get saved gifts for user.

        Args:
            user_id: User ID (defaults to self)

        Returns:
            Dict with gifts information
        """
        try:
            if user_id is None:
                input_user = types.InputUserSelf()
            else:
                input_user = await self.client.resolve_peer(user_id)

            result = await self.client.invoke(
                functions.payments.GetUserStarGifts(
                    user_id=input_user,
                    offset="",
                    limit=100,
                )
            )

            gifts = []
            for gift in getattr(result, 'gifts', []):
                gifts.append({
                    "id": getattr(gift, 'id', None),
                    "from_id": getattr(gift, 'from_id', None),
                    "date": datetime.fromtimestamp(
                        getattr(gift, 'date', 0)
                    ).isoformat() if getattr(gift, 'date', 0) else None,
                    "stars": getattr(gift, 'stars', 0),
                    "message": getattr(gift, 'message', None),
                    "converted": getattr(gift, 'converted', False),
                })

            logger.info(
                "wallet_service.gifts_fetched",
                count=len(gifts),
            )

            return {
                "gifts": gifts,
                "count": len(gifts),
            }

        except RPCError as e:
            logger.error(
                "wallet_service.get_gifts_failed",
                error=str(e),
            )
            # Return empty result if gifts not available
            return {"gifts": [], "count": 0, "error": str(e)}

    async def get_boosts_status(self, chat_id: int) -> Dict[str, Any]:
        """
        Get boost status for a channel/supergroup.

        Args:
            chat_id: Channel or supergroup ID

        Returns:
            Dict with boost status
        """
        try:
            peer = await self.client.resolve_peer(chat_id)

            result = await self.client.invoke(
                functions.premium.GetBoostsStatus(
                    peer=peer
                )
            )

            return {
                "my_boost": getattr(result, 'my_boost', False),
                "level": getattr(result, 'level', 0),
                "current_level_boosts": getattr(result, 'current_level_boosts', 0),
                "boosts": getattr(result, 'boosts', 0),
                "next_level_boosts": getattr(result, 'next_level_boosts', None),
                "premium_audience": getattr(result, 'premium_audience', None),
                "boost_url": getattr(result, 'boost_url', None),
            }

        except RPCError as e:
            logger.error(
                "wallet_service.get_boosts_status_failed",
                chat_id=chat_id,
                error=str(e),
            )
            raise

    async def get_revenue_stats(self, chat_id: int) -> Dict[str, Any]:
        """
        Get revenue statistics for a channel (for channel admins).

        Args:
            chat_id: Channel ID

        Returns:
            Dict with revenue statistics
        """
        try:
            peer = await self.client.resolve_peer(chat_id)

            result = await self.client.invoke(
                functions.payments.GetStarsRevenueStats(
                    peer=peer,
                )
            )

            return {
                "current_balance": getattr(result, 'current_balance', 0),
                "available_balance": getattr(result, 'available_balance', 0),
                "overall_revenue": getattr(result, 'overall_revenue', 0),
                "usd_rate": getattr(result, 'usd_rate', None),
            }

        except RPCError as e:
            logger.error(
                "wallet_service.get_revenue_stats_failed",
                chat_id=chat_id,
                error=str(e),
            )
            raise

    async def get_subscriptions(self) -> Dict[str, Any]:
        """
        Get active Stars subscriptions.

        Returns:
            Dict with active subscriptions
        """
        try:
            result = await self.client.invoke(
                functions.payments.GetStarsSubscriptions(
                    peer=types.InputPeerSelf(),
                    offset="",
                )
            )

            subscriptions = []
            for sub in getattr(result, 'subscriptions', []):
                subscriptions.append({
                    "id": getattr(sub, 'id', None),
                    "peer_id": self._extract_peer_id(getattr(sub, 'peer', None)),
                    "until_date": datetime.fromtimestamp(
                        getattr(sub, 'until_date', 0)
                    ).isoformat() if getattr(sub, 'until_date', 0) else None,
                    "pricing": {
                        "amount": getattr(getattr(sub, 'pricing', None), 'amount', 0),
                        "period": getattr(getattr(sub, 'pricing', None), 'period', None),
                    } if getattr(sub, 'pricing', None) else None,
                    "canceled": getattr(sub, 'canceled', False),
                    "missing_balance": getattr(sub, 'missing_balance', False),
                })

            logger.info(
                "wallet_service.subscriptions_fetched",
                count=len(subscriptions),
            )

            return {
                "subscriptions": subscriptions,
                "count": len(subscriptions),
                "balance": getattr(result, 'balance', 0),
            }

        except RPCError as e:
            logger.error(
                "wallet_service.get_subscriptions_failed",
                error=str(e),
            )
            return {"subscriptions": [], "count": 0, "error": str(e)}

    def get_stats(self) -> Dict[str, Any]:
        """Get wallet service statistics."""
        return {
            "cached_balance": self._stars_balance_cache,
            "cache_time": self._cache_time.isoformat() if self._cache_time else None,
            "cache_ttl": self._cache_ttl,
        }


# Factory function
_wallet_service: Optional[WalletService] = None


async def get_wallet_service(client: Client) -> WalletService:
    """
    Get or create wallet service instance.

    Args:
        client: Pyrogram client

    Returns:
        WalletService instance
    """
    global _wallet_service

    if _wallet_service is None:
        _wallet_service = WalletService(client)

    return _wallet_service
