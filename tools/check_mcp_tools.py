#!/usr/bin/env -S uv run --quiet --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Cross-check the four places every MCP tool must be declared.

    tools/check_mcp_tools.py            # report and exit non-zero on any defect
    tools/check_mcp_tools.py --quiet    # print only failures (for the build)

A tool has to appear in four places, none of which the compiler relates:

  1. mcp_tool_registry.cpp        the advertised Tool{} entry (tools/list)
  2. mcp_server.h                 the method declaration
  3. mcp_server_complete.cpp      the _toolHandlers binding (what is callable)
  4. mcp_tool_backing.h           what data source backs it

The server already checks parts of this at startup -- VerifyToolBackings, plus
the constructor's duplicate and unbacked checks. That is the right idea at the
wrong time: it fires on whoever happens to launch a build, having already
shipped. Every defect this script looks for was found by hand-written scripts
after the fact, never by the compiler:

  - 7 tools advertised twice
  - 8 callable but never advertised
  - 155 arguments read but never declared, unreachable to a caller
  - 72 arguments declared but never read, so following the schema did nothing
  - 22 parameters with no description

Run from anywhere; paths are resolved from this file's location.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

MCP = (Path(__file__).resolve().parent.parent
       / "tdesktop/Telegram/SourceFiles/mcp")

# Type of an argument, inferred from how the implementation consumes it. Order
# matters: toVariant().toLongLong() has to be tested before toInt().
CONSUMERS = [
    (r"\.toBool\s*\(", "boolean"),
    (r"\.toVariant\s*\(\s*\)\s*\.\s*toLongLong\s*\(", "integer"),
    (r"\.toInt\s*\(", "integer"),
    (r"\.toDouble\s*\(", "number"),
    (r"\.toArray\s*\(", "array"),
    (r"\.toObject\s*\(", "object"),
    (r"\.toString\s*\(", "string"),
]


def balanced(text: str, start: int) -> int:
    """Index of the brace closing the one at `start`, or -1."""
    depth = 0
    i = start
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def sources() -> dict[str, str]:
    return {p.name: p.read_text(errors="replace")
            for p in sorted(MCP.glob("*.cpp")) + sorted(MCP.glob("*.h"))}


# --- the four sites -----------------------------------------------------------

def advertised(registry: str) -> dict[str, dict]:
    """Tools from tools/list, in both shapes the registry uses.

    Most are `Tool{"name", "desc", QJsonObject{...}}` literals; the
    deleted-account tools are built imperatively as `Tool tool; tool.name = ...`
    with properties assigned one at a time. A parser that only knows the first
    shape silently reports the second as unregistered -- which is exactly the
    wrong answer, since they are advertised at runtime.
    """
    found: dict[str, dict] = {}

    for match in re.finditer(r"\bTool\s*\{", registry):
        i = registry.index("{", match.end() - 1)
        j = balanced(registry, i)
        if j < 0:
            continue
        entry = registry[i:j + 1]
        name = re.search(r'"\s*([a-z0-9_]+)\s*"', entry)
        if not name:
            continue
        props: dict[str, dict] = {}
        pm = re.search(r'\{\s*"properties"\s*,\s*QJsonObject\s*\{', entry)
        if pm:
            pi = entry.index("{", pm.end() - 1)
            pj = balanced(entry, pi)
            props = properties_of(entry[pi:pj + 1])
        required = []
        rm = re.search(r'\{\s*"required"\s*,\s*QJsonArray\s*\{([^}]*)\}', entry)
        if rm:
            required = re.findall(r'"([a-z0-9_]+)"', rm.group(1))
        found.setdefault(name.group(1), {"properties": props,
                                         "required": required,
                                         "duplicate": False})
        if name.group(1) in found and props and not found[name.group(1)]["properties"]:
            found[name.group(1)]["properties"] = props

    seen: set[str] = set()
    for match in re.finditer(r"\bTool\s*\{", registry):
        i = registry.index("{", match.end() - 1)
        j = balanced(registry, i)
        entry = registry[i:j + 1] if j > 0 else ""
        name = re.search(r'"\s*([a-z0-9_]+)\s*"', entry)
        if not name:
            continue
        if name.group(1) in seen:
            found[name.group(1)]["duplicate"] = True
        seen.add(name.group(1))

    for match in re.finditer(r"\{\s*Tool\s+tool\s*;", registry):
        i = registry.rindex("{", 0, match.end())
        j = balanced(registry, i)
        if j < 0:
            continue
        block = registry[i:j + 1]
        name = re.search(r'tool\.name\s*=\s*"([a-z0-9_]+)"', block)
        if not name:
            continue
        props = {k: {"described": True}
                 for k in re.findall(
                     r'properties\s*\[\s*"([a-zA-Z_][a-zA-Z0-9_]*)"\s*\]\s*=',
                     block)}
        found.setdefault(name.group(1), {"properties": props,
                                         "required": [], "duplicate": False})
    return found


def properties_of(block: str) -> dict[str, dict]:
    """Property names at depth 1, and whether each carries a description.

    `items` is a JSON Schema keyword describing array elements, not a
    parameter, so it is skipped -- flagging it as undescribed would be wrong.
    """
    props: dict[str, dict] = {}
    depth = 0
    i = 0
    while i < len(block):
        if block[i] == "{":
            depth += 1
            if depth == 2:
                m = re.match(r'\{\s*"([a-zA-Z_][a-zA-Z0-9_]*)"\s*,', block[i:])
                end = balanced(block, i)
                if m and end > 0:
                    if m.group(1) != "items":
                        props[m.group(1)] = {
                            "described": '"description"' in block[i:end + 1]}
                    i = end
                    depth -= 1
        elif block[i] == "}":
            depth -= 1
        i += 1
    return props


def bindings(server_complete: str) -> dict[str, str]:
    """tool name -> handler method, from initializeToolHandlers()."""
    return dict(re.findall(
        r'_toolHandlers\["([a-z0-9_]+)"\]\s*=\s*\[this\]\([^)]*\)\s*\{\s*'
        r"return\s+(tool[A-Za-z0-9_]+)\(", server_complete))


def backings(backing_header: str) -> tuple[dict[str, str], list[str]]:
    """The backing table, plus any names that break its sorted order.

    ToolBackingFor() binary-searches the table, so an out-of-order entry is not
    a style question: the lookup silently misses and the tool is refused.
    """
    entries = re.findall(r'\{"([a-z0-9_]+)",\s*Backing::(\w+)\s*\}',
                         backing_header)
    table = {name: backing for name, backing in entries}
    names = [name for name, _ in entries]
    unsorted = [b for a, b in zip(names, names[1:]) if b < a]
    return table, unsorted


def declared_methods(server_header: str) -> set[str]:
    return set(re.findall(r"QJsonObject\s+(tool[A-Za-z0-9_]+)\s*\(",
                          server_header))


# --- implementations ----------------------------------------------------------

def implementations(srcs: dict[str, str]) -> dict[str, tuple[str, str]]:
    """method -> (args parameter name, body), for every tool implementation."""
    out: dict[str, tuple[str, str]] = {}
    for text in srcs.values():
        for m in re.finditer(
                r"QJsonObject\s+Server::(tool[A-Za-z0-9_]+)\s*\(\s*"
                r"const\s+QJsonObject\s*&\s*(\w+)\s*\)\s*\{", text):
            i = text.index("{", m.end() - 1)
            j = balanced(text, i)
            if j > 0:
                out[m.group(1)] = (m.group(2), text[i:j + 1])
    return out


def helpers(srcs: dict[str, str]) -> dict[str, tuple[str, str]]:
    """Any function taking a QJsonObject, so one level can be inlined.

    Several tools read chat_id through resolveCommunity(args) rather than
    directly. Without following that, a first pass reported 23 required
    arguments as never read -- all false, and dropping them would have broken
    working tools.
    """
    out: dict[str, tuple[str, str]] = {}
    for text in srcs.values():
        for m in re.finditer(
                r"\b\w+\s*(?:Server::)?(\w+)\s*\(\s*const\s+QJsonObject"
                r"\s*&\s*(\w+)\s*\)\s*\{", text):
            i = text.index("{", m.end() - 1)
            j = balanced(text, i)
            if j > 0:
                out.setdefault(m.group(1), (m.group(2), text[i:j + 1]))
    return out


def args_read(param: str, body: str) -> dict[str, str]:
    """Argument names the body reads, with the type its use implies."""
    found: dict[str, str] = {}
    pattern = re.compile(
        re.escape(param)
        + r'\s*(?:\.value\s*\(\s*)?\[?\s*"([a-zA-Z_][a-zA-Z0-9_]*)"\s*\)?\]?')
    for m in pattern.finditer(body):
        tail = body[m.end():m.end() + 60]
        kind = "string"
        for rx, name in CONSUMERS:
            if re.match(r"\s*\)?\s*" + rx, tail):
                kind = name
                break
        found.setdefault(m.group(1), kind)
    return found


def reads_for(method: str, impls, helps) -> dict[str, str]:
    if method not in impls:
        return {}
    param, body = impls[method]
    found = dict(args_read(param, body))
    for callee in set(re.findall(
            r"\b([A-Za-z_]\w*)\s*\(\s*" + re.escape(param) + r"\s*[,)]", body)):
        if callee in helps and callee != method:
            hparam, hbody = helps[callee]
            for key, kind in args_read(hparam, hbody).items():
                found.setdefault(key, kind)
    return found


# --- the check ----------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--quiet", action="store_true",
                    help="print only failures")
    args = ap.parse_args()

    srcs = sources()
    for needed in ("mcp_tool_registry.cpp", "mcp_server_complete.cpp",
                   "mcp_tool_backing.h", "mcp_server.h"):
        if needed not in srcs:
            print(f"check_mcp_tools: {needed} not found under {MCP}",
                  file=sys.stderr)
            return 2

    ads = advertised(srcs["mcp_tool_registry.cpp"])
    binds = bindings(srcs["mcp_server_complete.cpp"])
    table, unsorted = backings(srcs["mcp_tool_backing.h"])
    methods = declared_methods(srcs["mcp_server.h"])
    impls = implementations(srcs)
    helps = helpers(srcs)

    problems: list[str] = []

    for name in sorted(set(binds) - set(ads)):
        problems.append(f"{name}: callable but never advertised — no client "
                        f"can discover it")
    for name in sorted(set(ads) - set(binds)):
        problems.append(f"{name}: advertised but not bound in "
                        f"initializeToolHandlers — calling it fails")
    for name in sorted(n for n, a in ads.items() if a["duplicate"]):
        problems.append(f"{name}: advertised more than once — clients see a "
                        f"repeated entry in tools/list")
    for name in sorted(set(binds) - set(table)):
        problems.append(f"{name}: no entry in kToolBackings — it will be "
                        f"refused as unimplemented")
    for name in sorted(unsorted):
        problems.append(f"{name}: breaks kToolBackings sort order — "
                        f"ToolBackingFor binary-searches it and will miss")
    for method in sorted({m for m in binds.values()} - methods):
        problems.append(f"{method}: bound but not declared in mcp_server.h")

    for name in sorted(set(ads) & set(binds)):
        method = binds[name]
        reads = reads_for(method, impls, helps)
        props = ads[name]["properties"]
        body = impls.get(method, ("", ""))[1]

        for arg in sorted(set(reads) - set(props)):
            problems.append(f"{name}.{arg}: read by {method} but not "
                            f"advertised — unreachable to a caller that "
                            f"trusts tools/list")
        # Only claim an advertised argument is unread when its literal appears
        # nowhere in the implementation. Anything subtler is ambiguous, and a
        # false positive here deletes a working parameter.
        for arg in sorted(set(props) - set(reads)):
            if f'"{arg}"' not in body:
                problems.append(f"{name}.{arg}: advertised but never read — "
                                f"following the schema has no effect")
        for arg in ads[name]["required"]:
            if arg not in reads and f'"{arg}"' not in body:
                problems.append(f"{name}.{arg}: required but never read — "
                                f"callers are forced to pass something ignored")
        for arg, info in sorted(props.items()):
            if not info.get("described"):
                problems.append(f"{name}.{arg}: no description — the type "
                                f"alone does not say what to pass")

    if problems:
        print(f"check_mcp_tools: {len(problems)} problem(s)\n", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        print("\nEvery MCP tool must agree across mcp_tool_registry.cpp, "
              "mcp_server.h,\nmcp_server_complete.cpp and mcp_tool_backing.h. "
              "See AGENTS.md.", file=sys.stderr)
        return 1

    if not args.quiet:
        described = sum(len(a["properties"]) for a in ads.values())
        print(f"check_mcp_tools: {len(ads)} tools, {described} parameters, "
              f"four declaration sites agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
