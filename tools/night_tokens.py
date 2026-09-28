"""Token use of a night run, per work order, as a Markdown table for the night's review note.

Claude Code writes a transcript per session, <project>/<session>.jsonl, and one per subagent,
<project>/<session>/subagents/agent-*.jsonl with a .meta.json beside it holding the
description it was launched with. A night run implements each order in its own subagent
described "Implement work order NN" (work_orders/README.md), so each order's usage is its
subagent's; the session's own messages are the run's overhead (picking orders, merging,
the second probes). Only messages at or after --since count, so a run launched from a
session that did other work first is measured from its start.

    python tools/night_tokens.py <session-id> --since 2026-09-15T09:26:05Z

<session-id> may be omitted to take the most recently written session of this project.
Four token kinds are reported apart: cache reads are most of the count and cost least,
so one total would mislead.
"""

import argparse
import glob
import json
import os
import re
import sys
from datetime import datetime, timezone

KINDS = [
    ("output_tokens", "Output"),
    ("input_tokens", "Input"),
    ("cache_creation_input_tokens", "Cache write"),
    ("cache_read_input_tokens", "Cache read"),
]


def projectDir():
    """~/.claude/projects/<this repo's path with separators as dashes>."""
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    slug = re.sub(r"[:\\/]", "-", repo)
    return os.path.join(os.path.expanduser("~"), ".claude", "projects", slug)


def parseTime(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


def tally(path, since):
    """Summed usage of the assistant messages in one transcript, at or after since.
    A streamed message is written several times under one id; the last copy has the
    final counts."""
    last = {}
    models = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            msg = rec.get("message")
            if not isinstance(msg, dict) or not msg.get("usage"):
                continue
            ts = rec.get("timestamp")
            if since and ts and parseTime(ts) < since:
                continue
            last[msg.get("id") or len(last)] = msg["usage"]
            if msg.get("model"):
                models.add(msg["model"])
    sums = {k: sum(u.get(k) or 0 for u in last.values()) for k, _ in KINDS}
    return sums, len(last), models


def fmt(n):
    if n >= 1_000_000:
        return f"{n / 1_000_000:.1f}M"
    if n >= 10_000:
        return f"{n / 1000:.0f}k"
    if n >= 1000:
        return f"{n / 1000:.1f}k"
    return str(n)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("session", nargs="?", help="session id (default: the latest)")
    ap.add_argument("--since", help="ISO time the run started, e.g. 2026-09-15T09:26:05Z")
    args = ap.parse_args()

    proj = projectDir()
    if args.session:
        session = args.session
    else:
        files = glob.glob(os.path.join(proj, "*.jsonl"))
        if not files:
            sys.exit(f"no transcripts in {proj}")
        session = os.path.splitext(os.path.basename(max(files, key=os.path.getmtime)))[0]
    main_path = os.path.join(proj, session + ".jsonl")
    if not os.path.exists(main_path):
        sys.exit(f"no transcript {main_path}")
    since = parseTime(args.since) if args.since else None
    if since and since.tzinfo is None:
        since = since.replace(tzinfo=timezone.utc)

    rows = []  # (label, sums, messages)
    models = set()
    for path in sorted(glob.glob(os.path.join(proj, session, "subagents", "*.jsonl"))):
        meta_path = path[: -len(".jsonl")] + ".meta.json"
        desc = ""
        if os.path.exists(meta_path):
            with open(meta_path, encoding="utf-8") as f:
                desc = json.load(f).get("description", "")
        m = re.search(r"work order (\d+)", desc, re.IGNORECASE)
        sums, n, mods = tally(path, since)
        if n == 0:
            continue  # a subagent from before the run started
        models |= mods
        label = f"Order {int(m.group(1)):02d}" if m else f"Other: {desc or os.path.basename(path)}"
        rows.append((label, sums, n))
    rows.sort(key=lambda r: r[0])
    sums, n, mods = tally(main_path, since)
    models |= mods
    rows.append(("Run overhead", sums, n))

    total = {k: sum(r[1][k] for r in rows) for k, _ in KINDS}
    print(f"Session `{session}`" + (f", from {args.since}" if args.since else "")
          + f"; models: {', '.join(sorted(models))}.\n")
    print("| | " + " | ".join(name for _, name in KINDS) + " | Messages |")
    print("|---|" + "---:|" * (len(KINDS) + 1))
    for label, s, cnt in rows:
        print(f"| {label} | " + " | ".join(fmt(s[k]) for k, _ in KINDS) + f" | {cnt} |")
    print("| **Total** | " + " | ".join(f"**{fmt(total[k])}**" for k, _ in KINDS)
          + f" | {sum(r[2] for r in rows)} |")


if __name__ == "__main__":
    main()
