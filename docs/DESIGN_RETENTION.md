# Design: git-style message retention (ChatArchiver)

## Decision date
2026-09-02

## What we're building
The Retention feature's original goal, finally realized: when someone **edits** or
**deletes** a message, the fork keeps an immutable, git-style trail instead of
letting the change erase the past. Today `ChatArchiver::onMessageEdited` does
`UPDATE messages SET content=…` (it *clobbers* the prior text) and
`EphemeralArchiver::onMessageDeleted` is an empty stub — so the anti-tamper intent
was scaffolded but not built. We add an append-only version chain, wire it to the
live edit/delete signals, and expose it as an MCP read tool. Ephemeral (view-once)
capture, which already works, stays as-is.

## Ponytail gate
- Rung 2 (already here?): the archiver, its SQLite `_db`, and the edit/delete
  dispatch scaffolding exist — but no version store. **Reuse** `_db`; add nothing new.
- Rung 5: no new dependency (SQLite via the archiver's existing `QSqlDatabase`).
- Rung 7: the version store is a justified, non-trivial new abstraction → design it.

## Design A — dedicated append-only `message_versions` table + a small history API
- **`message_versions(chat_id, message_id, version, kind, content, edit_date, captured_at)`**,
  `UNIQUE(chat_id,message_id,version)` — the immutable log. `kind ∈ {created, edited, deleted}`.
- `recordEdit(HistoryItem*)` — seed a `created` baseline from the archive's current
  snapshot if this message has no history yet, then append `edited` with the new text.
- `recordDeletion(chatId, msgId, lastContent)` — seed baseline, append a `deleted`
  version preserving the content captured *at* deletion (from `itemRemoved`, which
  fires while the item is still readable). Never removes a row.
- `messageHistory(chatId, msgId) → QJsonArray` — the chain oldest-first.
- The `messages` table stays the *current* snapshot; `message_versions` is the *history*.

### Interface
```cpp
void      recordEdit(HistoryItem *message);
void      recordDeletion(qint64 chatId, qint64 messageId, const QString &lastContent);
QJsonArray messageHistory(qint64 chatId, qint64 messageId);
// private: int recordVersion(...); void ensureBaseline(...); bool ensureRetentionSchema();
```
### Depth score: 10/10
interface-simplicity 2 · impl-power 2 · info-hiding 2 · error-absorption 2 (no-op when off; seeds a missing original) · generality 2 (any message, any change kind).

## Design B — soft-delete + inline columns on `messages` (`is_deleted`, `edit_log` JSON)
Add `is_deleted`, `deleted_at`, `edit_count`, and an `edit_log` JSON blob column to
`messages`; never DELETE; on edit push the prior text into `edit_log`.
### Depth score: 5/10
The `edit_log` JSON blob is a **generic container** (POSD red flag): every history
read parses the whole blob, versions aren't indexable rows, and current-state and
history are conflated in one table (change amplification). Interface leaks the blob format.

## Chosen design: A
**Why:** a dedicated append-only table is a true git-style chain — every version is
an indexed, queryable row; the store is fully hidden behind three calls; and seeding
the baseline from the edit/delete event means no history is lost even for messages
first seen at their edit. B's JSON-blob-in-a-column conflates current with history and
re-parses everything on read.

**Tradeoffs accepted:** a second table + a join-free second query on read (vs. B's
single table). Worth it for indexable, immutable versions.

**Red flags cleared:** generic-container (B's edit_log) — avoided; information-leakage
(blob format) — avoided; the clobbering `UPDATE` — replaced by append + keep-current.

**Remaining risks:** (1) existing archive DBs already have `messages`, so
`initializeDatabase()` early-returns — the versions table is created by a separate
idempotent `ensureRetentionSchema()` run from `start()`. (2) High edit-rate chats grow
the table; bounded later by the existing `purgeOldMessages` if needed
(`ponytail:` unbounded growth — add a per-message version cap if it ever matters).

## Wiring (WHAT, not WHEN)
`Server::setSession` subscribes, tied to `session->lifetime()`:
- edits → `changes().messageUpdates(Data::MessageUpdate::Flag::Edited)` → `recordEdit(u.item)`
- deletes → `data().itemRemoved()` → `recordDeletion(peerId, msgId, item->originalText().text)`

## Interface revision (TeleBox Retention device)
The current panel advertises only "view-once capture". Revise it to a **message
history / version timeline** surface: a global capture toggle + a version feed
(created→edited→deleted rows with before/after), honest to what the plugin now does.
