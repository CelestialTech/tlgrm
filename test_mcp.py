#!/usr/bin/env python3
"""Test script for Telegram MCP integration"""

import subprocess
import json
import sys

def send_mcp_request(method, params=None):
    """Send a JSON-RPC request to Telegram MCP server"""
    request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": method,
        "params": params or {}
    }

    print(f"→ Sending: {method}")
    request_json = json.dumps(request) + "\n"

    # Launch Telegram with MCP enabled
    proc = subprocess.Popen(
        ["out/Release/Telegram.app/Contents/MacOS/Telegram", "--mcp"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # Send request
    stdout, stderr = proc.communicate(input=request_json, timeout=5)

    print(f"← Response: {stdout}")
    if stderr:
        print(f"⚠ Stderr: {stderr}", file=sys.stderr)

    return stdout

# Test sequence
print("Testing Telegram MCP Integration\n")
print("=" * 50)

# Test 1: Initialize
print("\n1. Testing initialize...")
send_mcp_request("initialize", {
    "protocolVersion": "1.0",
    "clientInfo": {
        "name": "test-client",
        "version": "1.0.0"
    }
})

# Test 2: List tools
print("\n2. Testing tools/list...")
send_mcp_request("tools/list")

print("\n" + "=" * 50)
print("Test complete!")
