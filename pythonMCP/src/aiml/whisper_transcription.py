"""
Whisper-based Voice Transcription for Telegram Messages

Provides speech-to-text transcription using OpenAI's Whisper model.
Optimized for Telegram voice messages (OGG/Opus format).

Requires: pip install openai-whisper
For faster inference: pip install openai-whisper torch torchvision torchaudio
"""

import asyncio
import tempfile
from pathlib import Path
from typing import Any, Dict, Optional, Union
import os

import structlog

logger = structlog.get_logger()

# Optional imports with fallback
try:
    import whisper
    import numpy as np
    WHISPER_AVAILABLE = True
except ImportError:
    WHISPER_AVAILABLE = False
    logger.warning(
        "whisper_transcription.dependencies_missing",
        message="Install with: pip install openai-whisper"
    )

# Try to import pydub for audio conversion
try:
    from pydub import AudioSegment
    PYDUB_AVAILABLE = True
except ImportError:
    PYDUB_AVAILABLE = False
    logger.info(
        "whisper_transcription.pydub_not_available",
        message="Install pydub for better audio format support: pip install pydub"
    )


class WhisperTranscription:
    """
    Whisper-based voice message transcription.

    Supports multiple Whisper model sizes:
    - tiny: Fastest, least accurate (~1GB VRAM)
    - base: Good balance (~1GB VRAM)
    - small: Better accuracy (~2GB VRAM)
    - medium: High accuracy (~5GB VRAM)
    - large: Best accuracy (~10GB VRAM)

    For Apple Silicon, uses MPS acceleration when available.
    """

    # Supported audio formats
    SUPPORTED_FORMATS = {".ogg", ".mp3", ".wav", ".m4a", ".flac", ".opus", ".webm"}

    def __init__(
        self,
        model_name: str = "base",
        device: Optional[str] = None,
        language: Optional[str] = None,
    ):
        """
        Initialize Whisper transcription.

        Args:
            model_name: Whisper model size (tiny, base, small, medium, large)
            device: Device to use (cpu, cuda, mps). Auto-detected if None.
            language: Default language for transcription. Auto-detected if None.
        """
        if not WHISPER_AVAILABLE:
            raise ImportError(
                "Whisper dependencies not available. "
                "Install with: pip install openai-whisper"
            )

        self.model_name = model_name
        self.language = language
        self._model = None

        # Auto-detect device
        if device is None:
            try:
                import torch
                if torch.backends.mps.is_available():
                    self.device = "mps"  # Apple Silicon
                elif torch.cuda.is_available():
                    self.device = "cuda"
                else:
                    self.device = "cpu"
            except ImportError:
                self.device = "cpu"
        else:
            self.device = device

        logger.info(
            "whisper_transcription.initialized",
            model=model_name,
            device=self.device,
            language=language,
        )

    async def _ensure_model(self) -> None:
        """Lazy-load the Whisper model."""
        if self._model is None:
            loop = asyncio.get_event_loop()
            self._model = await loop.run_in_executor(
                None,
                lambda: whisper.load_model(self.model_name, device=self.device)
            )
            logger.info(
                "whisper_transcription.model_loaded",
                model=self.model_name,
                device=self.device,
            )

    async def transcribe_file(
        self,
        audio_path: Union[str, Path],
        language: Optional[str] = None,
        task: str = "transcribe",
    ) -> Dict[str, Any]:
        """
        Transcribe an audio file.

        Args:
            audio_path: Path to audio file
            language: Language code (e.g., "en", "ru"). Auto-detect if None.
            task: "transcribe" or "translate" (translate to English)

        Returns:
            Dict with:
                - text: Full transcription
                - segments: List of timed segments
                - language: Detected language
                - duration: Audio duration in seconds
        """
        await self._ensure_model()

        audio_path = Path(audio_path)

        if not audio_path.exists():
            raise FileNotFoundError(f"Audio file not found: {audio_path}")

        # Check format
        suffix = audio_path.suffix.lower()
        if suffix not in self.SUPPORTED_FORMATS:
            raise ValueError(
                f"Unsupported audio format: {suffix}. "
                f"Supported: {self.SUPPORTED_FORMATS}"
            )

        # Use provided language or default
        lang = language or self.language

        # Transcribe in executor to avoid blocking
        loop = asyncio.get_event_loop()

        try:
            result = await loop.run_in_executor(
                None,
                lambda: self._model.transcribe(
                    str(audio_path),
                    language=lang,
                    task=task,
                    fp16=self.device != "cpu",  # Use FP16 on GPU
                )
            )
        except Exception as e:
            logger.error(
                "whisper_transcription.transcribe_failed",
                path=str(audio_path),
                error=str(e),
            )
            raise

        # Extract segments with timing
        segments = [
            {
                "start": seg["start"],
                "end": seg["end"],
                "text": seg["text"].strip(),
            }
            for seg in result.get("segments", [])
        ]

        # Calculate duration from segments
        duration = segments[-1]["end"] if segments else 0

        transcription = {
            "text": result["text"].strip(),
            "segments": segments,
            "language": result.get("language", lang),
            "duration": duration,
        }

        logger.info(
            "whisper_transcription.transcribe_complete",
            path=str(audio_path),
            language=transcription["language"],
            duration=duration,
            text_length=len(transcription["text"]),
        )

        return transcription

    async def transcribe_bytes(
        self,
        audio_data: bytes,
        format: str = "ogg",
        language: Optional[str] = None,
        task: str = "transcribe",
    ) -> Dict[str, Any]:
        """
        Transcribe audio from bytes.

        Args:
            audio_data: Raw audio bytes
            format: Audio format (ogg, mp3, wav, etc.)
            language: Language code. Auto-detect if None.
            task: "transcribe" or "translate"

        Returns:
            Dict with text, segments, language, duration
        """
        # Write to temp file
        suffix = f".{format.lstrip('.')}"

        with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as f:
            f.write(audio_data)
            temp_path = f.name

        try:
            return await self.transcribe_file(temp_path, language, task)
        finally:
            # Clean up temp file
            try:
                os.unlink(temp_path)
            except OSError:
                pass

    async def transcribe_telegram_voice(
        self,
        voice_path: Union[str, Path],
        language: Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Transcribe a Telegram voice message.

        Telegram voice messages are typically OGG/Opus format.
        This method handles conversion if needed.

        Args:
            voice_path: Path to voice message file
            language: Language code. Auto-detect if None.

        Returns:
            Dict with text, segments, language, duration
        """
        voice_path = Path(voice_path)

        # Telegram voice messages are usually .oga or .ogg (Opus codec)
        # Whisper handles these directly, but we might need conversion
        suffix = voice_path.suffix.lower()

        if suffix in {".oga", ".ogg", ".opus"}:
            # Direct transcription
            return await self.transcribe_file(voice_path, language)

        elif PYDUB_AVAILABLE:
            # Convert to WAV using pydub
            loop = asyncio.get_event_loop()

            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                temp_path = f.name

            try:
                await loop.run_in_executor(
                    None,
                    lambda: AudioSegment.from_file(str(voice_path)).export(
                        temp_path, format="wav"
                    )
                )
                return await self.transcribe_file(temp_path, language)
            finally:
                try:
                    os.unlink(temp_path)
                except OSError:
                    pass
        else:
            # Try direct transcription
            return await self.transcribe_file(voice_path, language)

    def get_stats(self) -> Dict[str, Any]:
        """Get transcription service statistics."""
        return {
            "model": self.model_name,
            "device": self.device,
            "language": self.language,
            "model_loaded": self._model is not None,
            "available": WHISPER_AVAILABLE,
            "pydub_available": PYDUB_AVAILABLE,
        }


def is_whisper_available() -> bool:
    """Check if Whisper dependencies are installed."""
    return WHISPER_AVAILABLE


# Global instance for convenience
_whisper_transcription: Optional[WhisperTranscription] = None


async def get_whisper_transcription(
    model_name: str = "base",
    device: Optional[str] = None,
    language: Optional[str] = None,
) -> Optional[WhisperTranscription]:
    """
    Get or create global Whisper transcription instance.

    Args:
        model_name: Whisper model size
        device: Device to use
        language: Default language

    Returns:
        WhisperTranscription instance or None if not available
    """
    global _whisper_transcription

    if not WHISPER_AVAILABLE:
        return None

    if _whisper_transcription is None:
        _whisper_transcription = WhisperTranscription(
            model_name=model_name,
            device=device,
            language=language,
        )

    return _whisper_transcription


async def transcribe_voice_message(
    audio_path: Union[str, Path],
    language: Optional[str] = None,
    model_name: str = "base",
) -> Optional[str]:
    """
    Convenience function to transcribe a voice message.

    Args:
        audio_path: Path to audio file
        language: Language code (auto-detect if None)
        model_name: Whisper model to use

    Returns:
        Transcribed text or None if unavailable
    """
    transcriber = await get_whisper_transcription(model_name=model_name)

    if transcriber is None:
        return None

    result = await transcriber.transcribe_telegram_voice(audio_path, language)
    return result.get("text")
