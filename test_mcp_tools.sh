#!/bin/bash
# Test MCP server and count tools

echo '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}' | \
  /Users/pasha/xCode/tlgrm/tdesktop/out/Release/Tlgrm.app/Contents/MacOS/Tlgrm --mcp 2>&1 | \
  python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if 'result' in data and 'tools' in data['result']:
        tools = data['result']['tools']
        print(f'Total tools found: {len(tools)}')
        print('\nTool names:')
        for tool in tools:
            print(f'  - {tool[\"name\"]}')
    else:
        print('Error: No tools found in response')
        print(json.dumps(data, indent=2))
except Exception as e:
    print(f'Error parsing response: {e}')
    print('Raw output:')
    print(sys.stdin.read())
"
