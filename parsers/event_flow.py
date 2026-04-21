#event_flow.py
import re
from pathlib import Path
from collections import Counter
from parsers.simplifier import clean_line

from settings import config

try:
    import tiktoken
    _enc = tiktoken.get_encoding(getattr(config, "TIKTOKEN_ENCODING", "cl100k_base"))
    def _count_tokens(text: str) -> int:
        return len(_enc.encode(text))
except Exception:
    def _count_tokens(text: str) -> int:
        return max(1, len(text) // 4)


BOOT_PATTERN = re.compile(r"CPU1:\s*0x00000002")
EVT_PATTERN = re.compile(r"\[evt by cpu.*?\]\s*(?:\d+\s*)?(.*)", re.IGNORECASE)

ER3_BOOT_PATTERN = re.compile(r"^Main_0$")
ER3_NRLG_PATTERN = re.compile(r"\[NRLG\]\s+trigger savelog", re.IGNORECASE)
ER3_EVT_PATTERN = re.compile(r"\[SL\]\s+(.*?)\s+->\s+Trigger Savelog", re.IGNORECASE)


def load_logs(folder: str | Path, project: str = "PJ1") -> list[dict]:
    from parsers.filter import resolve_log_folder
    rows: list[dict] = []
    folder = resolve_log_folder(Path(folder))

    if project == "ER3":
        candidates = sorted(
            list(folder.glob("Hs*.log"))
            + list(folder.glob("logNorEx.log"))
            + list(folder.glob("logNor.log"))
        )
    else:
        candidates = [
            f for f in sorted(folder.glob("*.log"))
            if f.name.lower().startswith("hs")
            or "norlog" in f.name.lower()
            or "lognor" in f.name.lower()
        ]

    for f in candidates:
        with open(f, "r", encoding="utf-8", errors="ignore") as fh:
            for i, line in enumerate(fh, start=1):
                rows.append({
                    "file": f.name,
                    "path": str(f),
                    "line_no": i,
                    "text": line.rstrip("\n"),
                })

    return rows


def split_segments(rows: list[dict], project: str = "PJ1") -> list[list[dict]]:
    segments: list[list[dict]] = []
    current: list[dict] = []

    if project == "ER3":
        for row in rows:
            line = row["text"].strip()
            # Hs*.log: segment by Main_0 (boot); logNorEx.log: segment by [NRLG] trigger savelog
            if ER3_BOOT_PATTERN.match(line) or ER3_NRLG_PATTERN.search(line):
                if current:
                    segments.append(current)
                    current = []
            current.append(row)
    else:
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


def extract_events(segment: list[dict], ignore: set[str] | None = None,
                   project: str = "PJ1") -> list[dict]:
    events: list[dict] = []
    _ignore = ignore or set()
    pattern = ER3_EVT_PATTERN if project == "ER3" else EVT_PATTERN

    for row in segment:
        line = row["text"]
        m = pattern.search(line)
        if not m:
            continue

        evt = m.group(1).strip()
        if not evt:
            continue

        if evt.lower() in _ignore:
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


def dedup_seen(events: list[dict]) -> list[dict]:
    seen: set[str] = set()
    out: list[dict] = []
    for e in events:
        if e["event"] not in seen:
            seen.add(e["event"])
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


def build_path_map(log_folder: str | Path, ignore: set[str] | None = None,
                   project: str = "PJ1",
                   single_ignore: set[str] | None = None) -> tuple[Counter, dict, int, int]:
    rows = load_logs(log_folder, project)
    segments = split_segments(rows, project)

    _single_ignore = {s.lower() for s in single_ignore} if single_ignore else set()

    counter: Counter = Counter()
    samples: dict[str, dict] = {}

    for seg in segments:
        events = extract_events(seg, ignore, project)
        events = dedup_consecutive(events)
        events = dedup_seen(events)

        if not events:
            continue

        # 若 segment 只有單一事件且在 single_ignore 清單中，略過
        if len(events) == 1 and events[0]["event"].lower() in _single_ignore:
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


_SNIPPET_DROP_PREFIXES_PJ1 = (
    "pcie_dbglog()",
)

_SNIPPET_DROP_PREFIXES_ER3 = (
    "[SL] Seq. Read:",
    "[SL] Scan :",
    "[SL] Savelog Blk Defect",
    "[SL] Power On Scan",
    "[SL] Last page scan",
    "[SL] Use =>",
    "[SL] Write Log",
    "[SL] Write Nand Done",
    "[SL] Leave SaveLog",
    "[SL] Enter SaveErrLog",
    "[SL] Erase :",
    "[SL] Debug_pool:",
)

_SNIPPET_ORPHAN_RE = re.compile(r"^[0-9A-Fa-f:, ]+,$")

_SNIPPET_DEDUP_PREFIXES_PJ1 = (
    "POH:",
    "PGR Ver",
    "Loader Ver",
    "SN:",
    "Rx Err Cnt",
    "Retrain",
    "Power cycle cnt",
)

_SNIPPET_DEDUP_PREFIXES_ER3 = (
    "[NRLG] trigger savelog",
    "[SL] Event Time",
    "SMARTPwrCylCnt",
    "Accumulative CRC Error",
)


def _build_ranges(events: list[dict], pre: int, post: int) -> list[tuple[int, int]]:
    raw = [(max(1, e["line_no"] - pre), e["line_no"] + post) for e in events]
    raw.sort()
    merged: list[list[int]] = [list(raw[0])]
    for s, e in raw[1:]:
        if s <= merged[-1][1] + 1:
            merged[-1][1] = max(merged[-1][1], e)
        else:
            merged.append([s, e])
    return [(s, e) for s, e in merged]


def _read_snippet(file_path: str, ranges: list[tuple[int, int]],
                  project: str = "PJ1") -> str:
    needed: set[int] = set()
    for s, e in ranges:
        needed.update(range(s, e + 1))
    max_line = max(e for _, e in ranges)

    drop_prefixes = _SNIPPET_DROP_PREFIXES_ER3 if project == "ER3" else _SNIPPET_DROP_PREFIXES_PJ1
    dedup_prefixes = _SNIPPET_DEDUP_PREFIXES_ER3 if project == "ER3" else _SNIPPET_DEDUP_PREFIXES_PJ1
    evt_pattern = ER3_EVT_PATTERN if project == "ER3" else EVT_PATTERN

    out: list[str] = []
    seen_dedup: set[str] = set()
    prev_dropped = False

    with open(file_path, "r", encoding="utf-8", errors="ignore") as fh:
        for i, line in enumerate(fh, start=1):
            if i > max_line:
                break
            if i not in needed:
                continue

            raw = line.rstrip(chr(10))
            cleaned = clean_line(raw)

            if not cleaned:
                prev_dropped = False
                continue

            stripped = cleaned.strip()

            if any(stripped.startswith(p) for p in drop_prefixes):
                prev_dropped = True
                continue

            if prev_dropped and _SNIPPET_ORPHAN_RE.match(stripped):
                continue

            prev_dropped = False

            dedup_key = next(
                (p for p in dedup_prefixes if stripped.startswith(p)), None
            )
            if dedup_key:
                if dedup_key in seen_dedup:
                    continue
                seen_dedup.add(dedup_key)

            if evt_pattern.search(stripped):
                out.append("")
                out.append(cleaned)
                out.append("")
            else:
                out.append(cleaned)

    return chr(10).join(out)


def build_snippet(file_path: str, first_line: int, last_line: int,
                  events: list[dict] | None = None,
                  use_per_event: bool = False,
                  project: str = "PJ1") -> tuple[int, int, str]:
    if use_per_event and events:
        pre = getattr(config, "FLOW_DETAIL_PRE_CONTEXT", 80)
        post = getattr(config, "FLOW_DETAIL_POST_CONTEXT", 20)
        ranges = _build_ranges(events, pre, post)
    else:
        pre = getattr(config, "FLOW_DETAIL_PRE_LINES", 150)
        post = getattr(config, "FLOW_DETAIL_POST_LINES", 10)
        ranges = [(max(1, first_line - pre), last_line + post)]

    snippet = _read_snippet(file_path, ranges, project)
    overall_start = min(s for s, _ in ranges)
    overall_end = max(e for _, e in ranges)
    return overall_start, overall_end, snippet

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


def format_flow_detail(counter: Counter, samples: dict, top_n: int | None = None,
                       use_per_event: bool = False, project: str = "PJ1") -> str:
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

        events = sample.get("events", [])
        start_line, end_line, snippet = build_snippet(file_path, first_line, last_line,
                                                      events=events, use_per_event=use_per_event,
                                                      project=project)

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


def analyze_event_flow(log_folder: str | Path, ignore: set[str] | None = None, top_n: int | None = None) -> str:
    counter, samples, total_lines, total_segments = build_path_map(log_folder, ignore)

    flow_text = format_flow(counter, total_lines, total_segments, top_n=top_n)
    detail_text = format_flow_detail(counter, samples, top_n=top_n)

    return f"{flow_text}\n{detail_text}"


def build_compressed_flow(counter: Counter, samples: dict,
                          total_lines: int, total_segments: int,
                          top_n: int | None = None,
                          project: str = "PJ1") -> str:
    """Build compressed event flow section with two-pass token threshold check.

    Pass 1: primary single-window snippets.
    Pass 2 (if total tokens > FLOW_TOTAL_TOKEN_THRESHOLD): per-event snippets.
    """
    from parsers.compress import process_lines

    threshold = getattr(config, "FLOW_TOTAL_TOKEN_THRESHOLD", None)

    def _build(use_per_event: bool = False, top_n_override: int | None = None) -> str:
        n = top_n_override if top_n_override is not None else top_n
        ft = (
            format_flow(counter, total_lines, total_segments, n)
            + "\n"
            + format_flow_detail(counter, samples, n, use_per_event=use_per_event, project=project)
        )
        return "\n".join(process_lines(ft.splitlines(keepends=True)))

    result = _build(use_per_event=False)
    if not threshold:
        return result

    tokens = _count_tokens(result)
    if tokens <= threshold:
        return result

    # lv1: per-event context
    pre = getattr(config, "FLOW_DETAIL_PRE_CONTEXT", 80)
    post = getattr(config, "FLOW_DETAIL_POST_CONTEXT", 20)
    print(f"  [fallback lv1] flow {tokens:,} > {threshold:,}, per-event PRE={pre}/POST={post}")
    result = _build(use_per_event=True)
    tokens = _count_tokens(result)
    if tokens <= threshold:
        return result

    # lv2: reduce top_n until fits, keep same per-event context
    steps = getattr(config, "FLOW_LV2_TOP_N_STEPS", [5, 3, 1])
    for n in steps:
        print(f"  [fallback lv2] still {tokens:,} > {threshold:,}, top_n={n} [!] reduced context reliability")
        result = _build(use_per_event=True, top_n_override=n)
        tokens = _count_tokens(result)
        if tokens <= threshold:
            warning = (
                f"{'!' * 80}\n"
                f"WARNING: Event detail truncated to top {n} paths (token limit).\n"
                f"         Analysis confidence reduced — some event paths omitted.\n"
                f"{'!' * 80}\n"
            )
            return warning + result

    # last resort: return whatever we have
    return result


if __name__ == "__main__":
    import sys
    from parsers.project_parser import detect_project_from_raw_logs, SEARCH_JSON_MAP
    from parsers.filter import load_settings

    if len(sys.argv) != 2:
        print("Usage: python event_flow.py <log_folder>")
        raise SystemExit(1)

    folder = sys.argv[1]
    _project = detect_project_from_raw_logs(folder)
    _ignore: set[str] = set()
    if _project and _project in SEARCH_JSON_MAP:
        _s = load_settings(SEARCH_JSON_MAP[_project])
        _ignore = {s.lower() for s in _s.get("ignore_event_signatures", [])}
    print(analyze_event_flow(folder, ignore=_ignore))