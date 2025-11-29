"""
FAISS-based Semantic Search for Telegram Messages

A lightweight, fast semantic search implementation using FAISS.
Advantages over ChromaDB:
- Faster pure vector search
- Lower memory footprint
- Better for large-scale search
- Optimized for Apple Silicon (uses accelerate)

Requires: pip install faiss-cpu sentence-transformers
For M1/M2 acceleration: pip install faiss-cpu accelerate
"""

import json
import pickle
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
import asyncio

import structlog

logger = structlog.get_logger()

# Optional imports with fallback
try:
    import numpy as np
    import faiss
    from sentence_transformers import SentenceTransformer
    FAISS_AVAILABLE = True
except ImportError:
    FAISS_AVAILABLE = False
    logger.warning("faiss_search.dependencies_missing",
                   message="Install with: pip install faiss-cpu sentence-transformers")


class FAISSSemanticSearch:
    """
    FAISS-based semantic search for messages.

    Uses sentence-transformers for embeddings and FAISS for fast similarity search.
    Optimized for Apple Silicon with MPS when available.
    """

    def __init__(
        self,
        model_name: str = "all-MiniLM-L6-v2",
        index_path: Optional[str] = None,
        dimension: int = 384,  # MiniLM-L6-v2 dimension
    ):
        """
        Initialize FAISS semantic search.

        Args:
            model_name: Sentence transformer model name
            index_path: Path to persist the index
            dimension: Embedding dimension
        """
        if not FAISS_AVAILABLE:
            raise ImportError(
                "FAISS dependencies not available. "
                "Install with: pip install faiss-cpu sentence-transformers"
            )

        self.model_name = model_name
        self.dimension = dimension
        self.index_path = Path(index_path) if index_path else None

        # Initialize embedding model
        self._model = None
        self._index = None
        self._metadata: List[Dict[str, Any]] = []
        self._id_to_idx: Dict[str, int] = {}

        logger.info(
            "faiss_search.initialized",
            model=model_name,
            dimension=dimension,
            index_path=str(index_path) if index_path else None,
        )

    async def _ensure_model(self) -> None:
        """Lazy-load the embedding model."""
        if self._model is None:
            # Run in executor to avoid blocking
            loop = asyncio.get_event_loop()
            self._model = await loop.run_in_executor(
                None,
                lambda: SentenceTransformer(self.model_name)
            )
            logger.info("faiss_search.model_loaded", model=self.model_name)

    async def _ensure_index(self) -> None:
        """Ensure FAISS index is initialized."""
        if self._index is None:
            if self.index_path and self.index_path.exists():
                await self.load_index()
            else:
                # Create new index
                # Using IndexFlatIP (Inner Product) for cosine similarity
                # (assumes normalized vectors)
                self._index = faiss.IndexFlatIP(self.dimension)
                self._metadata = []
                self._id_to_idx = {}
                logger.info("faiss_search.index_created", dimension=self.dimension)

    async def embed_text(self, text: str) -> np.ndarray:
        """
        Generate embedding for text.

        Args:
            text: Text to embed

        Returns:
            Normalized embedding vector
        """
        await self._ensure_model()

        loop = asyncio.get_event_loop()
        embedding = await loop.run_in_executor(
            None,
            lambda: self._model.encode(text, convert_to_numpy=True, normalize_embeddings=True)
        )

        return embedding

    async def embed_batch(self, texts: List[str]) -> np.ndarray:
        """
        Generate embeddings for multiple texts.

        Args:
            texts: List of texts to embed

        Returns:
            Array of normalized embeddings
        """
        await self._ensure_model()

        loop = asyncio.get_event_loop()
        embeddings = await loop.run_in_executor(
            None,
            lambda: self._model.encode(
                texts,
                convert_to_numpy=True,
                normalize_embeddings=True,
                batch_size=32,
                show_progress_bar=False
            )
        )

        return embeddings

    async def add_message(
        self,
        message_id: str,
        chat_id: str,
        text: str,
        metadata: Optional[Dict[str, Any]] = None,
    ) -> None:
        """
        Add a message to the search index.

        Args:
            message_id: Unique message identifier
            chat_id: Chat identifier
            text: Message text
            metadata: Additional metadata to store
        """
        await self._ensure_index()

        # Create unique ID
        unique_id = f"{chat_id}_{message_id}"

        # Skip if already indexed
        if unique_id in self._id_to_idx:
            return

        # Get embedding
        embedding = await self.embed_text(text)

        # Add to index
        idx = self._index.ntotal
        self._index.add(embedding.reshape(1, -1))

        # Store metadata
        full_metadata = {
            "message_id": message_id,
            "chat_id": chat_id,
            "text": text,
            **(metadata or {})
        }
        self._metadata.append(full_metadata)
        self._id_to_idx[unique_id] = idx

        logger.debug(
            "faiss_search.message_added",
            chat_id=chat_id,
            message_id=message_id,
        )

    async def add_messages_batch(
        self,
        messages: List[Dict[str, Any]],
    ) -> int:
        """
        Add multiple messages to the index in batch.

        Args:
            messages: List of message dicts with chat_id, message_id, text, metadata

        Returns:
            Number of messages added
        """
        await self._ensure_index()

        # Filter out already indexed messages
        new_messages = []
        for msg in messages:
            unique_id = f"{msg['chat_id']}_{msg['message_id']}"
            if unique_id not in self._id_to_idx:
                new_messages.append(msg)

        if not new_messages:
            return 0

        # Extract texts for batch embedding
        texts = [msg["text"] for msg in new_messages]
        embeddings = await self.embed_batch(texts)

        # Add to index
        start_idx = self._index.ntotal
        self._index.add(embeddings)

        # Store metadata
        for i, msg in enumerate(new_messages):
            unique_id = f"{msg['chat_id']}_{msg['message_id']}"
            full_metadata = {
                "message_id": msg["message_id"],
                "chat_id": msg["chat_id"],
                "text": msg["text"],
                **(msg.get("metadata", {}))
            }
            self._metadata.append(full_metadata)
            self._id_to_idx[unique_id] = start_idx + i

        logger.info(
            "faiss_search.batch_added",
            count=len(new_messages),
            total=self._index.ntotal,
        )

        return len(new_messages)

    async def search(
        self,
        query: str,
        chat_id: Optional[str] = None,
        top_k: int = 10,
    ) -> List[Dict[str, Any]]:
        """
        Semantic search for similar messages.

        Args:
            query: Search query
            chat_id: Optional chat ID to filter results
            top_k: Maximum number of results

        Returns:
            List of matching messages with similarity scores
        """
        await self._ensure_index()

        if self._index.ntotal == 0:
            return []

        # Get query embedding
        query_embedding = await self.embed_text(query)

        # Search index (request more results if filtering by chat)
        search_k = top_k * 5 if chat_id else top_k
        search_k = min(search_k, self._index.ntotal)

        distances, indices = self._index.search(
            query_embedding.reshape(1, -1),
            search_k
        )

        # Build results
        results = []
        for dist, idx in zip(distances[0], indices[0]):
            if idx < 0:  # Invalid index
                continue

            metadata = self._metadata[idx]

            # Filter by chat_id if specified
            if chat_id and metadata.get("chat_id") != chat_id:
                continue

            results.append({
                "text": metadata.get("text", ""),
                "chat_id": metadata.get("chat_id"),
                "message_id": metadata.get("message_id"),
                "similarity": float(dist),  # Inner product = cosine similarity for normalized vectors
                "metadata": {k: v for k, v in metadata.items()
                            if k not in ("text", "chat_id", "message_id")}
            })

            if len(results) >= top_k:
                break

        logger.debug(
            "faiss_search.search_complete",
            query=query[:50],
            chat_id=chat_id,
            result_count=len(results),
        )

        return results

    async def delete_message(self, chat_id: str, message_id: str) -> bool:
        """
        Remove a message from the index.

        Note: FAISS doesn't support direct deletion. We mark as deleted
        and rebuild index periodically.

        Args:
            chat_id: Chat ID
            message_id: Message ID

        Returns:
            True if message was found and marked for deletion
        """
        unique_id = f"{chat_id}_{message_id}"

        if unique_id not in self._id_to_idx:
            return False

        idx = self._id_to_idx[unique_id]

        # Mark as deleted in metadata
        if idx < len(self._metadata):
            self._metadata[idx]["_deleted"] = True
            del self._id_to_idx[unique_id]
            return True

        return False

    async def rebuild_index(self) -> int:
        """
        Rebuild index, removing deleted messages.

        Returns:
            Number of messages in new index
        """
        await self._ensure_model()

        # Filter out deleted messages
        active_metadata = [
            m for m in self._metadata
            if not m.get("_deleted", False)
        ]

        if not active_metadata:
            self._index = faiss.IndexFlatIP(self.dimension)
            self._metadata = []
            self._id_to_idx = {}
            return 0

        # Get texts and re-embed
        texts = [m["text"] for m in active_metadata]
        embeddings = await self.embed_batch(texts)

        # Create new index
        self._index = faiss.IndexFlatIP(self.dimension)
        self._index.add(embeddings)

        # Rebuild metadata and ID map
        self._metadata = active_metadata
        self._id_to_idx = {}
        for i, m in enumerate(self._metadata):
            unique_id = f"{m['chat_id']}_{m['message_id']}"
            self._id_to_idx[unique_id] = i

        logger.info("faiss_search.index_rebuilt", count=self._index.ntotal)

        return self._index.ntotal

    async def save_index(self, path: Optional[str] = None) -> None:
        """
        Persist index and metadata to disk.

        Args:
            path: Optional path override
        """
        save_path = Path(path) if path else self.index_path

        if save_path is None:
            raise ValueError("No index path specified")

        save_path.parent.mkdir(parents=True, exist_ok=True)

        # Save FAISS index
        faiss.write_index(self._index, str(save_path.with_suffix(".faiss")))

        # Save metadata
        with open(save_path.with_suffix(".meta"), "wb") as f:
            pickle.dump({
                "metadata": self._metadata,
                "id_to_idx": self._id_to_idx,
            }, f)

        logger.info(
            "faiss_search.index_saved",
            path=str(save_path),
            count=self._index.ntotal,
        )

    async def load_index(self, path: Optional[str] = None) -> None:
        """
        Load index and metadata from disk.

        Args:
            path: Optional path override
        """
        load_path = Path(path) if path else self.index_path

        if load_path is None:
            raise ValueError("No index path specified")

        faiss_path = load_path.with_suffix(".faiss")
        meta_path = load_path.with_suffix(".meta")

        if not faiss_path.exists() or not meta_path.exists():
            raise FileNotFoundError(f"Index files not found at {load_path}")

        # Load FAISS index
        self._index = faiss.read_index(str(faiss_path))

        # Load metadata
        with open(meta_path, "rb") as f:
            data = pickle.load(f)
            self._metadata = data["metadata"]
            self._id_to_idx = data["id_to_idx"]

        logger.info(
            "faiss_search.index_loaded",
            path=str(load_path),
            count=self._index.ntotal,
        )

    def get_stats(self) -> Dict[str, Any]:
        """Get index statistics."""
        return {
            "total_messages": self._index.ntotal if self._index else 0,
            "dimension": self.dimension,
            "model": self.model_name,
            "index_path": str(self.index_path) if self.index_path else None,
            "available": FAISS_AVAILABLE,
        }


def is_faiss_available() -> bool:
    """Check if FAISS dependencies are installed."""
    return FAISS_AVAILABLE


# Global instance for convenience
_faiss_search: Optional[FAISSSemanticSearch] = None


async def get_faiss_search(
    model_name: str = "all-MiniLM-L6-v2",
    index_path: Optional[str] = None,
) -> Optional[FAISSSemanticSearch]:
    """
    Get or create global FAISS search instance.

    Args:
        model_name: Sentence transformer model
        index_path: Path to persist index

    Returns:
        FAISSSemanticSearch instance or None if not available
    """
    global _faiss_search

    if not FAISS_AVAILABLE:
        return None

    if _faiss_search is None:
        _faiss_search = FAISSSemanticSearch(
            model_name=model_name,
            index_path=index_path,
        )

    return _faiss_search
