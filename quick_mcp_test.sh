#!/bin/bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./out/Release/Tlgrm.app/Contents/MacOS/Tlgrm --mcp 2>&1
