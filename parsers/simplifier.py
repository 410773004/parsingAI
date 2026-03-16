import re
from typing import Any


_RE_EVT_PREFIX = re.compile(r"^\[evt by cpu\]:\s*\d+\s*")
_RE_CPU_ADDR_PREFIX = re.compile(r"^CPU\d+:\s*0x[0-9A-Fa-f]+\s*")
_RE_BRACKET_CPU_PREFIX = re.compile(r"^\[[^\]]*\]:\s*\d+\s*CPU\d+:\s*0x[0-9A-Fa-f]+\s*")
_RE_FILE_LINE_PREFIX = re.compile(r"^[^ ]+\.(c|h)\s+\+\d+\s*")
_RE_FUNC_DASH = re.compile(r"([A-Za-z_]\w*\(\)\s*-\s*.*)$")


def normalize_block(text: str) -> str:
    text = text.strip().lower()
    text = re.sub(r"\s+", " ", text)
    return text


def clean_line(line: str) -> str:
    s = line.rstrip("\n")

    s = _RE_EVT_PREFIX.sub("", s)
    s = _RE_CPU_ADDR_PREFIX.sub("", s)
    s = _RE_BRACKET_CPU_PREFIX.sub("", s)
    s = _RE_FILE_LINE_PREFIX.sub("", s)

    m = _RE_FUNC_DASH.search(s)
    if m:
        s = m.group(1).strip()

    return s.strip()


def simplify_results(results: list[dict[str, Any]]) -> str:
    out: list[str] = []
    seen_blocks: set[str] = set()

    for item in results:
        lines = item["lines"]

        if not lines:
            continue

        if len(lines) == 1 and "not found." in lines[0]:
            continue

        cleaned_lines: list[str] = []

        for ln in lines:
            cleaned = clean_line(ln)
            if cleaned:
                cleaned_lines.append(cleaned)

        if not cleaned_lines:
            continue

        block_text = "\n".join(cleaned_lines)
        norm = normalize_block(block_text)

        if norm in seen_blocks:
            continue

        seen_blocks.add(norm)

        out.append(f"===== FILE: {item['file']} | {item['kind']} | {item['keyword']} =====")
        out.extend(cleaned_lines)
        out.append("")

    return "\n".join(out).strip() + "\n"