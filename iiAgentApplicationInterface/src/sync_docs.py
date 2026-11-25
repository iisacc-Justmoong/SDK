#!/usr/bin/env python3
"""Project-local, lossless Markdown-to-JSON projection with source hashes."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DOCUMENTS = (
    ("README.md", "docs/entrypoints/README.agent.json", "overview", "en"),
    ("AGENT.md", "docs/entrypoints/AGENT.agent.json", "instructions", "ko"),
    ("docs/Contract.md", "docs/Contract.agent.json", "contract", "ko"),
)


def digest(data):
    """Return a reproducible SHA-256 digest for unmodified source bytes."""
    return hashlib.sha256(data).hexdigest()


def encode(value):
    """Serialize a projection deterministically as human-readable UTF-8 JSON."""
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def projection(source, role, language):
    """Capture every source line and heading without inventing new instructions."""
    data = (ROOT / source).read_bytes()
    lines = data.decode("utf-8").splitlines(keepends=True)
    entries = []
    headings = []
    fenced = False
    for number, line in enumerate(lines, 1):
        stripped = line.rstrip("\r\n")
        match = re.match(r"^(#{1,6})\s+(.+)$", stripped) if not fenced else None
        if stripped.startswith("```"):
            kind = "fence"
            fenced = not fenced
        elif fenced:
            kind = "code"
        elif match:
            kind = "heading"
            headings.append({"line": number, "level": len(match.group(1)), "title": match.group(2)})
        else:
            kind = "blank" if not stripped else "text"
        entries.append({"line": number, "kind": kind, "raw": line})
    return {"format": "iiAgentApplicationInterface.document-projection.v1", "role": role,
            "source": {"path": source, "sha256": digest(data), "bytes": len(data),
                       "line_count": len(lines), "language": language, "encoding": "utf-8"},
            "headings": headings, "lines": entries}


def main():
    """Write or check all known projections and the source-index together."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="report stale or missing projections")
    args = parser.parse_args()
    expected = {}
    index = {"format": "iiAgentApplicationInterface.document-index.v1", "project": "SDK/iiAgentApplicationInterface", "documents": []}
    for source, destination, role, language in DOCUMENTS:
        doc = projection(source, role, language)
        expected[destination] = encode(doc)
        index["documents"].append({"source": source, "agent_json": destination, "role": role,
                                   "sha256": doc["source"]["sha256"]})
    expected["docs/agent-index.json"] = encode(index)
    stale = []
    for destination, data in expected.items():
        path = ROOT / destination
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                stale.append(destination)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    if stale:
        print("Stale or missing document projections: " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
