// The MCP tool taxonomy — 14 functionality domains → 58 subdomains → 362 tools.
//
// Built once from the embedded mcp_taxonomy.json (verified: every tool placed
// exactly once). This is the structure the MCP device renders as a navigable
// tree, instead of a flat 362-row dump.

use std::sync::OnceLock;

pub struct Domain {
    pub name: String,
    pub count: usize,
    pub subs: Vec<Sub>,
}
pub struct Sub {
    pub name: String,
    pub tools: Vec<String>,
}

static TAX: OnceLock<Vec<Domain>> = OnceLock::new();

pub fn domains() -> &'static Vec<Domain> {
    TAX.get_or_init(|| {
        let raw = include_str!("mcp_taxonomy.json");
        let v: serde_json::Value = serde_json::from_str(raw).unwrap_or(serde_json::Value::Null);
        v.get("domains")
            .and_then(|d| d.as_array())
            .map(|arr| {
                arr.iter()
                    .map(|dom| Domain {
                        name: dom.get("name").and_then(|x| x.as_str()).unwrap_or("").to_string(),
                        count: dom.get("tool_count").and_then(|x| x.as_u64()).unwrap_or(0) as usize,
                        subs: dom
                            .get("subdomains")
                            .and_then(|s| s.as_array())
                            .map(|sa| {
                                sa.iter()
                                    .map(|sub| Sub {
                                        name: sub.get("name").and_then(|x| x.as_str()).unwrap_or("").to_string(),
                                        tools: sub
                                            .get("tools")
                                            .and_then(|t| t.as_array())
                                            .map(|ta| ta.iter().filter_map(|x| x.as_str().map(String::from)).collect())
                                            .unwrap_or_default(),
                                    })
                                    .collect()
                            })
                            .unwrap_or_default(),
                    })
                    .collect()
            })
            .unwrap_or_default()
    })
}

pub fn total_tools() -> usize {
    domains().iter().map(|d| d.count).sum()
}
