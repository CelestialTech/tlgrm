# GATES — Export plugin, ideal usable redesign (unlazy)

User story: "As a power user I want to export the full history of a SPECIFIC chat
to disk — find it fast among hundreds, start it, watch it, cancel it — without the
client's own export window." Ontology: Service (plugin on/off, header only) ·
Chat (item, 727 of them → needs search) · Job/Run (exists ONLY while exporting) ·
no Store yet. The panel has two states: IDLE (pick) and RUNNING (job).

Evidence: [qa] = QA socket asserts the exact state the UI renders (same HostState
path); [render] = the app's own render_to_image shows it; [build] = compiles.

## Gates

- [ ] G1  No phantom job. When no export is running, the panel shows NO chat and NO
         progress — absence reads as absence, not "idle 0/6".
         CHECK: after cancel, QA snapshot of the Export panel has export_mode="idle"
         and no "progress"/"exporting" readout keys.
- [ ] G2  Findable. Typing part of a chat name filters the 727 chats to matches
         (search over the full set, not a 40-row window).
         CHECK: QA sets export_search="ольга" → filtered row count < 727 and every
         shown row's title contains the query (case-insensitive).
- [ ] G3  Action always reachable. The primary Export action sits at the TOP of the
         idle region (not below a long scroll). [render] manual.
- [ ] G4  Mode switch is coherent. Starting an export flips the panel to RUNNING
         (target chat + progress + Cancel); cancelling flips it back to IDLE (picker).
         CHECK: QA start → snapshot export_mode="running" with a target + progress;
         QA cancel → snapshot export_mode="idle".
- [ ] G5  No ontological collision. The plugin's on/off is ONLY in the header; the
         body has no "STATE: operational" service field colliding with job state.
         CHECK: QA snapshot Export readout has no key equal to "STATE".
- [ ] G6  Builds and deploys clean.
         CHECK: cargo build --release exits 0.

Method note: this gate file establishes the pattern for EVERY plugin — model the
ontology (service/item/job/store) and the user story first, gate the observable
usability outcomes, then build. Applies next to Archiver, Bots, Wallet, AI, and the
MCP tree (research done: virtualized tree + sticky headers + fuzzy search + counts).
