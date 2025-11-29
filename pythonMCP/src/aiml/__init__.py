"""
AI/ML Module for Telegram MCP

Provides AI/ML capabilities:
- Semantic search (FAISS and ChromaDB backends)
- Sentence embeddings
- Voice transcription (Whisper)
- Intent classification
- Conversation summarization
"""

from typing import Optional

# FAISS-based semantic search (lightweight)
try:
    from .faiss_search import (
        FAISSSemanticSearch,
        get_faiss_search,
        is_faiss_available,
    )
except ImportError:
    FAISSSemanticSearch = None
    get_faiss_search = None
    is_faiss_available = lambda: False

# Full AI/ML service (ChromaDB + transformers)
try:
    from .service import (
        AIMLService,
        get_aiml_service,
        is_aiml_available,
    )
except ImportError:
    AIMLService = None
    get_aiml_service = None
    is_aiml_available = lambda: False

# Whisper voice transcription
try:
    from .whisper_transcription import (
        WhisperTranscription,
        get_whisper_transcription,
        is_whisper_available,
        transcribe_voice_message,
    )
except ImportError:
    WhisperTranscription = None
    get_whisper_transcription = None
    is_whisper_available = lambda: False
    transcribe_voice_message = None


__all__ = [
    # FAISS search
    "FAISSSemanticSearch",
    "get_faiss_search",
    "is_faiss_available",
    # Full AI/ML service
    "AIMLService",
    "get_aiml_service",
    "is_aiml_available",
    # Whisper transcription
    "WhisperTranscription",
    "get_whisper_transcription",
    "is_whisper_available",
    "transcribe_voice_message",
]
