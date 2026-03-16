import re

_RE_EVT_PREFIX = re.compile(r"^\[evt by cpu\]:\s*\d+\s*")
_RE_CPU_ADDR_PREFIX = re.compile(r"^CPU\d+:\s*0x[0-9A-Fa-f]+\s*")
_RE_BRACKET_CPU_PREFIX = re.compile(r"^\[[^\]]*\]:\s*\d+\s*CPU\d+:\s*0x[0-9A-Fa-f]+\s*")
_RE_FILE_LINE_PREFIX = re.compile(r"^[^ ]+\.(c|h)\s+\+\d+\s*")
_RE_FUNC_DASH = re.compile(r"([A-Za-z_][\w:]*\(\)\s*-\s*.*)$")

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