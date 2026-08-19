// TeleBox — the plugin-hosted controller for the Tlgrm client.
//
// M0, first vertical slice: a GPUI control panel whose live state is driven by
// a real MCP plugin. TeleBox stands up its own aggregated MCP socket and
// proxies to the client's existing bridge; the panel shows that plugin as
// active, counts the requests flowing through it, and streams a log.
//
// The window renders the whole rack it will eventually host — MCP wired for
// real, the other six modelled — so the shape of the host is visible now.

mod host;
mod mcp_relay;

use std::time::Duration;

use gpui::{App, Bounds, Context, Div, FontWeight, Window, WindowBounds, WindowOptions, div,
    prelude::*, px, rgb, size};
use gpui_platform::application;

use host::{HostState, Plugin, Runtime};

// palette — the graphite / iris / signal-green of the approved design
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
const PY: u32 = 0x5FA8D8;
const RS: u32 = 0xE0854E;

const ENDPOINT: &str = "/tmp/telebox_host.sock";
const UPSTREAM: &str = "/tmp/tlgrm_mcp.sock";
const TOKEN: &str = "/tmp/auth_token";

struct TeleBox {
    state: HostState,
}

impl TeleBox {
    fn new(cx: &mut Context<Self>) -> Self {
        let state = HostState::new(ENDPOINT.to_string(), UPSTREAM.to_string());
        state.log("host", "TeleBox host starting");
        state.log("host", "loaded 7 plugins · 5 active");

        mcp_relay::spawn(
            state.clone(),
            ENDPOINT.to_string(),
            UPSTREAM.to_string(),
            TOKEN.to_string(),
        );

        // Repaint on a timer so the panel reflects the relay's live state.
        cx.spawn(async move |this, cx| loop {
            cx.background_executor()
                .timer(Duration::from_millis(400))
                .await;
            if this.update(cx, |_this, cx| cx.notify()).is_err() {
                break;
            }
        })
        .detach();

        Self { state }
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

fn live_badge() -> Div {
    div()
        .px_2()
        .py_1()
        .rounded_md()
        .bg(rgb(INSET))
        .border_1()
        .border_color(rgb(LINE))
        .child(mono(LED).text_xs().child("● live"))
}

fn module_card(p: &Plugin) -> Div {
    let led = if p.active { LED } else { INK3 };
    let name_color = if p.active { INK } else { INK3 };

    // header row — the live modules (only MCP, this slice) carry a marker
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
        header = header.child(live_badge());
    }
    header = header.child(div().flex_1()).child(runtime_badge(p.runtime));

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

fn stat(k: &'static str, v: String, color: u32) -> Div {
    div()
        .flex()
        .flex_col()
        .gap_1()
        .child(mono(INK3).text_xs().child(k))
        .child(mono(color).child(v))
}

impl Render for TeleBox {
    fn render(&mut self, _window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
        let (requests, upstream, clients, endpoint, bridge, logs) = {
            let i = self.state.0.lock().unwrap();
            (
                i.requests,
                i.upstream_connected,
                i.clients,
                i.endpoint.clone(),
                i.upstream.clone(),
                i.log.iter().rev().take(9).cloned().collect::<Vec<_>>(),
            )
        };
        let plugins = self.state.plugins();
        let up_label = if upstream { "connected" } else { "—" };
        let up_color = if upstream { LED } else { INK3 };

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
            .child(
                div()
                    .font_weight(FontWeight::BOLD)
                    .text_color(rgb(INK))
                    .child("TELEBOX"),
            )
            .child(mono(INK3).child("· plugin host"));

        // host status bar
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
            .child(dot(LED, 8.))
            .child(
                div()
                    .text_sm()
                    .font_weight(FontWeight::SEMIBOLD)
                    .text_color(rgb(LED))
                    .child("Host running"),
            );

        let hostbar = div()
            .flex()
            .items_center()
            .gap_5()
            .px_4()
            .py_3()
            .bg(rgb(0x1A1C23))
            .border_b_1()
            .border_color(rgb(LINE))
            .child(pill)
            .child(div().flex_1())
            .child(stat("Plugins", "5 active / 7".into(), INK))
            .child(stat("MCP requests", requests.to_string(), ACCENT))
            .child(stat("Upstream", up_label.into(), up_color))
            .child(stat("Clients", clients.to_string(), INK))
            .child(stat("Endpoint", endpoint, INK2))
            .child(stat("Bridge", bridge, INK2));

        // the rack — two modules per row
        let rack = div().flex().flex_col().gap_3().children(plugins.chunks(2).map(|pair| {
            let mut row = div().flex().gap_3();
            for p in pair {
                row = row.child(module_card(p));
            }
            row
        }));

        // activity log — newest first
        let logview = div()
            .flex()
            .flex_col()
            .gap_1()
            .p_3()
            .rounded_md()
            .bg(rgb(PANEL))
            .border_1()
            .border_color(rgb(LINE))
            .children(logs.into_iter().map(|e| {
                div()
                    .flex()
                    .gap_3()
                    .text_xs()
                    .child(mono(INK3).child(e.t))
                    .child(mono(ACCENT).w(px(58.)).child(e.src))
                    .child(mono(INK).child(e.msg))
            }));

        let heading = |t: &'static str| {
            div()
                .text_lg()
                .font_weight(FontWeight::BOLD)
                .text_color(rgb(INK))
                .child(t)
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
                    .flex_col()
                    .flex_1()
                    .gap_3()
                    .p_5()
                    .child(heading("The rack"))
                    .child(rack)
                    .child(div().h(px(4.)))
                    .child(heading("Activity"))
                    .child(logview),
            )
    }
}

fn main() {
    application().run(|cx: &mut App| {
        let bounds = Bounds::centered(None, size(px(1060.), px(820.)), cx);
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
