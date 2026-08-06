use crate::config::PLATFORM_ASSET_PREFIXES;

// ---------------------------------------------------------------------------
// GitHub JSON -> Telegram-format JSON
// ---------------------------------------------------------------------------

/// Transform a GitHub Releases API response body into the Telegram update
/// metadata format expected by the desktop client.
///
/// Output shape:
/// ```json
/// {
///   "armac": { "stable": { "released": "6009007", "link": "/tarmacupd6009007" } },
///   "mac":   { "stable": { "released": "6009007", "link": "/tmacupd6009007"   } }
/// }
/// ```
pub fn transform_github_to_telegram(github_body: &[u8]) -> Result<String, String> {
    let release: serde_json::Value =
        serde_json::from_slice(github_body).map_err(|e| format!("parse error: {}", e))?;

    let tag = release["tag_name"]
        .as_str()
        .ok_or_else(|| "missing tag_name".to_string())?;
    let version_int = tag_to_version_int(tag)?;
    let version_str = version_int.to_string();

    let assets = release["assets"]
        .as_array()
        .ok_or_else(|| "missing assets".to_string())?;

    // Locate platform-specific assets by prefix.
    let find_asset = |prefix: &str| -> Option<String> {
        assets.iter().find_map(|a| {
            let name = a["name"].as_str()?;
            if name.starts_with(prefix) {
                Some(format!("/{}", name))
            } else {
                None
            }
        })
    };

    let armac_link = find_asset(PLATFORM_ASSET_PREFIXES[0])
        .unwrap_or_else(|| {
            log::warn!("No asset with prefix '{}' found, synthesising default link", PLATFORM_ASSET_PREFIXES[0]);
            format!("/{}{}", PLATFORM_ASSET_PREFIXES[0], version_str)
        });

    let mac_link = find_asset(PLATFORM_ASSET_PREFIXES[1])
        .unwrap_or_else(|| {
            log::warn!("No asset with prefix '{}' found, synthesising default link", PLATFORM_ASSET_PREFIXES[1]);
            format!("/{}{}", PLATFORM_ASSET_PREFIXES[1], version_str)
        });

    let telegram = serde_json::json!({
        "armac": {
            "stable": {
                "released": &version_str,
                "link": &armac_link
            }
        },
        "mac": {
            "stable": {
                "released": &version_str,
                "link": &mac_link
            }
        }
    });

    serde_json::to_string(&telegram).map_err(|e| format!("serialize error: {}", e))
}

// ---------------------------------------------------------------------------
// Version conversion helpers
// ---------------------------------------------------------------------------

/// Convert a semver tag (e.g. `"v6.9.7"`) to a Telegram-style version integer
/// (`6009007`).  The formula is `major * 1_000_000 + minor * 1_000 + patch`.
pub fn tag_to_version_int(tag: &str) -> Result<u64, String> {
    let tag = tag.strip_prefix('v').unwrap_or(tag);
    let parts: Vec<&str> = tag.split('.').collect();
    if parts.len() != 3 {
        return Err(format!("bad tag format: {}", tag));
    }
    let major: u64 = parts[0].parse().map_err(|_| format!("bad major in tag: {}", tag))?;
    let minor: u64 = parts[1].parse().map_err(|_| format!("bad minor in tag: {}", tag))?;
    let patch: u64 = parts[2].parse().map_err(|_| format!("bad patch in tag: {}", tag))?;
    Ok(major * 1_000_000 + minor * 1_000 + patch)
}

/// Convert a Telegram-style version integer (e.g. `6009007`) back to a semver
/// tag string (`"v6.9.7"`).
pub fn version_int_to_tag(v: u64) -> String {
    format!(
        "v{}.{}.{}",
        v / 1_000_000,
        (v % 1_000_000) / 1_000,
        v % 1_000
    )
}

/// Determine whether `path` corresponds to an update binary and, if so, return
/// the filename portion (e.g. `"/tarmacupd6009007"` -> `"tarmacupd6009007"`).
pub fn extract_update_filename(path: &str) -> Option<&str> {
    let name = path.strip_prefix('/')?;
    for prefix in PLATFORM_ASSET_PREFIXES {
        if name.starts_with(prefix) {
            return Some(name);
        }
    }
    None
}

/// Extract the numeric version integer from an update filename (e.g.
/// `"tarmacupd6009007"` -> `6009007`).
pub fn extract_version_int(filename: &str) -> u64 {
    filename
        .chars()
        .skip_while(|c| !c.is_ascii_digit())
        .collect::<String>()
        .parse()
        .unwrap_or(0)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_tag_to_version_int() {
        assert_eq!(tag_to_version_int("v6.9.7").unwrap(), 6_009_007);
        assert_eq!(tag_to_version_int("6.9.7").unwrap(), 6_009_007);
        assert_eq!(tag_to_version_int("v1.0.0").unwrap(), 1_000_000);
        assert!(tag_to_version_int("v1.0").is_err());
    }

    #[test]
    fn test_version_int_to_tag() {
        assert_eq!(version_int_to_tag(6_009_007), "v6.9.7");
        assert_eq!(version_int_to_tag(1_000_000), "v1.0.0");
    }

    #[test]
    fn test_extract_update_filename() {
        assert_eq!(
            extract_update_filename("/tarmacupd6009007"),
            Some("tarmacupd6009007")
        );
        assert_eq!(
            extract_update_filename("/tmacupd6009007"),
            Some("tmacupd6009007")
        );
        assert_eq!(extract_update_filename("/current4"), None);
        assert_eq!(extract_update_filename("/health"), None);
    }

    #[test]
    fn test_extract_version_int() {
        assert_eq!(extract_version_int("tarmacupd6009007"), 6_009_007);
        assert_eq!(extract_version_int("tmacupd6009007"), 6_009_007);
        assert_eq!(extract_version_int("nodigits"), 0);
    }

    #[test]
    fn test_transform_github_to_telegram() {
        let github_json = serde_json::json!({
            "tag_name": "v6.9.7",
            "assets": [
                { "name": "tarmacupd6009007", "browser_download_url": "https://..." },
                { "name": "tmacupd6009007", "browser_download_url": "https://..." },
                { "name": "Tlgrm_6.9.7.dmg", "browser_download_url": "https://..." }
            ]
        });
        let body = serde_json::to_vec(&github_json).unwrap();
        let result = transform_github_to_telegram(&body).unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();

        assert_eq!(parsed["armac"]["stable"]["released"], "6009007");
        assert_eq!(parsed["armac"]["stable"]["link"], "/tarmacupd6009007");
        assert_eq!(parsed["mac"]["stable"]["released"], "6009007");
        assert_eq!(parsed["mac"]["stable"]["link"], "/tmacupd6009007");
    }
}
