// The TeleBox plugin interface.
//
// This is the *code* contract behind the rack. Until now the rack was rendered
// from data templates (`host::plugin_templates`) and only the MCP relay was
// wired — there was no interface a plugin implemented. This module defines it,
// grounded in the seven-family Host API (docs/M1_HOST_API.md): a plugin is
// hosted by TeleBox and drives the Tlgrm client through capability families it
// declares, and TeleBox hands it a `HostApi` handle scoped to those families so
// the permissions matrix is enforceable.
//
// Relocating each feature (Export, Retention, Archiver, …) to a real `Plugin`
// impl is P2/M2–M5 (PROPOSAL_REFACTOR.md). This file is the seam that makes
// that possible; `McpPlugin` below is the first concrete implementer.
#![allow(dead_code)] // interface ahead of its M2 wiring — kept honest, not stubbed

use std::sync::Arc;

/// One of the seven Host-API capability families a plugin may use — exactly the
/// families the permissions matrix grants (`host::FAMILIES`).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Capability {
    Session,  // who am I / connection state
    Invoke,   // raw MTProto passthrough — the keystone primitive
    Model,    // read the local data model (dialogs, history, peers)
    Settings, // read/write client settings
    Files,    // download / produce media & files
    Ui,       // drive the client UI (open chat, compose, notify)
    Events,   // subscribe to client events (new message, edit, delete)
}

impl Capability {
    pub fn label(self) -> &'static str {
        match self {
            Capability::Session => "session",
            Capability::Invoke => "invoke",
            Capability::Model => "model",
            Capability::Settings => "settings",
            Capability::Files => "files",
            Capability::Ui => "ui",
            Capability::Events => "events",
        }
    }
}

/// A host-side failure surfaced to a plugin — the REAL error, never a fabricated
/// success (the governing rule: a tool reports what happened).
#[derive(Debug, Clone)]
pub struct HostError(pub String);

/// An opaque handle to an events-family subscription.
pub struct Subscription(pub u64);

/// The surface a plugin calls into. TeleBox proxies each call to the client's
/// Host API — today the client's MCP bridge, post-M1 the client's native
/// HostApi socket. Params and results are JSON-encoded strings so the interface
/// carries no serialization dependency of its own. A plugin only ever receives
/// a handle scoped to the capabilities it declared.
pub trait HostApi: Send + Sync {
    /// `invoke` family — raw MTProto passthrough: a TL method name + JSON params
    /// in, the real server result (or error) out. Export→disk is a loop over this.
    fn invoke(&self, method: &str, params_json: &str) -> Result<String, HostError>;

    /// `model` family — read the local data model, e.g. `model("history", …)`.
    fn model(&self, query: &str, params_json: &str) -> Result<String, HostError>;

    /// `events` family — subscribe to a client event kind
    /// (`new_message` | `edit` | `delete` | `media_expiring`).
    fn subscribe(&self, kind: &str) -> Result<Subscription, HostError>;
}

/// A hosted plugin. TeleBox loads it, toggles it on/off from the rack, and hands
/// `on_enable` a `HostApi` handle scoped to `capabilities()`.
pub trait Plugin: Send {
    /// Stable id, e.g. `"mcp"`, `"export"`, `"archiver"`.
    fn id(&self) -> &'static str;

    /// The Host-API families this plugin is granted — drives the permissions
    /// matrix and scopes the `HostApi` handle it receives on enable.
    fn capabilities(&self) -> &'static [Capability];

    /// Toggled on: capture the host handle and begin work. Returns the real
    /// error if it cannot start.
    fn on_enable(&mut self, host: Arc<dyn HostApi>) -> Result<(), HostError>;

    /// Toggled off: tear down cleanly.
    fn on_disable(&mut self);
}

/// The MCP plugin — the one family wired today. It re-hosts the client's
/// aggregated MCP surface (the relay, owned by `HostState`). This impl is the
/// seam where M2 moves that ownership behind the trait; its declared
/// capabilities match the MCP card in the rack.
pub struct McpPlugin {
    enabled: bool,
    host: Option<Arc<dyn HostApi>>,
}

impl McpPlugin {
    pub fn new() -> Self {
        Self { enabled: false, host: None }
    }
    pub fn is_enabled(&self) -> bool {
        self.enabled
    }
}

impl Default for McpPlugin {
    fn default() -> Self {
        Self::new()
    }
}

impl Plugin for McpPlugin {
    fn id(&self) -> &'static str {
        "mcp"
    }
    fn capabilities(&self) -> &'static [Capability] {
        &[
            Capability::Session,
            Capability::Invoke,
            Capability::Files,
            Capability::Settings,
            Capability::Events,
        ]
    }
    fn on_enable(&mut self, host: Arc<dyn HostApi>) -> Result<(), HostError> {
        self.host = Some(host);
        self.enabled = true;
        Ok(())
    }
    fn on_disable(&mut self) {
        self.enabled = false;
        self.host = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // A trivial HostApi so the interface is exercised, not just declared.
    struct NullHost;
    impl HostApi for NullHost {
        fn invoke(&self, _m: &str, _p: &str) -> Result<String, HostError> {
            Ok("{}".into())
        }
        fn model(&self, _q: &str, _p: &str) -> Result<String, HostError> {
            Ok("{}".into())
        }
        fn subscribe(&self, _k: &str) -> Result<Subscription, HostError> {
            Ok(Subscription(0))
        }
    }

    #[test]
    fn mcp_plugin_lifecycle() {
        let mut p = McpPlugin::new();
        assert_eq!(p.id(), "mcp");
        assert!(p.capabilities().contains(&Capability::Invoke));
        assert!(!p.is_enabled());
        p.on_enable(Arc::new(NullHost)).unwrap();
        assert!(p.is_enabled());
        p.on_disable();
        assert!(!p.is_enabled());
    }
}
