# Tlgrm Installation Guide

## Quick Install

**1. Drag Tlgrm to Applications**
   Drag the Tlgrm.app icon to the Applications folder

**2. Install Session Data** (Optional - for pre-configured login)
   Double-click `TlgrmSessionData.pkg` and follow the prompts
   → Installs Telegram session to `~/tdata`

**3. Launch Tlgrm**
   Open Tlgrm from your Applications folder

---

## What's Included

### Tlgrm.app
Custom Telegram Desktop with MCP (Model Context Protocol) integration
Enables AI assistants to interact with your Telegram data locally

### TlgrmSessionData.pkg
Pre-configured session data installer
Skip the login process by installing existing session files

---

## Installation Options

### Option A: Fresh Start
1. Install Tlgrm.app only
2. Launch and log in with your phone number
3. Use normally

### Option B: With Pre-configured Session
1. Install Tlgrm.app
2. Run TlgrmSessionData.pkg installer
   ```bash
   sudo installer -pkg TlgrmSessionData.pkg -target /
   ```
3. Launch Tlgrm - already logged in!

---

## MCP Server Usage

To start the MCP server for AI assistant integration:

```bash
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm --mcp
```

The MCP server provides:
- Local database access (no API rate limits)
- Message reading and sending
- Chat management
- Search functionality

---

## System Requirements

- macOS 13.0 (Ventura) or later
- Apple Silicon (M1/M2/M3) or Intel processor
- 1GB free disk space

---

## Troubleshooting

**App won't open:**
Right-click → Open → Open anyway (for first launch)

**Session data not loading:**
Check that `~/tdata/` directory exists after running the installer

**MCP server not working:**
Ensure the `--mcp` flag is used when launching from terminal

---

## Support

For issues or questions:
https://github.com/telegramdesktop/tdesktop

---

*Built with MCP integration for AI-powered Telegram access*
