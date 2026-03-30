#event_flow.py
import re
from pathlib import Path
from collections import Counter
from parsers.simplifier import clean_line

import config


BOOT_PATTERN = re.compile(r"CPU1:\s*0x00000002")
EVT_PATTERN = re.compile(r"\[evt by cpu.*?\]\s*(?:\d+\s*)?(.*)", re.IGNORECASE)

IGNORE_EVENTS = {
    "plp handle finished",
    "pcie link error",
}


def load_logs(folder: str | Path) -> list[dict]:
    rows: list[dict] = []

    for f in sorted(Path(folder).glob("*.log")):
        name = f.name.lower()

        if not (
            name.startswith("hs")
            or "norlog" in name
            or "lognor" in name
        ):
            continue

        with open(f, "r", encoding="utf-8", errors="ignore") as fh:
            for i, line in enumerate(fh, start=1):
                rows.append({
                    "file": f.name,
                    "path": str(f),
                    "line_no": i,
                    "text": line.rstrip("\n"),
                })

    return rows


def split_segments(rows: list[dict]) -> list[list[dict]]:
    segments: list[list[dict]] = []
    current: list[dict] = []

    for row in rows:
        line = row["text"]

        if BOOT_PATTERN.search(line):
            if current:
                segments.append(current)
                current = []

        current.append(row)

    if current:
        segments.append(current)

    return segments


def extract_events(segment: list[dict]) -> list[dict]:
    events: list[dict] = []

    for row in segment:
        line = row["text"]
        m = EVT_PATTERN.search(line)
        if not m:
            continue

        evt = m.group(1).strip()
        if not evt:
            continue

        if evt.lower() in IGNORE_EVENTS:
            continue

        events.append({
            "event": evt,
            "file": row["file"],
            "path": row["path"],
            "line_no": row["line_no"],
            "raw": row["text"],
        })

    return events


def dedup_consecutive(events: list[dict]) -> list[dict]:
    if not events:
        return events

    out = [events[0]]

    for e in events[1:]:
        if e["event"] != out[-1]["event"]:
            out.append(e)

    return out


def build_path(events: list[dict]) -> str | None:
    if not events:
        return None
    return " → ".join(e["event"] for e in events)


def wrap_path(path: str, per_line: int | None = None) -> str:
    if per_line is None:
        per_line = getattr(config, "EVENTS_PER_LINE", 5)

    parts = [p.strip() for p in path.split("→")]
    parts = [p for p in parts if p]

    if not parts:
        return path

    lines: list[str] = []
    for i in range(0, len(parts), per_line):
        chunk = parts[i:i + per_line]
        if i == 0:
            lines.append(" → ".join(chunk))
        else:
            lines.append("  → " + " → ".join(chunk))

    return "\n".join(lines)


def build_path_map(log_folder: str | Path) -> tuple[Counter, dict, int, int]:
    rows = load_logs(log_folder)
    segments = split_segments(rows)

    counter: Counter = Counter()
    samples: dict[str, dict] = {}

    for seg in segments:
        events = extract_events(seg)
        events = dedup_consecutive(events)

        if not events:
            continue

        path = build_path(events)
        if not path:
            continue

        counter[path] += 1

        # 每種 path 只留第一個 representative sample
        if path not in samples:
            samples[path] = {
                "file": events[0]["file"],
                "path": events[0]["path"],
                "first_line": events[0]["line_no"],
                "last_line": events[-1]["line_no"],
                "events": events,
            }

    return counter, samples, len(rows), len(segments)


def build_snippet(file_path: str, first_line: int, last_line: int) -> tuple[int, int, str]:
    pre_lines = getattr(config, "FLOW_DETAIL_PRE_LINES", 200)

    start_line = max(1, first_line - pre_lines)
    end_line = last_line

    out: list[str] = []
    with open(file_path, "r", encoding="utf-8", errors="ignore") as fh:
        for i, line in enumerate(fh, start=1):
            if i < start_line:
                continue
            if i > end_line:
                break

            raw = line.rstrip("\n")
            cleaned = clean_line(raw)

            # 清完變空就跳過
            if not cleaned:
                continue

            out.append(cleaned)

    return start_line, end_line, "\n".join(out)


def format_flow(counter: Counter, total_lines: int, total_segments: int, top_n: int | None = None) -> str:
    if top_n is None:
        top_n = getattr(config, "EVENT_FLOW_TOP_N", 20)

    sep = "=" * 80
    out: list[str] = []

    out.append(sep)
    out.append("EVENT FLOW")
    out.append(sep)
    out.append(f"Total lines    : {total_lines}")
    out.append(f"Total segments : {total_segments}")
    out.append(f"Unique paths   : {len(counter)}")
    out.append("")

    for path, cnt in counter.most_common(top_n):
        wrapped = wrap_path(path)
        out.append(f"[{cnt}] {wrapped}")
        out.append("")

    if not counter:
        out.append("No event flow found.")

    return "\n".join(out).strip() + "\n"


def format_flow_detail(counter: Counter, samples: dict, top_n: int | None = None) -> str:
    if top_n is None:
        top_n = getattr(config, "EVENT_FLOW_TOP_N", 20)

    sep = "=" * 80
    out: list[str] = []

    out.append(sep)
    out.append("EVENT DETAIL")
    out.append(sep)

    if not counter:
        out.append("No event detail found.")
        return "\n".join(out).strip() + "\n"

    for path, cnt in counter.most_common(top_n):
        sample = samples[path]

        file_name = sample["file"]
        file_path = sample["path"]
        first_line = sample["first_line"]
        last_line = sample["last_line"]

        start_line, end_line, snippet = build_snippet(file_path, first_line, last_line)

        out.append("-" * 80)
        out.append(f"FLOW : {path}")
        out.append(f"COUNT: {cnt}")
        out.append(f"SOURCE FILE : {file_name}")
        total = end_line - start_line + 1
        out.append(f"LINE RANGE  : {start_line} ~ {end_line} ({total} lines)")
        out.append("-" * 80)
        out.append(snippet)
        out.append("")

    return "\n".join(out).strip() + "\n"


def analyze_event_flow(log_folder: str | Path, top_n: int | None = None) -> str:
    counter, samples, total_lines, total_segments = build_path_map(log_folder)

    flow_text = format_flow(counter, total_lines, total_segments, top_n=top_n)
    detail_text = format_flow_detail(counter, samples, top_n=top_n)

    return f"{flow_text}\n{detail_text}"


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 2:
        print("Usage: python event_flow.py <log_folder>")
        raise SystemExit(1)

    folder = sys.argv[1]
    print(analyze_event_flow(folder))