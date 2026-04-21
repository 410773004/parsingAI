#filter.py
import json
import re
from pathlib import Path


def load_settings(search_json):
    with open(search_json, "r", encoding="utf-8") as f:
        return json.load(f)


TIMESTAMP_RE = re.compile(r"^timestamp of cpu\d+:", re.IGNORECASE)

FW_RE = re.compile(r"FW\s+(FG\d+)")
PGR_RE = re.compile(r"PGR Ver\s*:\s*(\S+)")
LDR_RE = re.compile(r"Loader Ver\s*:\s*(\S+)")
NAND_RE = re.compile(r"(\d+CH\*\d+CE\*\d+LUN\*\d+PL)")

ER_FW_RE = re.compile(r"(F2[MN]\d+)\s+FW\s+SubVersion:")
ER_MODEL_RE = re.compile(r"(SSSTC\s+\S+)")


def remove_timestamp(lines):
    out = []
    for line in lines:
        if TIMESTAMP_RE.match(line.strip()):
            continue
        out.append(line)
    return out


def compress_repeated(lines):
    if not lines:
        return lines

    out = []
    prev = lines[0]
    count = 1

    for line in lines[1:]:
        if line == prev:
            count += 1
        else:
            if count > 1:
                out.append(f"{prev} (repeated {count} times)")
            else:
                out.append(prev)
            prev = line
            count = 1

    if count > 1:
        out.append(f"{prev} (repeated {count} times)")
    else:
        out.append(prev)

    return out


def parse_device_info(lines, project="PJ1"):
    fw = None
    pgr = None
    ldr = None
    nand = None
    model = None

    for line in lines:
        if project == "ER3":
            if fw is None:
                m = ER_FW_RE.search(line)
                if m:
                    fw = m.group(1)
            if model is None:
                m = ER_MODEL_RE.search(line)
                if m:
                    model = m.group(1)
        else:
            if fw is None:
                m = FW_RE.search(line)
                if m:
                    fw = m.group(1)
            if pgr is None:
                m = PGR_RE.search(line)
                if m:
                    pgr = m.group(1)
            if ldr is None:
                m = LDR_RE.search(line)
                if m:
                    ldr = m.group(1)
            if nand is None:
                m = NAND_RE.search(line)
                if m:
                    nand = m.group(1)

    return fw, pgr, ldr, nand, model


def extract_event_blocks(lines, keyword, context):
    keyword = keyword.lower()
    blocks = []

    for i, line in enumerate(lines):
        if keyword in line.lower():
            start = max(0, i - context)
            block = lines[start:i + 1]
            blocks.append(block)

    return blocks


_ER3_SIG_RE = re.compile(r"\[SL\]\s+(.*?)\s+->\s+Trigger Savelog", re.IGNORECASE)
_ER3_NRLG_RE = re.compile(r"\[NRLG\].*trigger savelog", re.IGNORECASE)


def get_event_signature(block, project="PJ1"):
    for line in reversed(block):
        s = line.strip()
        lower = s.lower()

        if project == "ER3":
            # Skip NRLG entries (log record markers, not real error events)
            if _ER3_NRLG_RE.search(s):
                continue
            m = _ER3_SIG_RE.search(s)
            if m:
                return m.group(1).strip()
        else:
            if "[evt by cpu" in lower:
                pos = s.find("]")
                if pos != -1:
                    return s[pos + 1:].strip()
                return s.strip()

    return block[-1].strip() if block else "unknown event"


def cluster_event_blocks(blocks, ignore_sigs: set[str], project="PJ1"):
    clusters = {}

    for block in blocks:
        sig = get_event_signature(block, project)

        # Skip empty/unknown signatures and NRLG records
        if not sig or sig == "unknown event":
            continue
        if sig.strip().lower() in ignore_sigs:
            continue
        # For ER3: skip if signature still contains [SL] or [NRLG] (unmatched)
        if project == "ER3" and (sig.startswith("[SL]") or sig.startswith("[NRLG]")):
            continue

        if sig not in clusters:
            clusters[sig] = {
                "count": 1,
                "sample": block,
            }
        else:
            clusters[sig]["count"] += 1

    return clusters


def resolve_log_folder(folder: Path) -> Path:
    """Return the effective log folder that directly contains Hs*.log files.

    If *folder* itself has no Hs*.log, walk subdirectories (rglob) and return
    the parent of the first Hs*.log found.  Falls back to *folder* unchanged
    so callers can still return their own 'no log found' message.
    """
    if list(folder.glob("Hs*.log")):
        return folder
    for hit in sorted(folder.rglob("Hs*.log")):
        return hit.parent
    return folder


def load_smart(folder):
    for name in ("smart_info.txt", "SMARTInfo.txt"):
        p = Path(folder) / name
        if p.exists():
            return p.read_text(encoding="utf-8", errors="ignore").strip()
    return ""


def run_filter(settings, log_folder, project="PJ1"):
    log_folder = resolve_log_folder(Path(log_folder))

    if project == "ER3":
        log_files = sorted(
            list(log_folder.glob("Hs*.log"))
            + list(log_folder.glob("logNorEx.log"))
            + list(log_folder.glob("logNor.log"))
        )
    else:
        log_files = sorted(log_folder.glob("*.log"))

    if not log_files:
        return "No log file found."

    all_lines = []
    for lf in log_files:
        all_lines.extend(lf.read_text(errors="ignore").splitlines())

    all_lines = remove_timestamp(all_lines)

    fw, pgr, ldr, nand, model = parse_device_info(all_lines, project)

    keywords = settings.get("keywords", [])
    global_context = int(settings.get("context_lines", 20) or 20)
    ignore_sigs = {s.lower() for s in settings.get("ignore_event_signatures", [])}

    all_blocks = []

    for item in keywords:
        if not item.get("enabled"):
            continue
        if item.get("type") != "error":
            continue

        keyword = item.get("keyword", "")
        context = int(item.get("lines_to_capture", global_context) or global_context)

        blocks = extract_event_blocks(all_lines, keyword, context)
        all_blocks.extend(blocks)

    processed_blocks = []
    for block in all_blocks:
        block = compress_repeated(block)
        processed_blocks.append(block)

    clusters = cluster_event_blocks(processed_blocks, ignore_sigs, project)
    sorted_clusters = sorted(
        clusters.items(),
        key=lambda x: x[1]["count"],
        reverse=True
    )

    smart = load_smart(log_folder)

    sep = "=" * 80

    out = []

    out.append(sep)
    out.append("DEVICE INFO")
    out.append(sep)
    if model:
        out.append(f"Model            : {model}")
    if fw:
        out.append(f"Firmware Version : {fw}")
    if pgr:
        out.append(f"PGR Version      : {pgr}")
    if ldr:
        out.append(f"Loader Version   : {ldr}")
    if nand:
        out.append(f"NAND Config      : {nand}")
    out.append("")

    if smart:
        out.append(sep)
        out.append("SMART INFO")
        out.append(sep)
        out.append(smart)
        out.append("")

    out.append(sep)
    out.append("EVENT SUMMARY")
    out.append(sep)
    for sig, info in sorted_clusters:
        out.append(f"{sig} : {info['count']}")
    out.append("")

    return "\n".join(out)
