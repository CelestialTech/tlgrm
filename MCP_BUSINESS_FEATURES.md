# MCP Business Feature Implementation Analysis

This document analyzes all Telegram Business features and determines which can be implemented via MCP for non-business users.

**Analysis Date:** 2025-11-28

---

## Summary

| Category | Count | Features |
|----------|-------|----------|
| **CAN IMPLEMENT** | 6 | Full equivalent functionality via MCP |
| **PARTIAL** | 2 | Core functionality possible, minor limitations |
| **BEYOND BUSINESS** | 2 | AI TTS Voice + AI TTV Video (MCP exclusive!) |
| **CANNOT IMPLEMENT** | 0 | None! All features have MCP solutions |

**Total New Tools:** 36 (26 Business equivalents + 10 AI voice/video)

---

## Telegram Business Features (8 total)

1. Location
2. Opening Hours
3. Quick Replies
4. Greeting Messages
5. Away Messages
6. Chatbots
7. Custom Intro (Start Page)
8. Links to Chat

---

## CAN IMPLEMENT via MCP (4 features)

### 1. Quick Replies
**Business:** Set up shortcuts with rich text and media to respond to messages faster.

**MCP Implementation:**
- Store templates locally with shortcuts
- Support rich text, media attachments, formatting
- Trigger via shortcut name or command
- Sync across devices via local database export

```
Tool: create_quick_reply
Args: {
  "shortcut": "hello",
  "message": "Hi! Thanks for reaching out. How can I help you today?",
  "media_path": "/path/to/image.jpg",  // optional
  "formatting": "markdown"
}

Tool: list_quick_replies
Args: {}
Returns: { "replies": [{ "shortcut": string, "message": string, "media": bool }] }

Tool: send_quick_reply
Args: { "chat_id": int, "shortcut": "hello" }
// Sends the pre-configured message

Tool: delete_quick_reply
Args: { "shortcut": "hello" }

Tool: edit_quick_reply
Args: { "shortcut": "hello", "new_message": string }
```

**Advantage over Business:** Works in groups too (not just 1-on-1), unlimited templates

---

### 2. Greeting Messages
**Business:** Create greetings that will be automatically sent to new customers.

**MCP Implementation:**
- Detect first message from new user (no prior conversation)
- Detect messages after configurable inactivity period (e.g., 7 days)
- Auto-send greeting message
- Configure recipient filters (contacts/non-contacts/all)

```
Tool: configure_greeting
Args: {
  "enabled": true,
  "message": "Hi! Thanks for reaching out. I'll get back to you shortly.",
  "inactivity_days": 7,  // Send if no messages in X days
  "recipients": "non_contacts",  // "all", "contacts", "non_contacts", "selected"
  "selected_chats": [int],  // If recipients = "selected"
  "excluded_chats": [int],
  "schedule": {  // Optional: only greet during certain hours
    "start": "09:00",
    "end": "18:00",
    "timezone": "America/New_York"
  }
}

Tool: get_greeting_config
Args: {}
Returns: { current greeting configuration }

Tool: test_greeting
Args: { "chat_id": int }
// Sends greeting to test it

Tool: get_greeting_stats
Args: {}
Returns: {
  "total_sent": int,
  "last_sent": timestamp,
  "recipients": [{ "chat_id": int, "sent_at": timestamp }]
}
```

**How it works:**
1. MCP monitors incoming messages via event hook
2. Checks if sender qualifies (new/inactive)
3. Sends greeting message automatically
4. Logs to avoid duplicate greetings

**Advantage over Business:** More flexible rules, works in groups, AI-customizable greetings

---

### 3. Away Messages
**Business:** Define messages that are automatically sent when you are off.

**MCP Implementation:**
- Multiple schedule modes: always, outside hours, custom schedule
- Offline detection (no activity for X minutes)
- Recipient filtering
- Different messages for different contexts

```
Tool: configure_away_message
Args: {
  "enabled": true,
  "message": "I'm currently away. I'll respond when I'm back!",
  "schedule_mode": "outside_hours",  // "always", "outside_hours", "custom", "offline_only"
  "business_hours": {
    "monday": [{ "open": "09:00", "close": "17:00" }],
    "tuesday": [{ "open": "09:00", "close": "17:00" }],
    "wednesday": [{ "open": "09:00", "close": "17:00" }],
    "thursday": [{ "open": "09:00", "close": "17:00" }],
    "friday": [{ "open": "09:00", "close": "17:00" }],
    "saturday": [],  // Closed
    "sunday": []
  },
  "timezone": "America/New_York",
  "custom_schedule": {  // If schedule_mode = "custom"
    "start": "2024-12-25T00:00:00",
    "end": "2024-12-26T23:59:59"
  },
  "offline_minutes": 30,  // For "offline_only" mode
  "recipients": "all",
  "excluded_chats": [int],
  "only_once_per_day": true  // Don't spam same person
}

Tool: get_away_config
Args: {}

Tool: set_away_now
Args: { "until": "2024-12-01T09:00:00" }
// Immediately enable away mode until specified time

Tool: disable_away
Args: {}

Tool: get_away_stats
Args: {}
Returns: {
  "total_sent": int,
  "currently_active": bool,
  "next_activation": timestamp,
  "recipients_today": [{ "chat_id": int, "sent_at": timestamp }]
}
```

**How it works:**
1. MCP runs background schedule checker
2. When away mode active, monitors incoming messages
3. Sends away message (once per sender per day)
4. Tracks sent messages to avoid spam

**Advantage over Business:**
- Multiple away messages for different schedules
- Vacation mode with date ranges
- AI-personalized responses based on message content

---

### 4. Opening Hours (Local Tracking)
**Business:** Show to your customers when you are open for business.

**MCP Implementation:**
- Store business hours locally
- Use for away message logic
- Provide hours info via MCP tool (for bots/integrations)
- Include in auto-responses

```
Tool: set_business_hours
Args: {
  "timezone": "America/New_York",
  "hours": {
    "monday": [{ "open": "09:00", "close": "12:00" }, { "open": "13:00", "close": "17:00" }],
    "tuesday": [{ "open": "09:00", "close": "17:00" }],
    "wednesday": [{ "open": "09:00", "close": "17:00" }],
    "thursday": [{ "open": "09:00", "close": "17:00" }],
    "friday": [{ "open": "09:00", "close": "15:00" }],
    "saturday": "closed",
    "sunday": "closed"
  },
  "special_dates": {
    "2024-12-25": "closed",
    "2024-12-31": [{ "open": "09:00", "close": "12:00" }]
  }
}

Tool: get_business_hours
Args: {}
Returns: { full hours configuration }

Tool: is_open_now
Args: {}
Returns: {
  "is_open": bool,
  "current_day": "monday",
  "current_time": "14:30",
  "next_open": timestamp,  // If currently closed
  "closes_at": timestamp   // If currently open
}

Tool: get_hours_message
Args: {}
Returns: {
  "formatted": "Mon-Thu: 9AM-5PM\nFri: 9AM-3PM\nSat-Sun: Closed"
}
```

**Note:** Hours are stored locally - can be included in away messages or greeting messages automatically.

---

## PARTIALLY IMPLEMENTABLE (2 features)

### 5. Location (Local Storage Only)
**Business:** Display the location of your business on your account.

**MCP Can Do:**
- Store business location locally
- Include in auto-responses
- Provide to other tools/bots
- Generate map links for sharing

**MCP Cannot Do:**
- Display on your Telegram profile (server feature)
- Show to users who view your profile

```
Tool: set_business_location
Args: {
  "address": "123 Main St, New York, NY 10001",
  "coordinates": { "lat": 40.7128, "lng": -74.0060 },
  "name": "My Business HQ",
  "google_maps_link": "https://maps.google.com/?q=..."
}

Tool: get_business_location
Args: {}

Tool: get_location_message
Args: { "format": "text" }  // or "link", "both"
Returns: {
  "text": "📍 123 Main St, New York, NY 10001",
  "link": "https://maps.google.com/?q=40.7128,-74.0060"
}

Tool: send_location
Args: { "chat_id": int }
// Sends location as a Telegram location message
```

**Use case:** Include location in greeting/away messages, or send on request.

---

### 6. Links to Chat (Local Tracking)
**Business:** Create links that start a chat with you, suggesting the first message.

**MCP Can Do:**
- Generate `t.me/username?start=...` deep links
- Track which links were clicked (by message content)
- Create multiple tracked links with different identifiers

**MCP Cannot Do:**
- Create official business chat links with analytics
- See server-side click statistics

```
Tool: create_chat_link
Args: {
  "name": "website_contact",
  "suggested_message": "Hi, I found you via your website!",
  "description": "Link for website contact form"
}
Returns: {
  "link": "https://t.me/yourusername?text=Hi%2C%20I%20found%20you%20via%20your%20website%21",
  "tracking_id": "website_contact"
}

Tool: list_chat_links
Args: {}
Returns: { "links": [...] }

Tool: get_link_stats
Args: { "name": "website_contact" }
Returns: {
  "clicks_detected": 15,  // Based on matching messages received
  "last_click": timestamp,
  "conversations_started": 12
}
```

**How tracking works:** MCP monitors incoming messages that match the suggested text pattern.

---

## CAN IMPLEMENT - Local AI Chatbots ✅

### 5. Chatbots (via pythonMCP + AI)
**Business:** Add any third party chatbots that will process customer interactions.

**MCP Implementation - BETTER than Business:**
- Local AI processing via pythonMCP bridge
- Support ANY AI backend (Claude, GPT-4, local LLaMA, Mistral, custom)
- Full conversation history context
- Custom routing rules per chat type
- Offline capable with local models
- No Telegram server rate limits

```
Architecture:
┌─────────────────┐     ┌──────────────┐     ┌─────────────────┐
│ Telegram MCP    │────▶│ pythonMCP    │────▶│ AI Backend      │
│ (message event) │     │ (IPC bridge) │     │ - Claude API    │
│                 │◀────│              │◀────│ - OpenAI API    │
│ (send_message)  │     │              │     │ - Local LLaMA   │
└─────────────────┘     └──────────────┘     │ - Custom logic  │
                                             └─────────────────┘

Tool: configure_ai_chatbot
Args: {
  "enabled": true,
  "backend": "pythonMCP",  // "claude", "openai", "local_llama", "custom"
  "model": "claude-3-sonnet",  // or "gpt-4", "llama-3", etc.
  "chats": "selected",  // "all", "non_contacts", "selected"
  "selected_chats": [int],
  "excluded_chats": [int],
  "system_prompt": "You are a helpful customer service agent for Acme Corp...",
  "auto_reply": true,
  "reply_delay_seconds": 2,  // Simulate typing
  "human_handoff_keywords": ["speak to human", "real person", "manager"],
  "max_context_messages": 20,
  "business_hours_only": true
}

Tool: get_chatbot_config
Args: {}

Tool: pause_chatbot
Args: { "chat_id": int }  // Pause for specific chat (human handoff)

Tool: resume_chatbot
Args: { "chat_id": int }

Tool: set_chatbot_prompt
Args: { "system_prompt": string, "chat_id": int }  // Per-chat custom prompts

Tool: get_chatbot_stats
Args: {}
Returns: {
  "total_conversations": int,
  "messages_handled": int,
  "human_handoffs": int,
  "avg_response_time_ms": int,
  "satisfaction_signals": { "positive": int, "negative": int }
}

Tool: train_chatbot
Args: { "examples": [{ "user": string, "assistant": string }] }
// Add few-shot examples for better responses
```

**Advantages over Telegram Business chatbots:**
| Telegram Business | MCP AI Chatbot |
|-------------------|----------------|
| Limited to specific bots | Any AI model |
| Server-side rate limits | No limits |
| Bot sees single message | Full conversation context |
| Fixed bot behavior | Customizable prompts |
| Requires internet | Can work offline (local LLM) |
| Monthly fee | Free (or your API costs) |

---

## PARTIAL - Custom Intro via Instant Greeting

### 6. Custom Intro (Start Page)
**Business:** Customize the message people see before they start a chat with you.

**Technical Limitation:**
- Custom Intro displays in OTHER users' Telegram app
- Shows in empty chat state BEFORE they message
- We cannot control other users' client display

**MCP Workaround - Instant Greeting:**
The Greeting Message feature achieves 90% of the same goal:

| Custom Intro (Business) | Instant Greeting (MCP) |
|------------------------|------------------------|
| Shows BEFORE first message | Sends AFTER first message (instant) |
| Static title + message + sticker | Dynamic, personalized message |
| Visible in empty chat | Appears as first reply |
| Cannot include media | Can include any media |

```
Tool: configure_instant_intro
Args: {
  "enabled": true,
  "title": "Welcome to Acme Support!",
  "message": "Hi! I'm here to help. What can I assist you with today?",
  "sticker_id": "CAACAgIAAxkBAAI...",  // Optional sticker
  "response_delay_ms": 500,  // Near-instant but not suspicious
  "only_first_contact": true
}
```

**Result:** User messages you → instantly receives your "intro" as first reply.
Functionally equivalent, just 0.5 seconds later.

---

## Revised unique_business List

**NONE** - All features can be implemented or have functional equivalents!

| Feature | MCP Solution |
|---------|-------------|
| ~~Chatbots~~ | ✅ Local AI via pythonMCP - BETTER |
| ~~Custom Intro~~ | ✅ Instant Greeting workaround |

---

## BEYOND BUSINESS - AI-Powered Features

These features don't exist in Telegram Premium OR Business - they're MCP exclusives!

### 7. AI Voice Messages (TTS)
**Generate voice messages from text using AI Text-to-Speech**

**Capabilities:**
- Clone YOUR voice from samples (speak as yourself without recording)
- Use celebrity/character voices (ElevenLabs, etc.)
- Multi-language voice responses
- Emotional tone control (happy, serious, excited)
- Local TTS for privacy (Coqui, Piper)

```
Architecture:
┌─────────────────┐     ┌──────────────┐     ┌─────────────────┐
│ MCP Server      │────▶│ pythonMCP    │────▶│ TTS Backend     │
│ text message    │     │ (bridge)     │     │ - ElevenLabs    │
│                 │◀────│              │◀────│ - OpenAI TTS    │
│ voice .ogg file │     │ .ogg audio   │     │ - Coqui (local) │
└─────────────────┘     └──────────────┘     │ - Azure TTS     │
                                             └─────────────────┘

Tool: configure_voice_persona
Args: {
  "backend": "elevenlabs",  // "openai", "coqui_local", "azure"
  "voice_id": "your_cloned_voice",
  "model": "eleven_multilingual_v2",
  "settings": {
    "stability": 0.5,
    "similarity_boost": 0.75,
    "style": 0.5
  }
}

Tool: generate_voice_message
Args: {
  "text": "Thanks for your message! I'll get back to you soon.",
  "voice_id": "your_voice",  // Optional override
  "language": "en",
  "emotion": "friendly"  // "neutral", "excited", "serious"
}
Returns: {
  "audio_path": "/tmp/voice_abc123.ogg",
  "duration_seconds": 4.2
}

Tool: send_voice_reply
Args: {
  "chat_id": int,
  "text": "Your order has been shipped!",
  "auto_translate": true,  // Speak in recipient's language
  "reply_to_message_id": int
}

Tool: clone_voice
Args: {
  "name": "my_voice",
  "sample_files": ["/path/to/sample1.mp3", "/path/to/sample2.mp3"],
  "description": "My natural speaking voice"
}
Returns: { "voice_id": "cloned_abc123", "quality_score": 0.92 }
```

**Use Cases:**
- Voice greetings without recording each time
- Multi-language support (your voice, any language)
- Accessibility (voice replies for users who prefer audio)
- Personalized auto-replies that sound human
- Scale 1:1 communication with AI voice

---

### 8. AI Video Circles (TTV)
**Generate Telegram video circles from text using AI avatars**

**Capabilities:**
- AI avatar speaks your message (HeyGen, D-ID, Synthesia)
- Use your photo to create talking avatar
- Real-time lip sync with generated audio
- Different avatar styles (professional, casual, animated)
- Local generation with open-source models (SadTalker, Wav2Lip)

```
Architecture:
┌─────────────────┐     ┌──────────────┐     ┌─────────────────┐
│ MCP Server      │────▶│ pythonMCP    │────▶│ Avatar Backend  │
│ text + config   │     │ (bridge)     │     │ - HeyGen API    │
│                 │◀────│              │◀────│ - D-ID API      │
│ video circle    │     │ .mp4 video   │     │ - SadTalker     │
└─────────────────┘     └──────────────┘     │ - Wav2Lip       │
        │                                    └─────────────────┘
        ▼
  Telegram API
  (send video note)

Tool: configure_video_avatar
Args: {
  "backend": "heygen",  // "d-id", "sadtalker_local", "wav2lip_local"
  "avatar_id": "your_avatar",
  "source_type": "photo",  // "video", "3d_model"
  "source_path": "/path/to/your_photo.jpg",
  "voice_id": "your_cloned_voice",  // Links to TTS config
  "style": "professional"  // "casual", "animated", "realistic"
}

Tool: generate_video_circle
Args: {
  "text": "Hey! Thanks for reaching out. Let me help you with that.",
  "avatar_id": "your_avatar",
  "duration_max": 60,  // Telegram limit
  "background": "blur",  // "office", "none", "custom"
  "emotion": "friendly"
}
Returns: {
  "video_path": "/tmp/video_circle_abc123.mp4",
  "duration_seconds": 8.5,
  "thumbnail_path": "/tmp/thumb_abc123.jpg"
}

Tool: send_video_reply
Args: {
  "chat_id": int,
  "text": "Your question about pricing - here's a quick explanation...",
  "avatar_id": "your_avatar",
  "reply_to_message_id": int
}

Tool: upload_avatar_source
Args: {
  "name": "my_avatar",
  "photo_path": "/path/to/professional_headshot.jpg",
  "train_expressions": true  // Learn your expressions from video samples
}
Returns: { "avatar_id": "avatar_abc123", "ready": true }
```

**Use Cases:**
- Personal video greetings at scale (impossible manually)
- Customer support with face presence
- Multi-language video responses (same avatar, any language)
- Consistent brand presence
- "Be in two places at once" - AI handles video while you focus elsewhere

---

### Combined AI Pipeline

The real power comes from combining all AI features:

```
Incoming Message
       │
       ▼
┌─────────────────────────────────────────────────┐
│               AI CHATBOT (LLM)                  │
│  - Understands context                          │
│  - Generates response text                      │
│  - Decides response format (text/voice/video)  │
└─────────────────────────────────────────────────┘
       │
       ├──────────────┬──────────────┬────────────┐
       ▼              ▼              ▼            ▼
   Text Reply    Voice Reply    Video Reply   Action
   (instant)     (TTS + send)   (TTV + send)  (tool call)
```

**Example Flow:**
1. Customer sends: "Can you explain your return policy?"
2. AI Chatbot generates response text
3. Based on config, creates VIDEO CIRCLE of you explaining
4. Sends personalized video response in <10 seconds
5. Customer feels like they got personal attention

---

## Feature Comparison: Telegram vs MCP

| Capability | Telegram Premium | Telegram Business | MCP + AI |
|------------|------------------|-------------------|----------|
| Text auto-reply | ❌ | ✅ Basic | ✅ AI-powered |
| Voice transcription | ✅ | ✅ | ✅ Local (better privacy) |
| Voice generation | ❌ | ❌ | ✅ **AI TTS** |
| Video circles | Manual only | Manual only | ✅ **AI Generated** |
| Multi-language | ❌ | ❌ | ✅ Any language |
| Voice cloning | ❌ | ❌ | ✅ Your voice |
| Avatar video | ❌ | ❌ | ✅ Your face |
| 24/7 presence | ❌ | ✅ Basic bot | ✅ Full AI persona |

---

## Proposed MCP Tools Summary

### New Tools to Add (36 tools)

**Quick Replies (5 tools):**
1. `create_quick_reply` - Create new template
2. `list_quick_replies` - List all templates
3. `send_quick_reply` - Send by shortcut
4. `edit_quick_reply` - Modify template
5. `delete_quick_reply` - Remove template

**Greeting Messages (4 tools):**
6. `configure_greeting` - Set up auto-greeting
7. `get_greeting_config` - Get current config
8. `test_greeting` - Test greeting message
9. `get_greeting_stats` - View greeting statistics

**Away Messages (5 tools):**
10. `configure_away_message` - Set up away auto-reply
11. `get_away_config` - Get current config
12. `set_away_now` - Enable away immediately
13. `disable_away` - Turn off away mode
14. `get_away_stats` - View away statistics

**Business Hours (3 tools):**
15. `set_business_hours` - Configure schedule
16. `get_business_hours` - Get schedule
17. `is_open_now` - Check current status

**Location (1 tool):**
18. `set_business_location` - Store location locally

**AI Chatbot (7 tools):**
19. `configure_ai_chatbot` - Set up AI auto-responder
20. `get_chatbot_config` - Get current config
21. `pause_chatbot` - Pause for specific chat (human handoff)
22. `resume_chatbot` - Resume AI responses
23. `set_chatbot_prompt` - Update system prompt
24. `get_chatbot_stats` - View chatbot statistics
25. `train_chatbot` - Add few-shot examples

**Custom Intro (1 tool):**
26. `configure_instant_intro` - Set up instant greeting on first contact

**AI Voice Messages - TTS (5 tools):**
27. `configure_voice_persona` - Set up AI voice (clone your voice or select preset)
28. `generate_voice_message` - Convert text to voice message
29. `send_voice_reply` - Auto-reply with AI-generated voice
30. `list_voice_presets` - Available TTS voices/models
31. `clone_voice` - Create custom voice from samples

**AI Video Circles - TTV (5 tools):**
32. `configure_video_avatar` - Set up AI video avatar
33. `generate_video_circle` - Create video circle from text
34. `send_video_reply` - Auto-reply with AI-generated video circle
35. `upload_avatar_source` - Upload photo/video for avatar generation
36. `list_avatar_presets` - Available avatar styles

---

## Implementation Architecture

### Background Service Required

Unlike other MCP tools, Business features need a **background monitoring service**:

```
MCP Business Service
├── Message Monitor (watches for incoming messages)
│   ├── Greeting Trigger (new/inactive sender detection)
│   └── Away Trigger (schedule check + auto-reply)
├── Schedule Engine (cron-like scheduler)
│   ├── Business Hours Tracker
│   └── Custom Schedule Handler
├── Local Database
│   ├── quick_replies table
│   ├── greeting_config table
│   ├── away_config table
│   ├── business_hours table
│   └── sent_messages log (avoid duplicates)
└── Event Hooks
    ├── on_message_received → check greeting/away
    └── on_schedule_tick → update away status
```

### Files to Create

```
Telegram/SourceFiles/mcp/
├── mcp_server.h/.cpp       (existing - add tool declarations)
├── mcp_business.h          (NEW - Business feature manager)
├── mcp_business.cpp
├── mcp_quick_replies.h     (NEW - Quick replies storage)
├── mcp_quick_replies.cpp
├── mcp_auto_messages.h     (NEW - Greeting/Away engine)
├── mcp_auto_messages.cpp
├── mcp_schedule.h          (NEW - Schedule/hours manager)
├── mcp_schedule.cpp
└── mcp_business_db.h       (NEW - SQLite for business data)
    mcp_business_db.cpp
```

---

## Implementation Priority

| Priority | Feature | Effort | Impact |
|----------|---------|--------|--------|
| **P0** | Quick Replies | Low | High - Immediately useful |
| **P0** | Away Messages | Medium | High - Very common need |
| **P1** | Greeting Messages | Medium | Medium - Good for businesses |
| **P1** | Business Hours | Low | Medium - Enables away logic |
| **P2** | Location Storage | Low | Low - Nice to have |
| **P2** | Chat Links | Low | Low - Basic tracking |

---

## Integration with Premium Features

These Business MCP tools can combine with Premium MCP tools:

| Premium Tool | Business Tool | Combined Capability |
|--------------|---------------|---------------------|
| Voice-to-Text | Away Message | Auto-reply with voice transcription |
| Translation | Quick Replies | Multi-language quick replies |
| Message Tags | Greeting Stats | Tag conversations from greetings |
| Chat Rules | Business Hours | Archive after-hours messages |

---

## Database Schema

```sql
-- Quick Replies
CREATE TABLE quick_replies (
  id INTEGER PRIMARY KEY,
  shortcut TEXT UNIQUE NOT NULL,
  message TEXT NOT NULL,
  media_path TEXT,
  formatting TEXT DEFAULT 'plain',
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  use_count INTEGER DEFAULT 0
);

-- Greeting Configuration
CREATE TABLE greeting_config (
  id INTEGER PRIMARY KEY,
  enabled BOOLEAN DEFAULT 0,
  message TEXT,
  inactivity_days INTEGER DEFAULT 7,
  recipients TEXT DEFAULT 'non_contacts',
  excluded_chats TEXT,  -- JSON array
  schedule_start TEXT,
  schedule_end TEXT,
  timezone TEXT DEFAULT 'UTC'
);

-- Away Configuration
CREATE TABLE away_config (
  id INTEGER PRIMARY KEY,
  enabled BOOLEAN DEFAULT 0,
  message TEXT,
  schedule_mode TEXT DEFAULT 'outside_hours',
  offline_minutes INTEGER DEFAULT 30,
  recipients TEXT DEFAULT 'all',
  excluded_chats TEXT,
  only_once_per_day BOOLEAN DEFAULT 1
);

-- Business Hours
CREATE TABLE business_hours (
  id INTEGER PRIMARY KEY,
  day_of_week INTEGER,  -- 0=Monday, 6=Sunday
  open_time TEXT,
  close_time TEXT,
  timezone TEXT DEFAULT 'UTC'
);

-- Special Dates (holidays, etc)
CREATE TABLE special_dates (
  date TEXT PRIMARY KEY,
  status TEXT,  -- 'closed' or JSON hours array
  note TEXT
);

-- Sent Auto-Messages Log
CREATE TABLE auto_message_log (
  id INTEGER PRIMARY KEY,
  chat_id INTEGER,
  message_type TEXT,  -- 'greeting' or 'away'
  sent_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  message_id INTEGER
);

-- Business Location
CREATE TABLE business_location (
  id INTEGER PRIMARY KEY,
  address TEXT,
  lat REAL,
  lng REAL,
  name TEXT,
  maps_link TEXT
);
```

---

## Example Workflow

### User configures Business features via MCP:

```bash
# 1. Set business hours
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{
  "name":"set_business_hours",
  "arguments":{
    "timezone":"America/New_York",
    "hours":{
      "monday":[{"open":"09:00","close":"17:00"}],
      "tuesday":[{"open":"09:00","close":"17:00"}],
      "friday":[{"open":"09:00","close":"15:00"}]
    }
  }
}}' | ./Tlgrm --mcp

# 2. Configure away message
echo '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{
  "name":"configure_away_message",
  "arguments":{
    "enabled":true,
    "message":"Thanks for your message! I am currently away and will respond during business hours.",
    "schedule_mode":"outside_hours"
  }
}}' | ./Tlgrm --mcp

# 3. Create quick replies
echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{
  "name":"create_quick_reply",
  "arguments":{
    "shortcut":"pricing",
    "message":"Our pricing starts at $99/month. Would you like me to send you our full pricing guide?"
  }
}}' | ./Tlgrm --mcp

# 4. Configure greeting
echo '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{
  "name":"configure_greeting",
  "arguments":{
    "enabled":true,
    "message":"Hi! Thanks for reaching out. How can I help you today?",
    "inactivity_days":14
  }
}}' | ./Tlgrm --mcp
```

---

*Generated by Claude Code MCP Analysis*
