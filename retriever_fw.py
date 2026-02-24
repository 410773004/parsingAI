# retriever_fw.py
import re
from pathlib import Path
from dataclasses import dataclass
from typing import Dict, List, Optional

# 抓：path/to/file.c +1234 func()
PAT = re.compile(r"(?P<file>[\w./-]+\.c)\s+\+(?P<line>\d+)\s+(?P<func>\w+)\(\)")
# 補抓：path/to/file.c +1234
PAT2 = re.compile(r"(?P<file>[\w./-]+\.c)\s+\+(?P<line>\d+)")

@dataclass(frozen=True)
class Hit:
    file: str
    line: int
    func: Optional[str] = None

@dataclass
class Span:
    start: int
    end: int
    reasons: List[str]

def read_text(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="ignore")

def normalize_path(s: str) -> str:
    return s.replace("\\", "/").strip()

def extract_hits(text: str) -> List[Hit]:
    hits: List[Hit] = []
    for m in PAT.finditer(text):
        hits.append(Hit(normalize_path(m.group("file")), int(m.group("line")), m.group("func")))

    known = {(h.file, h.line) for h in hits}
    for m in PAT2.finditer(text):
        key = (normalize_path(m.group("file")), int(m.group("line")))
        if key in known:
            continue
        hits.append(Hit(normalize_path(m.group("file")), int(m.group("line")), None))
    return hits

def file_candidates(fw_root: Path, rel: str) -> List[Path]:
    """
    先用相對路徑找；找不到就用檔名在 repo 內 rglob（避免 log path 跟 repo 結構不完全一致）
    """
    rel = normalize_path(rel)
    p = fw_root / rel
    if p.exists():
        return [p]
    name = Path(rel).name
    return list(fw_root.rglob(name))

def clamp(a: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, a))

def build_spans_for_file(lines_count: int, hits: List[Hit], context: int) -> List[Span]:
    spans: List[Span] = []
    for h in hits:
        start = clamp(h.line - context, 1, lines_count)
        end = clamp(h.line + context, 1, lines_count)
        reason = f"{h.file}+{h.line}" + (f" {h.func}()" if h.func else "")
        spans.append(Span(start, end, [reason]))

    # 合併重疊/相鄰 spans，避免重複貼 code
    spans.sort(key=lambda s: (s.start, s.end))
    merged: List[Span] = []
    for s in spans:
        if not merged:
            merged.append(s)
            continue
        last = merged[-1]
        if s.start <= last.end + 5:
            last.end = max(last.end, s.end)
            last.reasons.extend(s.reasons)
        else:
            merged.append(s)
    return merged

def extract_snippet(file_path: Path, start: int, end: int) -> str:
    lines = read_text(file_path).splitlines()
    start0 = max(0, start - 1)
    end0 = min(len(lines), end)
    return "\n".join(lines[start0:end0])

def generate_related_code_md(
    fw_root: Path,
    parsing_txt: Path,
    out_md: Path,
    context_lines: int = 80,
    max_files: int = 20,
    max_snips_per_file: int = 8,
    pick_first_candidate_only: bool = True,
) -> Path:
    """
    讀 parsing_txt → 抽 (file,line,func) → 去重 → 對每個檔案抓片段(合併重疊) → 輸出 md
    """
    text = read_text(parsing_txt)
    hits = extract_hits(text)

    # 去重：同 (file,line,func) 視為同一個 hit
    uniq_hits = list({(h.file, h.line, h.func): h for h in hits}.values())

    # 依檔案分組
    by_file: Dict[str, List[Hit]] = {}
    for h in uniq_hits:
        by_file.setdefault(h.file, []).append(h)

    # 依命中數排序，只挑前 max_files 個檔案（避免太大）
    files_sorted = sorted(by_file.keys(), key=lambda f: len(by_file[f]), reverse=True)[:max_files]

    out_lines: List[str] = []
    out_lines.append("# Related Firmware Code (deduped)\n")
    out_lines.append(f"- FW_ROOT: `{fw_root}`")
    out_lines.append(f"- INPUT: `{parsing_txt}`")
    out_lines.append(f"- Context: ±{context_lines} lines\n")

    for rel_file in files_sorted:
        hits_for_file = sorted(by_file[rel_file], key=lambda h: h.line)[:max_snips_per_file]
        cands = file_candidates(fw_root, rel_file)

        out_lines.append(f"## {rel_file}\n")

        if not cands:
            out_lines.append("> ⚠️ Not found in firmware repo.\n")
            continue

        chosen = cands[:1] if pick_first_candidate_only else cands
        for fp in chosen:
            src_lines = read_text(fp).splitlines()
            spans = build_spans_for_file(len(src_lines), hits_for_file, context_lines)

            out_lines.append(f"- Resolved path: `{fp}`")
            out_lines.append(f"- Hits: {len(hits_for_file)} (deduped), Snippets: {len(spans)} (merged)\n")

            for i, sp in enumerate(spans, 1):
                reasons = ", ".join(sorted(set(sp.reasons)))
                out_lines.append(f"### Snippet {i}: L{sp.start}-L{sp.end}")
                out_lines.append(f"- Reasons: {reasons}\n")
                out_lines.append("```c")
                out_lines.append(extract_snippet(fp, sp.start, sp.end))
                out_lines.append("```\n")

            # 如果只挑第一個候選，就結束
            if pick_first_candidate_only:
                break

    out_md.write_text("\n".join(out_lines), encoding="utf-8")
    return out_md