# Tlgrm 7.0.7

Previous release: **6.9.6** (3 April 2026).

Tlgrm 6.9.6 was built on Telegram Desktop **6.5.1**. This release rebases onto
Telegram Desktop **7.0.7**, so it carries 26 upstream releases' worth of change
in one step, alongside a substantial repair of Tlgrm's own subsystems.

| | Upstream (6.5.1 → 7.0.7) | Tlgrm's own changes |
|---|---|---|
| Commits | 2,920 | 17 |
| Files changed | 2,043 | 140 |
| Lines | +298,181 / −34,862 | +44,161 / −226 |

---

## Part 1 — What comes from Telegram Desktop

Twenty-six upstream releases: 6.6, 6.6.1–6.6.2, 6.7, 6.7.1–6.7.6, 6.7.8, 6.8,
6.8.1–6.8.2, 6.9, 6.9.1–6.9.3, 7.0, 7.0.1–7.0.7.

### Articles and rich messages — the largest single change

`SourceFiles/iv/` grew by **87,156 lines**, 29% of everything upstream added.
Instant View stopped being a reader and became an editor: messages can now
carry full article content — headings, lists, tables, quotes, embedded media,
collapsible blocks — composed in the client.

Three new dependencies arrived to support it:

- **cmark-gfm** — GitHub-flavoured Markdown parsing
- **MicroTeX** — mathematical formula rendering
- **TooManyCooks** — the coroutine/task library the editor is built on

86 new `lng_article_*` strings accompany it.

### Communities

130 new `lng_community_*` strings, plus `data_community` and `api_communities`.
The largest new *product* surface in the release.

### Polls, rebuilt

An entire new `SourceFiles/poll/` subsystem — polls were previously handled
inline. 72 new `lng_polls_*` strings; options can now carry media and location.

### AI composition

72 new `lng_ai_*` strings. Server-side compose assistance with selectable
tones, and streaming replies that render progressively as they arrive.

### Passkeys

A new `SourceFiles/webauthn/` subsystem with **libfido2** and **libcbor**.
Hardware-backed and platform authenticators for login, replacing password-only
flows. 25 new `lng_passkey_*` strings.

### Translation

A new `lib_translate` module, adding on-device translation on macOS through the
system framework rather than a server round trip.

### Sticker and emoji pack creation

27 new `lng_stickers_*` strings — packs can be created and edited in-client
rather than through @stickers.

### Visual and platform work

- Qt RHI rendering backend with compiled shaders (`Telegram/shaders/`)
- The "Thanos" dissolution effect for deleted messages — compute-shader particles
- 3D premium covers (star, coin, diamond) rendered from `.obj` meshes
- Gradient-reveal animation for progressively appearing text
- Image editor gains drawing, text and blur tools
- Multiple favourite reactions

### Build requirements that changed

| Requirement | Note |
|---|---|
| **Qt 6.2.13 → 6.11.1** on macOS | Nine minor versions in one step |
| **Swift 6 toolchain** | Required by `lib_translate` |
| **`qsb`** (Qt Shader Baker) | Hard failure if absent — `Telegram/shaders/` must compile |
| **python3 at build time** | Model baking, `.obj` → `.binobj` |
| autotools | `autoreconf` needed by a dependency |

---

## Part 2 — What is Tlgrm's own

All 17 fork commits, +44,161 lines, of which `SourceFiles/mcp/` is 42,455.

### The MCP server ported and repaired

The 65-file MCP subsystem was ported to 7.0.7, then audited. The audit found
the tool surface was not what it claimed to be.

**Tools now state what backs them.** A single `constexpr` table records, for
each of 338 tools, whether it queries Telegram, reads live session state,
computes from its arguments, reads a local database nothing syncs, or is
unimplemented. Every response and every `tools/list` entry carries that
`backing` field. Previously nothing distinguished a Telegram query from an
invented answer.

| Backing | Tools |
|---|---|
| MTProto | 74 |
| Live session | 31 |
| Local-only | 229 |
| Pure compute | 4 |
| **Unimplemented** | **0** |

**19 tools that reported success while doing nothing now work.**
`get_stars_rate` returned `0.0` with `success: true` under a comment claiming
the credits API was unavailable — both rates had been in `appConfig` all along,
and that zero silently turned every conversion into zero. `get_topup_options`
and `get_giveaway_options` returned hardcoded catalogues that went stale
whenever Telegram changed pricing; they now return live data.
`set_reaction_price` claimed "Reaction price set locally" while storing nothing
anywhere. `update_profile_username` was declared to need interactive
verification — that had confused it with changing a phone number.

**The registry had drifted.** One tool was advertised twice with conflicting
schemas, five had their handler assigned twice, eight were callable but never
advertised, and `rename_chat_title` was implemented but never registered at
all. A startup check now cross-references the handler map, the advertised list
and the backing table, so drift fails loudly instead of reaching clients.

**Declared schemas are now enforced and honest.** 28 tools ignored their own
`required` lists and answered with nothing supplied. Separately, 61 tools
declared arguments their implementation never read — `set_reaction_price`
advertised `price` while reading `min_stars`, six privacy tools advertised
`option` while reading `rule`. Schema, contract and implementation now agree.

**New tools:** `send_document` (sends a local file as a document, preserving
bytes and filename), `create_channel` and `delete_channel`.

### The auto-update system, repaired

It was dead at **five independent layers**, each failing silently. A shipped
build could never have found an update.

1. The MTProto feed resolved `tlgrmfeed4` — a username nobody owns
2. Launching with `--mcp` silently disabled all update checks
3. `packer.cpp` verified signatures against Telegram's public keys, not ours
4. `Packer` was gated behind a build flag and had never been built
5. The Cloudflare Worker was bound to `updates.tlgrm.app` — a domain we do not own

All fixed. The update channel is `@updates71grm`, serving is on
`updates.71grm.site`, and `tools/publish_update.py` packs and publishes,
refusing to continue unless Packer reports `Signature verified!`.

### The gradual export subsystem

Ported to 7.0.7 (+1,415 lines). Upstream had rewritten the export writers for
rich messages, so the fork's changes were re-applied rather than merged.

### Crashes and silent failures fixed

- **A missing argument crashed the client.** `get_chat_info` with no `chat_id`
  aborted the process — `Session::peer()` is create-or-abort, and the
  `if (!peer)` guards written after it were dead code, since it returns
  `not_null`. 19 call sites shared the pattern.
- **Nested event loops could outlive their objects.** Rapid tool calls killed
  the client twice. The loop now excludes user input, watches the server with a
  `QPointer`, and re-checks the session afterwards.
- **`ChatArchiver` never had a session.** `archive_chat` had always failed.
- **`archiveChat` treated "nothing to archive" as failure.**
- Export dropped `[[nodiscard]]` write results, so a full disk passed as success
- Four batch operations counted successes and never stored them, reporting 0/0

---

## Part 3 — Testing

Three harnesses ship in `tools/`:

- **`mcp_test_suite.py`** — calls every advertised tool, classifying crashes,
  unenforced arguments and unbacked success
- **`mcp_smoke_test.py`** — read-only tools for real, mutating tools on their
  validation paths only
- **`mcp_fixture_test.py`** — creates a disposable channel and supergroup,
  exercises the destructive tools against those alone, and deletes them

The fixture harness enforces its scoping: every call passes a guard that
refuses any argument naming a chat the run did not create.

Current state: **16 of 16 fixture tests passing**, 0 crashes, 0 schema holes,
0 tools reporting success without backing.

---

## Upgrading

**`AppVersion` is 7000007.** The tdata version gate rejects any file whose
embedded version exceeds `AppVersion` and regenerates the local key, which
destroys the session. Migration is one-way: **do not downgrade** to 6.9.6 with
a 7.0.7 profile.

**Behaviour changes for MCP clients:**

- 20 tools that returned `success: true` now return errors. Anything treating
  those responses as real will start failing — which is the point, but it will
  look like a regression until the error text is read.
- Tools now reject calls missing their declared required arguments.
- 39 schemas renamed arguments to match their implementations.
- The IPC socket moved from `/tmp/tdesktop_mcp.sock` to `/tmp/tlgrm_mcp.sock`.

**Build:** `Telegram/build/target` must contain `mac`, or the `Packer` target is
not generated and no update package can be produced. That file is gitignored
upstream, so it has to be created per checkout.

---

## Known issues

- **229 tools are local-only.** They read a database nothing syncs from
  Telegram. They are now labelled rather than silently misleading, but giving
  them real backing is a separate program.
- **36 tool names are pass-through aliases.** Removing them is a breaking change
  and needs a deprecation pass.
- **The gradual export resume path has no tests.** No test writes a partial
  export and restarts it; `resume_state.json` round-tripping is unverified.
- **Intel Macs get no updates.** The build is arm64-only; no `tmacupd` package
  is produced.
- **The channel `@updates71grm` and DNS for `updates.71grm.site` are not yet
  provisioned**, so update delivery is not live.
