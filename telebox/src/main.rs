// TeleBox — the plugin-hosted controller for the Tlgrm client.
//
// M0+controls: a GPUI control panel with real interactivity.
//  - Host Start / Stop / Restart actually bind and unbind the aggregated MCP
//    endpoint (see host.rs / mcp_relay.rs).
//  - Each plugin's bypass toggle flips it; MCP's toggle drives the real relay,
//    the other six flip modelled state (they are not wired yet).
//  - The left rail switches views: Plugins (the rack), Permissions (the Host
//    API grant matrix), Activity (the live log).
//
// The MCP module is wired end-to-end: TeleBox proxies to the client's bridge.

mod host;
mod mcp_relay;

use std::time::Duration;

use gpui::{App, Bounds, Context, Div, FontWeight, Window, WindowBounds, WindowOptions, div,
    prelude::*, px, rgb, size};
use gpui_platform::application;

use host::{plugin_templates, HostState, Plugin, Runtime, FAMILIES};

const GROUND: u32 = 0x121319;
const PANEL: u32 = 0x1B1D26;
const INSET: u32 = 0x15161D;
const SURFACE2: u32 = 0x20222C;
const LINE: u32 = 0x2B2E3A;
const INK: u32 = 0xE9EAF1;
const INK2: u32 = 0xA7AABE;
const INK3: u32 = 0x6E7286;
const ACCENT: u32 = 0x8A82FF;
const LED: u32 = 0x43D9A3;
const CRIT: u32 = 0xE5647D;
const WARN: u32 = 0xE0A54A;
const PY: u32 = 0x5FA8D8;
const RS: u32 = 0xE0854E;

const ENDPOINT: &str = "/tmp/telebox_host.sock";
const UPSTREAM: &str = "/tmp/tlgrm_mcp.sock";
const TOKEN: &str = "/tmp/auth_token";

#[derive(Clone, Copy, PartialEq)]
enum PanelView {
    Plugins,
    Permissions,
    Activity,
}

struct TeleBox {
    state: HostState,
    view: PanelView,
    enabled: Vec<bool>, // per plugin; index 0 (MCP) mirrors the relay, unused here
}

impl TeleBox {
    fn new(cx: &mut Context<Self>) -> Self {
        let state = HostState::new(ENDPOINT.into(), UPSTREAM.into(), TOKEN.into());
        state.log("host", "TeleBox host starting");
        state.start();

        cx.spawn(async move |this, cx| loop {
            cx.background_executor()
                .timer(Duration::from_millis(400))
                .await;
            if this.update(cx, |_this, cx| cx.notify()).is_err() {
                break;
            }
        })
        .detach();

        Self {
            state,
            view: PanelView::Plugins,
            // Export, Retention, Archiver, Bots on; Wallet, AI off.
            enabled: vec![true, true, true, true, true, false, false],
        }
    }

    fn toggle_plugin(&mut self, i: usize) {
        if i == 0 {
            if self.state.is_running() {
                self.state.stop();
            } else {
                self.state.start();
            }
        } else if i < self.enabled.len() {
            self.enabled[i] = !self.enabled[i];
            let name = plugin_templates()[i].name;
            let word = if self.enabled[i] { "enabled" } else { "bypassed" };
            self.state.log("host", format!("{name} {word}"));
        }
    }
}

fn dot(color: u32, sz: f32) -> Div {
    div().size(px(sz)).rounded_full().bg(rgb(color))
}

fn mono(color: u32) -> Div {
    div().font_family("Menlo").text_color(rgb(color))
}

fn runtime_badge(rt: Runtime) -> Div {
    let color = match rt {
        Runtime::Rust => RS,
        Runtime::Python => PY,
    };
    div()
        .flex()
        .items_center()
        .gap_1()
        .px_2()
        .py_1()
        .rounded_md()
        .bg(rgb(INSET))
        .border_1()
        .border_color(rgb(LINE))
        .child(div().size(px(7.)).bg(rgb(color)))
        .child(mono(INK2).text_xs().child(rt.label()))
}

fn stat(k: &'static str, v: String, color: u32) -> Div {
    div()
        .flex()
        .flex_col()
        .gap_1()
        .child(mono(INK3).text_xs().child(k))
        .child(mono(color).child(v))
}

impl TeleBox {
    fn module_card(&self, i: usize, p: &Plugin, active: bool, cx: &mut Context<Self>) -> Div {
        let led = if active { LED } else { INK3 };
        let name_color = if active { INK } else { INK3 };

        // bypass toggle — real interactivity
        let knob_ml = if active { 18. } else { 2. };
        let toggle = div()
            .id(("toggle", i))
            .w(px(38.))
            .h(px(22.))
            .rounded_full()
            .bg(rgb(if active { LED } else { LINE }))
            .cursor_pointer()
            .child(
                div()
                    .size(px(16.))
                    .rounded_full()
                    .bg(rgb(0xFFFFFF))
                    .ml(px(knob_ml))
                    .mt(px(2.)),
            )
            .on_click(cx.listener(move |this, _, _, cx| {
                this.toggle_plugin(i);
                cx.notify();
            }));

        let mut header = div()
            .flex()
            .items_center()
            .gap_2()
            .child(dot(led, 8.))
            .child(
                div()
                    .font_weight(FontWeight::SEMIBOLD)
                    .text_color(rgb(name_color))
                    .child(p.name),
            );
        if p.live {
            header = header.child(
                div()
                    .px_2()
                    .py_1()
                    .rounded_md()
                    .bg(rgb(INSET))
                    .border_1()
                    .border_color(rgb(LINE))
                    .child(mono(LED).text_xs().child("● live")),
            );
        }
        header = header
            .child(div().flex_1())
            .child(runtime_badge(p.runtime))
            .child(toggle);

        div()
            .flex()
            .flex_1()
            .flex_col()
            .gap_2()
            .p_4()
            .rounded_md()
            .bg(rgb(PANEL))
            .border_1()
            .border_color(rgb(LINE))
            .child(header)
            .child(mono(INK3).text_xs().child(p.slot))
            .child(div().text_sm().text_color(rgb(INK2)).child(p.desc))
            .child(mono(INK3).text_xs().child(p.perms))
    }

    fn permissions_matrix(&self) -> Div {
        let plugins = plugin_templates();
        let header = div().flex().items_center().py_2().px_3().border_b_1().border_color(rgb(LINE)).children(
            std::iter::once(div().w(px(150.)).child(mono(INK3).text_xs().child("plugin"))).chain(
                FAMILIES.iter().map(|f| {
                    let color = if *f == "invoke" { ACCENT } else { INK3 };
                    div().flex_1().child(mono(color).text_xs().child(*f))
                }),
            ),
        );
        let rows = plugins.iter().map(|p| {
            div().flex().items_center().py_2().px_3().border_b_1().border_color(rgb(0x212D39)).children(
                std::iter::once(
                    div().w(px(150.)).child(
                        div().font_weight(FontWeight::SEMIBOLD).text_sm().text_color(rgb(INK)).child(p.name),
                    ),
                )
                .chain(FAMILIES.iter().map(|f| {
                    let granted = p.perms.contains(f);
                    let cell = if granted {
                        let c = if *f == "invoke" { ACCENT } else { ACCENT };
                        div().size(px(14.)).rounded_md().bg(rgb(c))
                    } else {
                        div().size(px(5.)).rounded_full().bg(rgb(LINE))
                    };
                    div().flex_1().flex().items_center().child(cell)
                })),
            )
        });
        div()
            .flex()
            .flex_col()
            .rounded_md()
            .bg(rgb(PANEL))
            .border_1()
            .border_color(rgb(LINE))
            .child(header)
            .children(rows)
    }
}

impl Render for TeleBox {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let (running, upstream, requests, clients, endpoint, bridge, logs) = {
            let i = self.state.0.lock().unwrap();
            (
                i.running,
                i.upstream_connected,
                i.requests,
                i.clients,
                i.endpoint.clone(),
                i.upstream.clone(),
                i.log.iter().rev().take(16).cloned().collect::<Vec<_>>(),
            )
        };
        let view = self.view;
        let enabled = self.enabled.clone();
        let plugins = plugin_templates();

        // titlebar
        let titlebar = div()
            .flex()
            .items_center()
            .gap_3()
            .px_4()
            .py_3()
            .bg(rgb(SURFACE2))
            .border_b_1()
            .border_color(rgb(LINE))
            .child(div().font_weight(FontWeight::BOLD).text_color(rgb(INK)).child("TELEBOX"))
            .child(mono(INK3).child("· plugin host"));

        // status pill reflects the real relay state
        let (pill_color, pill_text) = if running {
            (LED, "Host running")
        } else {
            (CRIT, "Host stopped")
        };
        let pill = div()
            .flex()
            .items_center()
            .gap_2()
            .px_3()
            .py_1()
            .rounded_full()
            .bg(rgb(INSET))
            .border_1()
            .border_color(rgb(LINE))
            .child(dot(pill_color, 8.))
            .child(
                div()
                    .text_sm()
                    .font_weight(FontWeight::SEMIBOLD)
                    .text_color(rgb(pill_color))
                    .child(pill_text),
            );

        // lifecycle buttons — real
        let ctrl = |label: &'static str, id: &'static str, color: u32, cx: &mut Context<Self>, act: fn(&mut TeleBox)| {
            div()
                .id(id)
                .cursor_pointer()
                .px_3()
                .py_1()
                .rounded_md()
                .bg(rgb(INSET))
                .border_1()
                .border_color(rgb(LINE))
                .child(div().text_sm().font_weight(FontWeight::SEMIBOLD).text_color(rgb(color)).child(label))
                .on_click(cx.listener(move |this, _, _, cx| {
                    act(this);
                    cx.notify();
                }))
        };
        let mut controls = div().flex().gap_2();
        if running {
            controls = controls
                .child(ctrl("Stop", "stop", CRIT, cx, |t| t.state.stop()))
                .child(ctrl("Restart", "restart", WARN, cx, |t| t.state.restart()));
        } else {
            controls = controls.child(ctrl("Start", "start", LED, cx, |t| t.state.start()));
        }

        let up_label = if upstream { "connected" } else { "—" };
        let up_color = if upstream { LED } else { INK3 };
        let hostbar = div()
            .flex()
            .items_center()
            .gap_4()
            .px_4()
            .py_3()
            .bg(rgb(0x1A1C23))
            .border_b_1()
            .border_color(rgb(LINE))
            .child(pill)
            .child(controls)
            .child(div().flex_1())
            .child(stat("MCP requests", requests.to_string(), ACCENT))
            .child(stat("Upstream", up_label.into(), up_color))
            .child(stat("Clients", clients.to_string(), INK))
            .child(stat("Endpoint", endpoint, INK2))
            .child(stat("Bridge", bridge, INK2));

        // rail — switches views
        let rail_item = |label: &'static str, v: PanelView, id: &'static str, cx: &mut Context<Self>| {
            let selected = view == v;
            div()
                .id(id)
                .cursor_pointer()
                .px_3()
                .py_2()
                .rounded_md()
                .bg(rgb(if selected { 0x232238 } else { 0x1A1C23 }))
                .text_color(rgb(if selected { INK } else { INK2 }))
                .child(div().text_sm().font_weight(FontWeight::SEMIBOLD).child(label))
                .on_click(cx.listener(move |this, _, _, cx| {
                    this.view = v;
                    cx.notify();
                }))
        };
        let rail = div()
            .flex()
            .flex_col()
            .gap_1()
            .w(px(180.))
            .p_3()
            .bg(rgb(SURFACE2))
            .border_r_1()
            .border_color(rgb(LINE))
            .child(mono(INK3).text_xs().child("HOST"))
            .child(rail_item("Plugins", PanelView::Plugins, "rail-plugins", cx))
            .child(rail_item("Permissions", PanelView::Permissions, "rail-perms", cx))
            .child(rail_item("Activity", PanelView::Activity, "rail-activity", cx));

        // main content by view
        let heading = |t: &'static str| {
            div().text_lg().font_weight(FontWeight::BOLD).text_color(rgb(INK)).child(t)
        };
        let content = match view {
            PanelView::Plugins => {
                let mut cards: Vec<Div> = Vec::new();
                for (i, p) in plugins.iter().enumerate() {
                    let active = if i == 0 { running } else { enabled[i] };
                    cards.push(self.module_card(i, p, active, cx));
                }
                let mut rack = div().flex().flex_col().gap_3();
                let mut it = cards.into_iter();
                loop {
                    let a = it.next();
                    let b = it.next();
                    match (a, b) {
                        (Some(a), Some(b)) => {
                            rack = rack.child(div().flex().gap_3().child(a).child(b));
                        }
                        (Some(a), None) => {
                            rack = rack.child(div().flex().gap_3().child(a));
                            break;
                        }
                        _ => break,
                    }
                }
                div().flex().flex_col().gap_3().child(heading("The rack")).child(rack)
            }
            PanelView::Permissions => div()
                .flex()
                .flex_col()
                .gap_3()
                .child(heading("Host API permissions"))
                .child(mono(INK3).text_sm().child("plugin × capability family — a filled cell is a grant"))
                .child(self.permissions_matrix()),
            PanelView::Activity => {
                let log = div().flex().flex_col().gap_1().p_3().rounded_md().bg(rgb(PANEL))
                    .border_1().border_color(rgb(LINE))
                    .children(logs.into_iter().map(|e| {
                        div().flex().gap_3().text_xs()
                            .child(mono(INK3).child(e.t))
                            .child(mono(ACCENT).w(px(58.)).child(e.src))
                            .child(mono(INK).child(e.msg))
                    }));
                div().flex().flex_col().gap_3().child(heading("Activity")).child(log)
            }
        };

        div()
            .flex()
            .flex_col()
            .size_full()
            .bg(rgb(GROUND))
            .text_color(rgb(INK))
            .child(titlebar)
            .child(hostbar)
            .child(
                div()
                    .flex()
                    .flex_1()
                    .child(rail)
                    .child(div().flex_1().p_5().child(content)),
            )
    }
}

fn main() {
    application().run(|cx: &mut App| {
        let bounds = Bounds::centered(None, size(px(1080.), px(820.)), cx);
        cx.open_window(
            WindowOptions {
                window_bounds: Some(WindowBounds::Windowed(bounds)),
                ..Default::default()
            },
            |_, cx| cx.new(|cx| TeleBox::new(cx)),
        )
        .unwrap();
        cx.activate(true);
    });
}
