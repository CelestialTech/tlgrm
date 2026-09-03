# SELF-GATES — operational guardrails I must run

Concrete, checkable rules to stop repeating failures. Each has a runnable CHECK
and the EXPECT it must satisfy. Run the relevant gate at the moment named; never
declare a step done while its gate is unmet. Helper: `scratchpad/verify.py`
(`mcp <tool> <json>` calls the live client).

## G-AR — no native export auto-resumes (the failure that prompted this file)
- **WHEN:** after EVERY Tlgrm client restart.
- **WHY:** the client's `tryAutoResumeExport()` used to re-summon the NATIVE export
  panel on startup and resume a queued export (a 15,655-msg channel export kept
  restarting on every relaunch, downloading media unbidden). This fork exports
  HEADLESSLY via TeleBox only. The call is now disabled in
  `tdesktop/.../mcp/mcp_server_complete.cpp` (session-connect). Keep it disabled.
- **CHECK:** `python3 verify.py mcp get_export_status '{}'`
- **EXPECT:** `state` is `idle` (NOT `in_progress`). If `in_progress`: a native
  export auto-started — kill the client, confirm the disable is intact, rebuild.
- **RULE:** a crash-fix is NOT a behavior-fix. If a startup side effect is unwanted,
  remove the side effect, not just its crash.

## G-DEPLOY — the running client is actually the binary I just built
- **WHEN:** after any `ninja Telegram` rebuild that must take effect live.
- **WHY:** `pkill` did not always cycle the client; I probed the OLD binary and got
  a stale/wrong result (list_archived_chats read 0 from an un-rebuilt client).
- **CHECK:** kill by explicit PID (`kill -9 <pid>`), confirm `pgrep` is empty, relaunch,
  then probe a NEW behavior the build introduced.
- **EXPECT:** `pgrep` shows a NEW pid AND the new behavior is live (e.g. a new tool in
  `tools/list`, or the fixed value). Never trust `pkill` alone.

## G-HEAVY — never run a heavy export/archive as a test
- **WHEN:** before any export/archive/download on a real chat.
- **WHY:** [[feedback_no_heavy_export_test]] — Ольга ~38k, Colonelcassad ~220k,
  Технотренды ~15k messages. Media download compounds it.
- **CHECK:** the target's message count (get_chat_history `count`) and a cap.
- **EXPECT:** test on a tiny chat OR pass a small `limit`/`max`. Never uncapped on a
  large chat.

## G-BRIDGE — client bridge calls are serialized + paced
- **WHEN:** touching TeleBox↔client I/O.
- **WHY:** the single-call bridge drops concurrent connections; the gate + 150ms
  throttle in `telebox/src/retention.rs` fix it. Paced usage is clean; sub-second
  bursts still show residual misses recovered by retry.
- **CHECK:** a paced sequence of read invokes.
- **EXPECT:** all succeed first-try at realistic cadence.
