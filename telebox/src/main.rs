// TeleBox — the plugin-hosted controller for the Tlgrm client.
//
// Every plugin is a rack DEVICE, not a list row: the rail selects a device and
// the stage loads that device's own operable control surface — a Host-API
// patch-bay (the permission matrix from plugin.rs, made visible) over a control
// panel honest to what the plugin does. The hero is Export: pick the chat, set
// the gradual-export parameters, arm it, watch it run.
//
// Every control is a thin wrapper over a HostState method, so the QA socket
// (qa.rs) drives the identical path — and grabs the rendered window as a PNG via
// the app's own render_to_image, no screen and no system tools needed.

mod host;
mod mcp_relay;
mod plugin;
mod qa;
mod retention;

use std::time::Duration;

use gpui::{App, Bounds, Context, Div, FocusHandle, FontWeight, KeyDownEvent, Window, WindowBounds,
    WindowOptions, div, prelude::*, px, relative, rgb, size};
use gpui_platform::application;

use host::{plugin_active, plugin_templates, HostState, PanelRow, Plugin, FAMILIES};

// Studio-dark instrument palette.
const VOID: u32 = 0x0A0C0F;
const PANEL: u32 = 0x14181F;
const PANEL2: u32 = 0x171C24;
const WELL: u32 = 0x0D1015;
const RAISED: u32 = 0x1B212B;
const LINE: u32 = 0x29313D;
const LINE2: u32 = 0x20262F;
const INK: u32 = 0xE9EEF5;
const INK2: u32 = 0x8B95A4;
const INK3: u32 = 0x59626E;
const MINT: u32 = 0x6EE7C5; // armed / live / primary
const MINT_DK: u32 = 0x244C43;
const VIOLET: u32 = 0xB79BFF; // capability grant
const VIOLET_DK: u32 = 0x2E2A48;
const AMBER: u32 = 0xE0B85A;
const CRIT: u32 = 0xE2685D;
const OK: u32 = 0x7BCF8A;

const ENDPOINT: &str = "/tmp/telebox_host.sock";
const UPSTREAM: &str = "/tmp/tlgrm_mcp.sock";
const TOKEN: &str = "/tmp/auth_token";
const QA_SOCK: &str = "/tmp/telebox_qa.sock";

struct TeleBox {
    state: HostState,
    loop_started: bool,
    // Focus for the Export chat-search field (a real typed input).
    search_focus: FocusHandle,
}

impl TeleBox {
    fn new(cx: &mut Context<Self>) -> Self {
        let state = HostState::new(ENDPOINT.into(), UPSTREAM.into(), TOKEN.into());
        state.log("host", "TeleBox host starting");
        state.start();
        qa::spawn(state.clone(), QA_SOCK.into());
        // Poll the client's real message_versions store for the Retention panel.
        retention::spawn(state.clone(), UPSTREAM.into(), TOKEN.into());

        cx.spawn(async move |this, cx| loop {
            cx.background_executor().timer(Duration::from_millis(400)).await;
            if this.update(cx, |_this, cx| cx.notify()).is_err() {
                break;
            }
        })
        .detach();

        Self { state, loop_started: false, search_focus: cx.focus_handle() }
    }
}

// ---- small stateless chrome helpers -----------------------------------------

fn dot(color: u32, sz: f32) -> Div {
    div().size(px(sz)).rounded_full().bg(rgb(color))
}
fn mono(color: u32) -> Div {
    div().font_family("Menlo").text_color(rgb(color))
}
fn label(t: &'static str) -> Div {
    mono(INK3).text_xs().child(t)
}
fn well(p: f32) -> Div {
    div().bg(rgb(WELL)).rounded_md().border_1().border_color(rgb(LINE2)).p(px(p))
}

// Version-kind → color: created (mint), edited (amber), deleted (red).
fn kind_color(kind: &str) -> u32 {
    match kind {
        "created" => MINT,
        "edited" => AMBER,
        "deleted" => CRIT,
        _ => INK3,
    }
}

fn stat(k: &'static str, v: String, color: u32) -> Div {
    div().flex().flex_col().gap_1().flex_shrink_0()
        .child(mono(INK3).text_xs().child(k))
        .child(mono(color).child(v))
}
fn stat_path(k: &'static str, v: String) -> Div {
    div().flex().flex_col().gap_1().min_w_0().max_w(px(200.))
        .child(mono(INK3).text_xs().child(k))
        .child(mono(INK2).text_xs().whitespace_nowrap().overflow_hidden().text_ellipsis().child(v))
}

fn chip(text: String) -> Div {
    div().px_2().py_1().rounded_full().bg(rgb(WELL)).border_1().border_color(rgb(LINE))
        .child(mono(INK2).text_xs().child(text))
}

// One capability jack in the patch-bay. granted → violet; active → mint (a
// pulse the render loop keeps re-drawing); ungranted → dark.
fn patch_pill(family: &'static str, granted: bool, active: bool) -> Div {
    let (jack, txt, border, bg) = if active {
        (MINT, MINT, MINT_DK, 0x10201C)
    } else if granted {
        (VIOLET, INK2, VIOLET_DK, WELL)
    } else {
        (0x2A323D, INK3, LINE2, WELL)
    };
    div().flex().items_center().gap_2().pl_2().pr_3().py_1().rounded_full()
        .bg(rgb(bg)).border_1().border_color(rgb(border))
        .child(div().size(px(8.)).rounded_full().bg(rgb(jack)))
        .child(mono(txt).text_xs().child(family))
}

impl TeleBox {
    // A pill toggle (40x24). Drives the plugin's enable bit (MCP → the relay).
    fn toggle_el(&self, i: usize, on: bool, cx: &mut Context<Self>) -> impl IntoElement {
        let ml = if on { 18. } else { 2. };
        div().id(("dtoggle", i)).flex().items_center().flex_shrink_0()
            .w(px(40.)).h(px(24.)).rounded_full().bg(rgb(if on { MINT } else { LINE }))
            .cursor_pointer()
            .child(div().size(px(20.)).rounded_full().bg(rgb(0xF3F6FB)).ml(px(ml)))
            .on_click(cx.listener(move |this, _, _, cx| {
                this.state.toggle_plugin(i);
                cx.notify();
            }))
    }

    // One device in the rack rail — a single clean line: a power LED, the name,
    // and (MCP only) a live request-count badge, then the runtime. Not a list
    // row: it loads the device's control surface. State reads from the LED and
    // name brightness; the capability grants live in the panel's patch-bay.
    fn device_strip(&self, i: usize, p: &Plugin, active: bool, selected: bool, requests: u64, cx: &mut Context<Self>) -> impl IntoElement {
        let led = if active { MINT } else { 0x2A323D };
        let name_color = if active { INK } else { INK2 };
        // Only MCP carries a live counter; show it as a badge on the name line.
        let badge = (i == 0 && active).then(|| {
            div().flex_shrink_0().px(px(7.)).py(px(2.)).rounded_full().bg(rgb(MINT_DK))
                .child(mono(MINT).text_xs().child(format!("{requests} req")))
        });
        div().id(("dev", i)).cursor_pointer()
            .flex().items_center().gap_2().pl_3().pr_3().py(px(11.)).rounded_md()
            .bg(rgb(if selected { PANEL2 } else { PANEL }))
            .border_l_2().border_color(rgb(if selected { MINT } else { LINE2 }))
            .border_t_1().border_r_1().border_b_1()
            .child(dot(led, 8.))
            .child(div().font_weight(FontWeight::SEMIBOLD).text_sm().text_color(rgb(name_color)).child(p.name))
            .child(div().flex_1())
            .children(badge)
            .on_click(cx.listener(move |this, _, _, cx| {
                this.state.select(i);
                cx.notify();
            }))
    }

    fn patch_bay(&self, p: &Plugin) -> Div {
        let pills = FAMILIES.iter().map(|f| patch_pill(f, p.perms.contains(*f), p.active.contains(*f)));
        div().flex().flex_wrap().items_center().gap_2()
            .child(mono(INK3).text_xs().child("HOST API"))
            .children(pills)
    }

    // The stage: the selected device's control surface.
    fn device_panel(&self, i: usize, p: &Plugin, active: bool, cx: &mut Context<Self>) -> Div {
        // Header: mark, name, kind, preset, power.
        let mark = div().size(px(42.)).rounded_md().flex().items_center().justify_center()
            .bg(rgb(RAISED)).border_1().border_color(rgb(LINE))
            .child(div().font_weight(FontWeight::BOLD).text_color(rgb(MINT)).child(&p.name[..1]));
        let header = div().flex().items_center().gap_3().pb_4().mb_4().border_b_1().border_color(rgb(LINE2))
            .child(mark)
            .child(
                div().flex().flex_col().gap_1()
                    .child(div().text_xl().font_weight(FontWeight::BOLD).text_color(rgb(INK)).child(p.name))
                    .child(mono(INK3).text_xs().child(p.kind)),
            )
            .child(div().flex_1())
            .child(self.toggle_el(i, active, cx));

        let body = if i == 1 {
            self.export_body(cx).into_any_element()
        } else if i == 2 {
            self.retention_body(cx).into_any_element()
        } else {
            self.relay_body(i, p, active, cx).into_any_element()
        };

        div().flex().flex_col().flex_1().min_w_0()
            .p_5().rounded_lg().bg(rgb(PANEL2)).border_1().border_color(rgb(LINE))
            .child(header)
            .child(self.patch_bay(p))
            .child(div().h(px(16.)))
            .child(body)
    }

    // The Retention device — a LIVE git-style version browser reading the real
    // message_versions store over the relay (retention.rs polls it). Left: every
    // tracked message; right: the selected message's full version chain. Nothing
    // here is hardcoded — it is whatever the archiver has actually recorded.
    fn retention_body(&self, cx: &mut Context<Self>) -> impl IntoElement {
        let ret = self.state.retention();
        let st = ret.stats.clone();
        let sel = ret.selected;

        // Real, separate ephemeral-capture switches. Each click flips one type
        // and the poller applies it via configure_ephemeral_capture; the shown
        // state is the client's true flag read back from get_ephemeral_stats.
        let (sd, vo, va) = self.state.capture();
        let cap_switch = |label: &'static str, on: bool, which: usize, cx: &mut Context<Self>| {
            let ml = if on { 18. } else { 2. };
            well(12.).id(("cap", which)).cursor_pointer().flex().items_center().gap_2()
                .child(div().flex_1().flex().flex_col().gap_1()
                    .child(mono(INK3).text_xs().child(label))
                    .child(mono(if on { MINT } else { INK3 }).text_xs().child(if on { "capturing" } else { "off" })))
                .child(div().flex().items_center().flex_shrink_0().w(px(40.)).h(px(24.)).rounded_full()
                    .bg(rgb(if on { MINT } else { LINE }))
                    .child(div().size(px(20.)).rounded_full().bg(rgb(0xF3F6FB)).ml(px(ml))))
                .on_click(cx.listener(move |this, _, _, cx| {
                    this.state.toggle_capture(which);
                    cx.notify();
                }))
        };
        let switches = div().flex().gap_2()
            .child(div().flex_1().child(cap_switch("self-destruct", sd, 0, cx)))
            .child(div().flex_1().child(cap_switch("view-once", vo, 1, cx)))
            .child(div().flex_1().child(cap_switch("vanishing", va, 2, cx)));

        let readout = div().flex().items_center().gap_5().p_4().rounded_lg().bg(rgb(WELL))
            .border_1().border_color(rgb(LINE))
            .child(div().flex().items_baseline().gap_2()
                .child(div().text_3xl().font_weight(FontWeight::BOLD).text_color(rgb(INK)).child(st.total_versions.to_string()))
                .child(mono(INK2).child("versions")))
            .child(div().flex().flex_col().gap_2()
                .child(mono(INK3).text_xs().child(format!(
                    "{} messages tracked · {} edits · {} deletions preserved",
                    st.messages_tracked, st.edits, st.deletions)))
                .child(mono(if st.available { MINT } else { INK3 }).text_xs()
                    .child(if st.available { "message_versions · live" } else { "archiver not reporting" })))
            .child(div().flex_1())
            .child(mono(INK3).text_xs().child("polled · 2s"));

        let mut list = div().flex().flex_col().gap_1();
        if ret.tracked.is_empty() {
            list = list.child(mono(INK3).text_sm().child("no tracked messages yet — edit or delete one"));
        }
        for t in ret.tracked.iter().take(12) {
            let is_sel = sel == Some((t.chat_id, t.message_id));
            let (cid, mid) = (t.chat_id, t.message_id);
            let kc = kind_color(&t.latest_kind);
            list = list.child(
                div().id(("trk", mid as usize)).cursor_pointer()
                    .flex().items_center().gap_2().py_2().px_3().rounded_md()
                    .bg(rgb(if is_sel { PANEL2 } else { WELL }))
                    .border_1().border_color(rgb(if is_sel { MINT_DK } else { LINE2 }))
                    .child(dot(kc, 7.))
                    .child(mono(INK3).text_xs().child(format!("{mid}")))
                    .child(div().flex_1().min_w_0().text_sm().text_color(rgb(INK)).whitespace_nowrap().overflow_hidden().text_ellipsis().child(t.latest_content.clone()))
                    .child(mono(INK2).text_xs().child(format!("{}v", t.versions)))
                    .on_click(cx.listener(move |this, _, _, cx| {
                        this.state.select_tracked(cid, mid);
                        cx.notify();
                    })),
            );
        }

        let mut chain = div().flex().flex_col().gap_2();
        if ret.chain.is_empty() {
            chain = chain.child(mono(INK3).text_sm().child("select a message to see its version chain"));
        }
        for v in ret.chain.iter() {
            let kc = kind_color(&v.kind);
            chain = chain.child(
                div().flex().items_center().gap_3().py_2().px_3().rounded_md().bg(rgb(WELL))
                    .border_1().border_color(rgb(LINE2))
                    .child(dot(kc, 8.))
                    .child(mono(INK3).text_xs().w(px(22.)).child(format!("v{}", v.version)))
                    .child(div().w(px(60.)).child(mono(kc).text_xs().child(v.kind.clone())))
                    .child(div().flex_1().min_w_0().text_sm().text_color(rgb(INK)).whitespace_nowrap().overflow_hidden().text_ellipsis().child(v.content.clone())),
            );
        }

        let hist_label = match sel {
            Some((_, m)) => format!("VERSION HISTORY · msg {m}"),
            None => "VERSION HISTORY".to_string(),
        };
        div().flex().flex_col().gap_4().w_full()
            .child(switches)
            .child(readout)
            .child(div().flex().gap_4()
                .child(div().flex_1().min_w_0().flex().flex_col().gap_2()
                    .child(mono(INK3).text_xs().child("TRACKED MESSAGES · latest first"))
                    .child(list))
                .child(div().flex_1().min_w_0().flex().flex_col().gap_2()
                    .child(mono(INK3).text_xs().child(hist_label))
                    .child(chain)))
            .child(div().pl_3().py_2().border_l_2().border_color(rgb(VIOLET_DK))
                .child(div().text_sm().text_color(rgb(INK2)).child(
                    "Live from the archiver's message_versions store — every edit appended, every deletion preserved. Click a message to inspect its chain.")))
    }

    // A generic device surface — OPERABLE, not a readout. The poller (retention.rs)
    // fills PanelData from real tools over the relay; here we render its readout,
    // its clickable rows (chats / bots / the tool catalog), and a primary button
    // that fires a real tool call via HostState::primary_action — the same path
    // the QA socket's `action` command drives. Wallet stays read-only on purpose
    // (spending is dark); AI has an action but no rows.
    fn relay_body(&self, i: usize, p: &Plugin, _active: bool, cx: &mut Context<Self>) -> impl IntoElement {
        let d = self.state.panel(i);
        let sel_row = self.state.panel_row_sel(i);
        let busy = self.state.busy();

        // Per-device labels. Bots' verb follows the picked bot's run state.
        let (list_label, has_rows, has_action) = match i {
            0 => ("TOOL CATALOG · every advertised tool", true, true),
            1 => ("PICK A CHAT TO EXPORT", true, true),
            3 => ("PICK A CHAT TO ARCHIVE", true, true),
            4 => ("BOTS · click to select", true, true),
            5 => ("", false, false),   // Wallet: live balance, spend stays dark
            6 => ("", false, true),    // AI: action, no rows
            _ => ("", false, false),
        };
        let action_label: String = match i {
            0 => "Invoke canary · list_chats".into(),
            1 => {
                let running = d.readout.iter().any(|(k, v)| {
                    k == "state" && matches!(v.as_str(),
                        "exporting" | "running" | "archiving" | "scanning" | "paused" | "queued")
                });
                if running { "Cancel export".into() } else { "Export selected chat (headless) →".into() }
            }
            3 => "Archive selected chat →".into(),
            4 => match sel_row.and_then(|r| d.rows.get(r)) {
                Some(r) if r.on => format!("Stop {} →", r.title),
                Some(r) => format!("Start {} →", r.title),
                None => "Start / Stop selected bot".into(),
            },
            6 => "Synthesize sample →".into(),
            _ => String::new(),
        };

        // Readout well: the real key/value lines the read tool returned.
        let field = |k: String, v: String| {
            div().flex().flex_col().gap_1().min_w_0().flex_shrink_0()
                .child(mono(INK3).text_xs().child(k))
                .child(mono(INK).child(v))
        };
        // Body shows domain data only — the plugin's on/off lives in the header.
        let mut readout = div().flex().flex_wrap().items_center().gap_5().p_5()
            .rounded_lg().bg(rgb(WELL)).border_1().border_color(rgb(LINE));
        if d.readout.is_empty() {
            readout = readout.child(mono(INK3).text_sm()
                .child(if self.state.is_running() { "querying client…" } else { "start the host to query" }));
        }
        for (k, v) in d.readout.iter() {
            readout = readout.child(field(k.clone(), v.clone()));
        }
        readout = readout.child(div().flex_1())
            .child(div().size(px(7.)).rounded_full().bg(rgb(if d.loaded { OK } else { INK3 })))
            .child(mono(INK3).text_xs().child(if d.loaded { "live · 2s" } else { "polled" }));

        // Clickable rows (chats / bots / tools).
        let mut rows = div().flex().flex_col().gap_1();
        if has_rows && d.rows.is_empty() {
            rows = rows.child(mono(INK3).text_sm().child("nothing to list yet — querying the client…"));
        }
        for (idx, r) in d.rows.iter().enumerate().take(40) {
            let is_sel = sel_row == Some(idx);
            let dotc = if i == 4 { if r.on { OK } else { INK3 } } else if is_sel { MINT } else { INK3 };
            let sub = if i == 4 && r.on { "running".to_string() }
                else if i == 4 { "stopped".to_string() }
                else { r.sub.clone() };
            rows = rows.child(
                div().id(("prow", i * 1000 + idx)).cursor_pointer()
                    .flex().items_center().gap_2().py_2().px_3().rounded_md()
                    .bg(rgb(if is_sel { PANEL2 } else { WELL }))
                    .border_1().border_color(rgb(if is_sel { MINT_DK } else { LINE2 }))
                    .child(dot(dotc, 7.))
                    .child(div().flex_1().min_w_0().text_sm().text_color(rgb(INK))
                        .whitespace_nowrap().overflow_hidden().text_ellipsis()
                        .child(if r.title.is_empty() { r.sid.clone() } else { r.title.clone() }))
                    .child(mono(INK3).text_xs().max_w(px(260.)).whitespace_nowrap().overflow_hidden().text_ellipsis().child(sub))
                    .on_click(cx.listener(move |this, _, _, cx| {
                        this.state.select_panel_row(i, idx);
                        cx.notify();
                    })),
            );
        }

        // Primary action button + the result line of the last call.
        let btn = if has_action {
            let (bg, fg) = if busy { (WELL, INK3) } else { (MINT_DK, MINT) };
            Some(div().id(("act", i)).cursor_pointer().flex_shrink_0()
                .px_4().py_2().rounded_md().bg(rgb(bg)).border_1().border_color(rgb(if busy { LINE } else { MINT }))
                .child(div().text_sm().font_weight(FontWeight::SEMIBOLD).text_color(rgb(fg))
                    .child(if busy { "working…".to_string() } else { action_label }))
                .on_click(cx.listener(move |this, _, _, cx| {
                    this.state.primary_action(i);
                    cx.notify();
                })))
        } else {
            None
        };
        let result = if d.result.is_empty() {
            mono(INK3).text_xs().child(if has_action { "no action run yet".to_string() } else { "read-only device".to_string() })
        } else {
            let c = if d.result.starts_with('✕') { CRIT } else if d.result.starts_with('✓') { OK } else { INK2 };
            mono(c).text_sm().child(d.result.clone())
        };
        let action_bar = div().flex().items_center().gap_3().pt_1()
            .children(btn)
            .child(result);

        // Assemble. Wallet (5) shows the dark spend note in place of a button.
        let mut col = div().flex().flex_col().gap_4().w_full().child(readout);
        if has_rows {
            col = col.child(div().flex().flex_col().gap_2()
                .child(mono(INK3).text_xs().child(list_label))
                .child(rows));
        }
        col = col.child(action_bar);
        if i == 5 {
            col = col.child(div().pl_3().py_2().border_l_2().border_color(rgb(LINE))
                .child(mono(INK3).text_xs().child("send_stars / spending is disabled by design — this panel reads the live balance only.")));
        }
        col.child(div().pl_3().py_2().border_l_2().border_color(rgb(VIOLET_DK))
            .child(div().text_sm().text_color(rgb(INK2)).child(p.desc)))
    }

    // Export device — built strictly from the ontology (ONTOLOGY.md): Service in
    // the header, an Item (a chat, found by SEARCH among all 727), and a Job (the
    // run). Two states that never mix: IDLE (find → pick → Export) and RUNNING
    // (target · progress · Cancel — the job replaces the picker).
    fn export_body(&self, cx: &mut Context<Self>) -> impl IntoElement {
        let d = self.state.panel(1);
        let busy = self.state.busy();
        let result_line = |r: String| -> Div {
            if r.is_empty() {
                mono(INK3).text_xs().child("")
            } else {
                let c = if r.starts_with('✕') { CRIT } else if r.starts_with('✓') { OK } else { INK2 };
                mono(c).text_sm().child(r)
            }
        };

        // RUNNING — the job replaces the picker.
        if let Some(run) = self.state.export_run() {
            let frac = if run.total > 0 {
                (run.done as f32 / run.total as f32).clamp(0.0, 1.0)
            } else {
                0.0
            };
            let bar = div().w_full().h(px(8.)).rounded_full().bg(rgb(WELL))
                .border_1().border_color(rgb(LINE2))
                .child(div().h_full().rounded_full().bg(rgb(MINT)).w(relative(frac)));
            return div().flex().flex_col().gap_4().w_full()
                .child(div().flex().items_center().gap_3()
                    .child(dot(MINT, 8.))
                    .child(div().text_lg().font_weight(FontWeight::SEMIBOLD).text_color(rgb(INK))
                        .child(format!("Exporting {}", run.chat)))
                    .child(div().flex_1())
                    .child(mono(INK2).child(format!("{} / {} messages", run.done, run.total))))
                .child(bar)
                .child(div().flex().items_center().gap_3()
                    .child(mono(INK3).text_xs().child(format!(
                        "{} messages exported to disk · {}",
                        run.done, run.state)))
                    .child(div().flex_1())
                    .child(div().id("exp-cancel").cursor_pointer().px_4().py_2().rounded_md()
                        .bg(rgb(0x2A1512)).border_1().border_color(rgb(CRIT))
                        .child(div().text_sm().font_weight(FontWeight::SEMIBOLD).text_color(rgb(CRIT))
                            .child(if busy { "working…" } else { "Cancel export" }))
                        .on_click(cx.listener(|this, _, _, cx| { this.state.primary_action(1); cx.notify(); }))))
                .child(result_line(d.result.clone()))
                .child(div().pl_3().py_2().border_l_2().border_color(rgb(VIOLET_DK))
                    .child(mono(INK3).text_xs().child("Headless — runs inside TeleBox, no Tlgrm window.")))
                .into_any_element();
        }

        // IDLE — find an Item (a chat), then act.
        let query = self.state.export_search();
        let ql = query.to_lowercase();
        let target = self.state.export_target();
        let mut matched: Vec<&PanelRow> = d.rows.iter()
            .filter(|r| ql.is_empty() || r.title.to_lowercase().contains(&ql))
            .collect();
        let (total, shown) = (d.rows.len(), matched.len());
        matched.truncate(60);
        let typing = !query.is_empty();

        let search_box = div().id("exp-search").track_focus(&self.search_focus)
            .flex().items_center().gap_2().px_3().py_2().rounded_md().bg(rgb(WELL))
            .border_1().border_color(rgb(if typing { MINT_DK } else { LINE }))
            .child(mono(INK3).child("⌕"))
            .child(if typing {
                mono(INK).child(format!("{query}▏"))
            } else {
                mono(INK3).child("type to find a chat…".to_string())
            })
            .on_click(cx.listener(|this, _, window, cx| { window.focus(&this.search_focus, cx); cx.notify(); }))
            .on_key_down(cx.listener(|this, ev: &KeyDownEvent, _window, cx| {
                let ks = &ev.keystroke;
                if ks.modifiers.control || ks.modifiers.platform {
                    return;
                }
                match ks.key.as_str() {
                    "backspace" => this.state.export_search_backspace(),
                    "escape" => this.state.export_search_clear(),
                    _ => {
                        if let Some(ch) = ks.key_char.as_ref() {
                            this.state.export_search_push(ch.as_str());
                        }
                    }
                }
                cx.notify();
            }));

        let has_target = target.is_some();
        let btn_label = match &target {
            Some((_, t)) => format!("Export {t} →"),
            None => "Pick a chat to export".to_string(),
        };
        let export_btn = div().id("exp-go").cursor_pointer().flex_shrink_0().px_4().py_2().rounded_md()
            .bg(rgb(if has_target && !busy { MINT_DK } else { WELL }))
            .border_1().border_color(rgb(if has_target { MINT } else { LINE }))
            .child(div().text_sm().font_weight(FontWeight::SEMIBOLD)
                .text_color(rgb(if has_target { MINT } else { INK3 })).child(btn_label))
            .on_click(cx.listener(|this, _, _, cx| { this.state.primary_action(1); cx.notify(); }));

        let mut list = div().flex().flex_col().gap_1();
        if matched.is_empty() {
            list = list.child(mono(INK3).text_sm().child(if typing {
                format!("no chat matches \"{query}\"")
            } else {
                "querying the client for your chats…".to_string()
            }));
        }
        for r in matched {
            let selected = target.as_ref().map(|(id, _)| *id == r.id).unwrap_or(false);
            let (id, title, sub) = (r.id, r.title.clone(), r.sub.clone());
            let pick = title.clone();
            list = list.child(div().id(("exprow", id as usize)).cursor_pointer()
                .flex().items_center().gap_2().py_2().px_3().rounded_md()
                .bg(rgb(if selected { PANEL2 } else { WELL }))
                .border_1().border_color(rgb(if selected { MINT_DK } else { LINE2 }))
                .child(dot(if selected { MINT } else { INK3 }, 7.))
                .child(div().flex_1().min_w_0().text_sm().text_color(rgb(INK))
                    .whitespace_nowrap().overflow_hidden().text_ellipsis().child(title))
                .child(mono(INK3).text_xs().child(sub))
                .on_click(cx.listener(move |this, _, _, cx| {
                    this.state.set_export_target(id, pick.clone());
                    cx.notify();
                })));
        }

        let meta = if typing {
            format!("{shown} of {total} chats")
        } else {
            format!("{total} chats · type to filter")
        };
        div().flex().flex_col().gap_3().w_full()
            .child(div().flex().items_center().gap_3()
                .child(div().flex_1().child(search_box))
                .child(export_btn))
            .child(div().flex().items_center().gap_2()
                .child(mono(INK3).text_xs().child(meta))
                .child(div().flex_1())
                .child(result_line(d.result.clone())))
            .child(list)
            .into_any_element()
    }
}

impl Render for TeleBox {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        // Start the render/QA loop once — it repaints, and fulfills any pending
        // screenshot request by rendering the window to a PNG offscreen (the
        // app's own monitor — no system capture tool involved).
        if !self.loop_started {
            self.loop_started = true;
            cx.spawn_in(window, async move |this, cx| loop {
                cx.background_executor().timer(Duration::from_millis(250)).await;
                let r = this.update_in(cx, |this, window, cx| {
                    let (path, armed) = this.state.peek_shot();
                    if let Some(path) = path {
                        if armed {
                            match window.render_to_image() {
                                Ok(img) => {
                                    let (w, h) = (img.width(), img.height());
                                    let ok = img.save(&path).is_ok();
                                    this.state.finish_shot(w, h, ok);
                                    this.state.log("qa", format!("shot → {path} ({w}x{h})"));
                                }
                                Err(e) => {
                                    this.state.finish_shot(0, 0, false);
                                    this.state.log("qa", format!("shot failed: {e}"));
                                }
                            }
                        } else {
                            this.state.arm_shot();
                        }
                    }
                    cx.notify();
                });
                if r.is_err() {
                    break;
                }
            })
            .detach();
        }

        let (running, upstream, requests, clients, endpoint, bridge) = {
            let i = self.state.0.lock().unwrap();
            (i.running, i.upstream_connected, i.requests, i.clients, i.endpoint.clone(), i.upstream.clone())
        };
        let enabled = self.state.enabled_vec();
        let selected = self.state.selected().min(plugin_templates().len() - 1);
        let plugins = plugin_templates();

        // --- title bar ---
        let titlebar = div().flex().items_center().gap_3().px_4().py_3().bg(rgb(PANEL))
            .border_b_1().border_color(rgb(LINE))
            .child(div().size(px(22.)).rounded_md().flex().items_center().justify_center()
                .bg(rgb(RAISED)).border_1().border_color(rgb(MINT_DK))
                .child(div().text_sm().child("📦")))
            .child(div().font_weight(FontWeight::BOLD).text_color(rgb(INK)).child("TELEBOX"))
            .child(mono(INK3).child("· rack"))
            .child(div().flex_1())
            .child(chip("@poly1337".into()))
            .child(div().px_2().py_1().rounded_full().bg(rgb(0x1C1808)).border_1().border_color(rgb(0x4A3F22))
                .child(mono(AMBER).text_xs().child("premium")));

        // --- host transport bar ---
        let (pill_color, pill_text) = if running { (MINT, "Host running") } else { (CRIT, "Host stopped") };
        let pill = div().flex().items_center().gap_2().px_3().py_1().rounded_full().bg(rgb(WELL))
            .border_1().border_color(rgb(LINE)).child(dot(pill_color, 8.))
            .child(div().text_sm().font_weight(FontWeight::SEMIBOLD).text_color(rgb(pill_color)).child(pill_text));

        let ctrl = |lbl: &'static str, id: &'static str, color: u32, cx: &mut Context<Self>, act: fn(&mut TeleBox)| {
            div().id(id).cursor_pointer().px_3().py_1().rounded_md().bg(rgb(RAISED))
                .border_1().border_color(rgb(LINE))
                .child(div().text_sm().font_weight(FontWeight::SEMIBOLD).text_color(rgb(color)).child(lbl))
                .on_click(cx.listener(move |this, _, _, cx| { act(this); cx.notify(); }))
        };
        let mut controls = div().flex().gap_2();
        if running {
            controls = controls
                .child(ctrl("Stop", "stop", CRIT, cx, |t| t.state.stop()))
                .child(ctrl("Restart", "restart", AMBER, cx, |t| t.state.restart()));
        } else {
            controls = controls.child(ctrl("Start", "start", MINT, cx, |t| t.state.start()));
        }
        let up_color = if upstream { MINT } else { INK3 };
        let hostbar = div().flex().items_center().gap_4().px_4().py_3().bg(rgb(0x11141A))
            .border_b_1().border_color(rgb(LINE))
            .child(pill).child(controls).child(div().flex_1())
            .child(stat("requests", requests.to_string(), MINT))
            .child(stat("upstream", if upstream { "connected".into() } else { "—".into() }, up_color))
            .child(stat("clients", clients.to_string(), INK))
            .child(stat_path("endpoint", endpoint))
            .child(stat_path("bridge", bridge));

        // --- rack rail ---
        let mut rail = div().flex().flex_col().gap_2().w(px(260.)).flex_shrink_0()
            .p_3().bg(rgb(PANEL)).border_r_1().border_color(rgb(LINE))
            .child(div().flex().items_center().px_1().pb_1()
                .child(div().flex_1())
                .child(mono(INK3).text_xs().child(format!("{} devices", plugins.len()))));
        for (i, p) in plugins.iter().enumerate() {
            let active = plugin_active(i, running, &enabled);
            rail = rail.child(self.device_strip(i, p, active, i == selected, requests, cx));
        }

        // --- stage (selected device) ---
        let sp = &plugins[selected];
        let sactive = plugin_active(selected, running, &enabled);
        let stage = div().id("stage-scroll").flex_1().min_w_0().overflow_y_scroll().p_5()
            .child(self.device_panel(selected, sp, sactive, cx))
            .child(
                div().flex().items_center().gap_4().mt_4().pt_3().border_t_1().border_color(rgb(LINE2))
                    .child(div().flex().items_center().gap_2().child(dot(OK, 7.)).child(mono(INK3).text_xs().child("/tmp/tlgrm_mcp.sock")))
                    .child(mono(INK3).text_xs().child("tools 359")).child(mono(INK3).text_xs().child("layer 229"))
                    .child(div().flex_1())
                    .child(mono(INK3).text_xs().child("tlgrm 7.1.3a · telebox M2")),
            );

        div().flex().flex_col().size_full().bg(rgb(VOID)).text_color(rgb(INK))
            .child(titlebar).child(hostbar)
            .child(div().flex().flex_1().min_h_0().child(rail).child(stage))
    }
}

fn main() {
    application().run(|cx: &mut App| {
        let bounds = Bounds::centered(None, size(px(1240.), px(880.)), cx);
        cx.open_window(
            WindowOptions { window_bounds: Some(WindowBounds::Windowed(bounds)), ..Default::default() },
            |_, cx| cx.new(|cx| TeleBox::new(cx)),
        )
        .unwrap();
        cx.activate(true);
    });
}
