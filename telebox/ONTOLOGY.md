# TeleBox plugin surface — ontology & use cases (the foundation)

Every panel MUST be derived from this. No element exists unless a use case needs it.

## The 5 entity kinds (they recur in every plugin)

| Kind | What it is | Where it lives in the UI | State rule |
|---|---|---|---|
| **Service** | the plugin as a capability | header only (power control) | on/off, running/stopped |
| **Item** | a thing the user picks & acts on (a chat, a bot, a tool, a message) | a findable list — **search when there are many** | selected / not |
| **Job** | a time-bounded operation (an export, an archive pass, a transcription) | a progress region that **only exists while it runs** | none → running → done/failed. NEVER shown when none. |
| **Store** | accumulated results ("what I already have") | a stats/browser region | grows over time |
| **Setting** | a persistent config toggle | a switch | on/off, persisted |

Two failures this kills: (1) mashing Service + Job + Item counts into one "STATE" row; (2) showing a Job's fields (chat, progress) when no Job exists.

## Per plugin: purpose → use cases → entities → therefore the UI

### MCP — the aggregated endpoint
- **Purpose:** the one socket that exposes every tool to agents/clients.
- **Use cases:** UC1 is it up & who's connected? · UC2 browse what it can do · UC3 try a tool.
- **Entities:** Service (endpoint on/off) · Store (the 362-tool catalog → domain tree) · Job (live request stream) · connected clients.
- **UI:** endpoint health + clients/throughput (top) · the tool **tree** (14 domains → 58 subdomains → 362 tools, searchable, count badges) · invoke a selected tool.

### Export — a chat → disk (headless gradual engine)
- **Purpose:** save one specific chat's full history to disk, without the client's export window.
- **Use cases:** UC1 export chat X · UC2 watch / cancel the run · UC3 (later) resume / see past exports.
- **Entities:** Service · **Item** (chat — 727, needs search) · **Job** (the export: target/progress/state) · Store (past exports — later).
- **UI:** two states. **IDLE** = search → pick a chat → Export (action on top). **RUNNING** = target · progress · Cancel (the job *replaces* the picker). No phantom when idle.

### Archiver — any chat → local database
- **Purpose:** keep a permanent, searchable local copy of any chat.
- **Use cases:** UC1 archive chat X to the local DB · UC2 see what the archive holds · UC3 modes: sweep deleted accounts / mirror to a group.
- **Entities:** Service (on + running) · **Item** (chat → search) · **Job** (an archive pass) · **Store** (the DB: messages/chats/size).
- **UI:** same shape as Export — IDLE search→pick→Archive; Store stats = "what I have"; modes secondary. (Ephemeral capture is NOT here — that's Retention.)

### Retention — git-style history + ephemeral capture
- **Purpose:** never lose a message — preserve every edit & deletion, capture ephemeral media.
- **Use cases:** UC1 see a message's full edit/delete chain · UC2 turn capture on for self-destruct / view-once / vanishing · UC3 browse what's tracked.
- **Entities:** Service · **Settings** (3 capture toggles) · **Store** (message_versions) · Item (a tracked message → its chain).
- **UI:** 3 capture switches · tracked-message browser · version chain. (Already coherent.)

### Bots — automations
- **Purpose:** run rule-based bot automations.
- **Use cases:** UC1 see my bots & state · UC2 start/stop a bot · UC3 configure a bot's rules · UC4 see a bot's activity.
- **Entities:** Service · **Item** (a bot: running/stopped) · **Settings** (a bot's rules) · Store (activity log).
- **UI:** bot list (item + state) → select → start/stop · config · activity.

### Wallet — Stars / TON
- **Purpose:** see balance & history. Spending is disabled by design.
- **Use cases:** UC1 what's my Stars balance · UC2 transaction history.
- **Entities:** Service · **Store** (balance + transactions). No Job (read-only).
- **UI:** balance + transactions. Read-only; spend stays dark.

### AI — local LLM / TTS / voice
- **Purpose:** local model features — transcription and speech.
- **Use cases:** UC1 transcribe a voice message · UC2 speak text (TTS) · UC3 is the engine ready.
- **Entities:** Service (engine ready/offline) · Item (a voice message → transcribe) · **Job** (a transcription/synthesis) · Settings (voice/model).
- **UI:** engine status · actions (transcribe a target / speak text) shown as jobs with results.

## The recurring panel shape (so every plugin reads the same way)
`Header (Service on/off)` → `Store: "what I have"` → `Do: find an Item → act → watch the Job`.
Export is the first panel rebuilt strictly to this model; the rest follow.
