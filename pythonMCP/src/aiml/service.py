"""
AI/ML Service Layer for Telegram MCP

Provides semantic understanding capabilities:
- Sentence embeddings
- Vector search (ChromaDB)
- Intent classification
- Topic extraction
- Conversation summarization

Optimized for Apple Silicon (MPS acceleration).
"""

import asyncio
from typing import Optional, List, Dict, Any
from datetime import datetime

try:
    from sentence_transformers import SentenceTransformer
    import chromadb
    from chromadb.config import Settings
    import torch
    from transformers import pipeline
    from langchain.text_splitter import RecursiveCharacterTextSplitter
    AIML_AVAILABLE = True
except ImportError:
    AIML_AVAILABLE = False

import structlog

logger = structlog.get_logger()


class AIMLService:
    """Centralized AI/ML service for embeddings, search, and inference"""

    def __init__(self, config: Dict[str, Any]):
        if not AIML_AVAILABLE:
            raise ImportError(
                "AI/ML dependencies not installed. "
                "Run: pip install -r requirements.txt"
            )

        self.config = config
        device = config.get("device", "mps" if torch.backends.mps.is_available() else "cpu")

        logger.info("aiml.initializing", device=device)

        # Sentence embeddings model
        embedding_model = config.get("embedding_model", "sentence-transformers/all-MiniLM-L6-v2")
        self.embedding_model = SentenceTransformer(embedding_model, device=device)

        # ChromaDB for vector storage
        db_path = config.get("vector_db_path", "./data/chromadb")
        self.chroma_client = chromadb.Client(Settings(
            chroma_db_impl="duckdb+parquet",
            persist_directory=db_path
        ))

        # Create collections
        self.message_collection = self.chroma_client.get_or_create_collection(
            name="telegram_messages",
            metadata={"description": "Telegram message embeddings"}
        )

        # NLP pipeline for intent classification
        try:
            self.intent_classifier = pipeline(
                "text-classification",
                model="facebook/bart-large-mnli",
                device=0 if device == "mps" else -1
            )
        except Exception as e:
            logger.warning("aiml.intent_classifier_failed", error=str(e))
            self.intent_classifier = None

        # Text splitter for long messages
        self.text_splitter = RecursiveCharacterTextSplitter(
            chunk_size=500,
            chunk_overlap=50
        )

        logger.info("aiml.initialized")

    async def embed_text(self, text: str) -> List[float]:
        """Generate embeddings for text"""
        try:
            embedding = self.embedding_model.encode(text, convert_to_tensor=False)
            return embedding.tolist()
        except Exception as e:
            logger.error("aiml.embed_failed", error=str(e))
            return []

    async def index_message(
        self,
        message_id: int,
        chat_id: int,
        text: str,
        metadata: Optional[Dict[str, Any]] = None
    ):
        """Index a message for semantic search"""
        try:
            embedding = await self.embed_text(text)

            self.message_collection.add(
                embeddings=[embedding],
                documents=[text],
                ids=[f"{chat_id}_{message_id}"],
                metadatas=[{
                    "chat_id": chat_id,
                    "message_id": message_id,
                    "timestamp": datetime.now().isoformat(),
                    **(metadata or {})
                }]
            )

            logger.debug("aiml.message_indexed", chat_id=chat_id, message_id=message_id)
        except Exception as e:
            logger.error("aiml.index_failed", error=str(e))

    async def semantic_search(
        self,
        query: str,
        chat_id: Optional[int] = None,
        top_k: int = 10
    ) -> List[Dict[str, Any]]:
        """Perform semantic search across messages"""
        try:
            query_embedding = await self.embed_text(query)

            where_filter = {"chat_id": chat_id} if chat_id else None

            results = self.message_collection.query(
                query_embeddings=[query_embedding],
                n_results=top_k,
                where=where_filter
            )

            return [
                {
                    "text": doc,
                    "metadata": meta,
                    "distance": dist
                }
                for doc, meta, dist in zip(
                    results["documents"][0],
                    results["metadatas"][0],
                    results["distances"][0]
                )
            ]
        except Exception as e:
            logger.error("aiml.search_failed", error=str(e))
            return []

    async def classify_intent(self, text: str) -> Optional[Dict[str, Any]]:
        """Classify user intent from text"""
        if not self.intent_classifier:
            return None

        try:
            candidate_labels = [
                "question",
                "request",
                "statement",
                "command",
                "greeting",
                "farewell"
            ]

            result = self.intent_classifier(
                text,
                candidate_labels,
                multi_label=False
            )

            return {
                "intent": result["labels"][0],
                "confidence": result["scores"][0]
            }
        except Exception as e:
            logger.error("aiml.classify_failed", error=str(e))
            return None

    async def summarize_conversation(
        self,
        messages: List[str],
        max_length: int = 150
    ) -> str:
        """Generate a summary of a conversation"""
        # Placeholder for summarization
        # Could use BART, T5, or GPT-based summarization
        conversation_text = "\n".join(messages[-20:])  # Last 20 messages
        return f"Summary: {conversation_text[:max_length]}..."


# Global service instance
_aiml_service: Optional[AIMLService] = None


async def get_aiml_service(config: Optional[Dict[str, Any]] = None) -> Optional[AIMLService]:
    """Get or create the global AI/ML service instance"""
    global _aiml_service

    if not AIML_AVAILABLE:
        logger.warning("aiml.not_available",
                      message="AI/ML dependencies not installed")
        return None

    if not _aiml_service and config:
        _aiml_service = AIMLService(config)

    return _aiml_service


def is_aiml_available() -> bool:
    """Check if AI/ML dependencies are installed"""
    return AIML_AVAILABLE
