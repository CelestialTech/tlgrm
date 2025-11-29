#!/usr/bin/env python3
"""
Fix test file to use direct method calls instead of _tool_handlers
"""

import re

# Read the test file
with open('tests/test_mcp_tools.py', 'r') as f:
    content = f.read()

# Define replacement patterns
replacements = [
    # get_messages
    (r'mcp_server_hybrid\.server\._tool_handlers\["get_messages"\]\(',
     r'mcp_server_hybrid.tool_get_messages('),
    (r'mcp_server_standalone\.server\._tool_handlers\["get_messages"\]\(',
     r'mcp_server_standalone.tool_get_messages('),

    # search_messages
    (r'mcp_server_hybrid\.server\._tool_handlers\["search_messages"\]\(',
     r'mcp_server_hybrid.tool_search_messages('),
    (r'mcp_server_standalone\.server\._tool_handlers\["search_messages"\]\(',
     r'mcp_server_standalone.tool_search_messages('),

    # semantic_search
    (r'mcp_server_hybrid\.server\._tool_handlers\["semantic_search"\]\(',
     r'mcp_server_hybrid.tool_semantic_search('),
    (r'mcp_server_standalone\.server\._tool_handlers\["semantic_search"\]\(',
     r'mcp_server_standalone.tool_semantic_search('),
]

# Apply all replacements
for pattern, replacement in replacements:
    content = re.sub(pattern, replacement, content)

# Write back the fixed content
with open('tests/test_mcp_tools.py', 'w') as f:
    f.write(content)

print("✓ Fixed all _tool_handlers references in test_mcp_tools.py")
