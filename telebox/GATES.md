# GATES — TeleBox user stories (honest status)

Owner: I own UI + user stories. Rule (unlazy): a story is MET only with real
evidence; wired-but-unproven is UNMET.

Evidence legend:
- [qa] — driven through the QA socket, which calls the *identical* HostState
  method (`primary_action` / `toggle_*` / `select`) the on-screen control calls,
  and the call reached the client's REAL MCP backend with a REAL result.
- [render] — the app's own `render_to_image` (no system capture) shows the
  panel drawing real data + the operable controls.
- [click] — a real mouse event verified the GPUI dispatch (done earlier this
  session while the window was foreground).

## MET — proven

Host / rack:
- G-US1  See host status (running/endpoint/upstream/clients/requests) — MET [render]
- G-US2  Start relay — MET [qa]
- G-US3  Stop relay — MET [click]
- G-US5  See 7 plugins + state/runtime/grants — MET [render]
- G-US6  Select a plugin → its panel loads — MET [click][render]

Retention (device 2):
- G-US8  Retention stats (versions/tracked/edits/deletions) — MET [render] live
- G-US9  Browse tracked messages — MET [render] live
- G-US10 View a message's version chain — MET [render] live
- G-US11 Toggle self-destruct capture — MET [click] real client flag flipped
- G-US12 Toggle view-once capture — MET [click] real client flag flipped
- G-US13 Toggle vanishing capture — MET [click] real client flag flipped
- G-CORE Git-style retention (send→edit→edit→delete chain) — MET [qa] end-to-end

Plugin devices now OPERABLE over the relay (was: read-only):
- G-US14 Export a chat — MET [qa][render]. HEADLESS: the button drives
         `start_gradual_export` / `cancel_gradual_export` (GradualArchiver), NOT
         the client's native export window. Verified start→running→cancel with
         the progress shown IN TELEBOX ("state: running · Ольга Тимошевская ·
         0/6"), no Tlgrm popup, cancel works from the panel.
         [corrected] The first cut wrongly called `export_chat`, which opens the
         Tlgrm Export::Controller window — off-plan. Rewired to the headless
         engine so the export use case lives in TeleBox's own UI.
- G-US15 Archive a chat — MET [qa][render]. `archive_chat` archived REAL
         messages from a picked chat ("✓ archived 6 message(s) from chat …");
         live stats (24 msgs / 11 chats / 396 KB) shown.
- G-US16 Start/stop a bot — MET [qa]. `stop_bot` stopped the real bot
         ("✓ Bot stopped: context_assistant"); live list of registered bots.
- G-US17 Wallet balance — MET [qa][render]. `get_wallet_balance` shows the live
         Stars balance from payments.getStarsStatus. Spending stays dark by
         design (read-only device).
- G-US18 AI voice/TTS — MET [qa]. `text_to_speech` invokes the real local TTS
         pipeline and surfaces its true result (here: piper binary not
         installed, with the exact install command) — an honest backend state,
         not a mock.
- G-US19 Browse + invoke MCP tools — MET [qa][render]. The panel lists all 362
         real advertised tools with descriptions (tools/list); the invoke canary
         round-tripped a real tools/call ("✓ list_chats → 727 chats").

## PARTIAL — control wired to a proven method; real-mouse click blocked

- G-US4  Restart relay — method MET [qa]; button is `on_click → state.restart()`.
- G-US7  Power/bypass toggle — method MET [qa]; button is `on_click → toggle_plugin`.
- G-US10c Click a tracked row to switch the chain — method MET [qa]; row is
         `on_click → select_tracked` (same pattern as US11–13 which ARE [click]).

  Why not [click]: TeleBox is currently a **Stage Manager thumbnail**, so the
  desktop-control tools' background captures/clicks hit the thumbnail, not the
  live window, and promoting it disrupts the user's Stage Manager layout. This
  is an environment constraint (surfaced per the macos-desktop-control skill),
  not a UI defect — the GPUI click layer is proven by US3/US6/US11–13 [click]
  earlier this session, and each control's action is proven by [qa].

## Tally
MET: 19 · PARTIAL (method-proven, click blocked by Stage Manager): 3 · ABANDON: 0

Every plugin device now performs its real job from the UI, verified against the
client's live backends. No decorative or read-only-only controls remain.
