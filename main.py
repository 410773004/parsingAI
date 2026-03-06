# main.py
from __future__ import annotations

import json
import re
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog
from pathlib import Path
from typing import Any

import ollama

import config
import prompts


# =============================
# UI
# =============================
def choose_folder() -> Path | None:
    """用視窗選資料夾（不走 CLI）。"""
    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)
    folder = filedialog.askdirectory(title="選擇包含 Hs*.log 的資料夾")
    root.destroy()
    return Path(folder) if folder else None


def ask_issue() -> str | None:
    """用視窗輸入『客戶回報問題』。"""
    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)

    issue = simpledialog.askstring(
        title="客戶回報問題",
        prompt="請輸入客戶回報問題（必填）：",
        parent=root,
    )
    root.destroy()

    if issue is None:
        return None

    issue = issue.strip()
    if not issue:
        return ""
    return issue


# =============================
# Find logs / merge
# =============================
def find_logs(folder: Path) -> list[Path]:
    """
    找出資料夾內所有符合 pattern 的 log（預設抓 Hs*.log）
    會遞迴子資料夾。
    """
    patterns = getattr(config, "LOG_PATTERNS", ["Hs*.log"])
    logs: list[Path] = []
    for pat in patterns:
        logs.extend([p for p in folder.rglob(pat) if p.is_file()])
    # 去重 + 排序
    logs = sorted({p.resolve() for p in logs})
    return logs


def merge_logs(files: list[Path]) -> str:
    """
    把多個 log 合併成一個字串（加上檔名分隔，方便回溯）。
    注意：這裡只負責合併，不做清洗/挑行，避免換行被破壞。
    """
    parts: list[str] = []
    for f in files:
        parts.append(f"===== FILE: {f.name} =====\n")
        try:
            text = f.read_text(encoding="utf-8", errors="ignore")
        except Exception as e:
            text = f"[READ_FAIL] {f} : {e}\n"
        parts.append(text)
        if not text.endswith("\n"):
            parts.append("\n")
        parts.append("\n")
    return "".join(parts)


# =============================
# Stage 0: load search_string.json
# =============================
def load_search_keywords(json_path: Path) -> dict[str, Any]:
    with json_path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    kws = data.get("keywords", [])
    # enabled 預設 True
    kws = [k for k in kws if k.get("enabled", True)]
    data["keywords"] = kws
    return data


# =============================
# Stage 1: pick key lines by search_string.json logic
# =============================
def pick_key_lines_by_search_json(merged_text: str, search_cfg: dict[str, Any]) -> str:
    """
    依照 search_string.json 的 keywords 做「擷取」：
    - type=info/error: substring match + lines_to_capture
    - type=regex: regex match（只抓該行）
    - keyword=powercycle_count: 特例，計數 'LJ1(SSSTC) FW' 出現次數，輸出一行摘要

    會保留 FILE header，並且維持原始換行（keepends）。
    """
    keywords: list[dict[str, Any]] = search_cfg.get("keywords", [])

    # 先把 merged 切成「檔案區塊」來做（這樣 powercycle_count 才能 per file）
    # 區塊格式：
    # ===== FILE: xxx.log =====\n
    # <content...>
    file_header_re = re.compile(r"^===== FILE: (.+?) =====\n", re.MULTILINE)

    # 找出每個 header 的位置
    matches = list(file_header_re.finditer(merged_text))
    if not matches:
        # 沒 header 就當單一檔案處理
        return _pick_in_one_block("UNKNOWN.log", merged_text, keywords)

    out_parts: list[str] = []
    for i, m in enumerate(matches):
        fname = m.group(1)
        start = m.start()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(merged_text)
        block = merged_text[start:end]

        # block 第一行就是 header，先拆出 content
        # 保留 header 原樣
        header_line_end = block.find("\n") + 1
        header = block[:header_line_end]
        content = block[header_line_end:]

        picked_block = _pick_in_one_block(fname, content, keywords)
        out_parts.append(header)
        out_parts.append(picked_block)
        if not picked_block.endswith("\n"):
            out_parts.append("\n")
        out_parts.append("\n")

    return "".join(out_parts)


def _pick_in_one_block(fname: str, content: str, keywords: list[dict[str, Any]]) -> str:
    lines = content.splitlines(keepends=True)

    # 蒐集要輸出的 line index
    keep = [False] * len(lines)

    # powercycle_count 特例：count 'LJ1(SSSTC) FW'
    # （沿用你舊版邏輯：看到 'LJ1(SSSTC) FW' 就 +1）
    powercycle_enabled = any(k.get("keyword") == "powercycle_count" for k in keywords)
    if powercycle_enabled:
        cnt = 0
        for ln in lines:
            if "LJ1(SSSTC) FW" in ln:
                cnt += 1
        # 先把摘要行記下，最後 append
        powercycle_line = f"[powercycle_count]: {cnt} time(s)\n"
    else:
        powercycle_line = ""

    # 逐 keyword 擷取
    for k in keywords:
        ktype = k.get("type", "").lower()
        keyword = str(k.get("keyword", ""))
        lines_to_capture = int(k.get("lines_to_capture", 1) or 1)

        if keyword == "powercycle_count":
            # 已處理
            continue

        if ktype == "regex":
            pattern = k.get("regex", "")
            if not pattern:
                continue
            rx = re.compile(pattern)
            for i, ln in enumerate(lines):
                if rx.search(ln):
                    keep[i] = True
            continue

        # info / error：substring match
        if not keyword:
            continue

        if keyword == "evt by cpu":
            # 沿用你舊版的 evt 擷取邏輯：
            # - 若該行含 panic：往下抓 lines_to_capture 行（含該行）
            # - 否則：抓「往上」lines_to_capture 行（含該行）
            for i, ln in enumerate(lines):
                if keyword in ln:
                    if "panic" in ln:
                        s = i
                        e = min(len(lines), i + lines_to_capture)
                    else:
                        s = max(0, i - lines_to_capture + 1)
                        e = i + 1
                    for j in range(s, e):
                        keep[j] = True
            continue

        # 一般 keyword：往下抓 N 行（含該行）
        for i, ln in enumerate(lines):
            if keyword in ln:
                s = i
                e = min(len(lines), i + lines_to_capture)
                for j in range(s, e):
                    keep[j] = True

    # 組回輸出
    out: list[str] = []
    for i, ln in enumerate(lines):
        if keep[i]:
            out.append(ln)

    if powercycle_line:
        # 尾端補一行摘要（避免插進中間破壞上下文）
        out.append(powercycle_line)

    text = "".join(out)
    if text and not text.endswith("\n"):
        text += "\n"
    return text


# =============================
# Stage 2: Clean / simplify lines
# =============================
_RE_EVT_PREFIX = re.compile(r"^\[evt by cpu\]:\s*\d+\s*")
_RE_CPU_ADDR_PREFIX = re.compile(r"^CPU\d+:\s*0x[0-9a-fA-F]+\s*")
_RE_EVT_INLINE = re.compile(r"\[evt by cpu\d+\]\s*", re.IGNORECASE)

# 例：[nand_detect() - Read ID]: 148 CPU: 0x0000006A ncl_20/nand.c +694 nand_detect() - Read ID ch0 ce0:
_RE_BRACKET_CPU_PREFIX = re.compile(r"^\[[^\]]*\]:\s*\d+\s*CPU:\s*0x[0-9a-fA-F]+\s*")

# 抓最後一段「func() - ...」
_RE_FUNC_DASH = re.compile(r"([A-Za-z_]\w*\(\)\s*-\s*.*)$")


def clean_one_line(line: str) -> str:
    """
    - 刪：[evt by cpu]: 121415
    - 刪：CPU1: 0x0000000B
    - 刪：行內 [evt by cpu3]
    - 刪：開頭 [xxx]: 148 CPU: 0x....
    - 若存在 'func() - ...' → 只保留最後那段（你要的 nand_detect() - Read ID ch0 ce0:）
    """
    ln = line.rstrip("\n")
    if ln == "":
        return ""

    s = ln
    s = _RE_EVT_PREFIX.sub("", s)
    s = _RE_CPU_ADDR_PREFIX.sub("", s)
    s = _RE_EVT_INLINE.sub("", s)
    s = _RE_BRACKET_CPU_PREFIX.sub("", s)

    m = _RE_FUNC_DASH.search(s)
    if m:
        s = m.group(1).strip()

    return s


def filter_merged_log(text: str) -> str:
    """清洗入口：維持換行（keepends），避免換行不見。"""
    out: list[str] = []
    for ln in text.splitlines(keepends=True):
        # 檔名分隔線不動（回溯用）
        if ln.startswith("===== FILE: "):
            out.append(ln)
            continue

        cleaned = clean_one_line(ln)
        if ln.endswith("\n"):
            out.append(cleaned + "\n")
        else:
            out.append(cleaned)

    result = "".join(out)
    if result and not result.endswith("\n"):
        result += "\n"
    return result


# =============================
# Token count (optional)
# =============================
def count_tokens(text: str) -> int:
    """
    優先用 tiktoken（如果你有裝），沒有就用粗估（字元/4）。
    注意：這只是估算，不等於 qwen 的真實 token。
    """
    try:
        import tiktoken  # type: ignore

        enc = tiktoken.get_encoding(getattr(config, "TIKTOKEN_ENCODING", "cl100k_base"))
        return len(enc.encode(text))
    except Exception:
        return max(1, len(text) // 4)


# =============================
# Ollama helpers
# =============================
def stream_chat(messages: list[dict]) -> str:
    resp = ollama.chat(
        model=config.MODEL,
        messages=messages,
        stream=True,
        options={"temperature": config.TEMPERATURE},
    )
    full = ""
    for chunk in resp:
        content = chunk.get("message", {}).get("content", "")
        if content:
            print(content, end="", flush=True)
            full += content
    return full


def try_parse_json(text: str) -> dict | None:
    """ollama 偶爾會多吐字；嘗試抽出最外層 JSON。"""
    text = text.strip()
    try:
        return json.loads(text)
    except Exception:
        pass

    l = text.find("{")
    r = text.rfind("}")
    if l != -1 and r != -1 and r > l:
        candidate = text[l : r + 1]
        try:
            return json.loads(candidate)
        except Exception:
            return None
    return None


# =============================
# main
# =============================
def main():
    folder = choose_folder()
    if folder is None:
        messagebox.showinfo("取消", "未選擇資料夾，程式結束。")
        return

    issue = ask_issue()
    if issue is None:
        messagebox.showinfo("取消", "未輸入客戶回報問題，程式結束。")
        return
    if issue == "":
        messagebox.showerror("缺少輸入", "客戶回報問題為必填。")
        return

    logs = find_logs(folder)
    if not logs:
        messagebox.showerror("找不到檔案", f"在此資料夾找不到 log：\n{folder}")
        return

    # 1) 合併 raw
    merged_raw = merge_logs(logs)

    # 2) Stage1：依 search_string.json 挑關鍵行（含 lines_to_capture/regex）
    search_json_path = Path(getattr(config, "SEARCH_JSON", "search_string.json"))
    if not search_json_path.is_absolute():
        # 以 main.py 所在資料夾為基準
        search_json_path = Path(__file__).resolve().parent / search_json_path

    if not search_json_path.exists():
        messagebox.showerror("缺少設定檔", f"找不到 search_string.json：\n{search_json_path}")
        return

    search_cfg = load_search_keywords(search_json_path)
    picked = pick_key_lines_by_search_json(merged_raw, search_cfg)

    # 3) Stage2：清洗（砍 prefix、抽 func() - ...）
    cleaned = filter_merged_log(picked)

    # 4) token（用 cleaned 算，不要用 merged_raw）
    tok = count_tokens(cleaned)
    print(f"\nToken count (after Stage1+Stage2): {tok}\n")

    # 5) 存檔（方便你驗證）
    picked_path = folder / getattr(config, "PICKED_LOG", "picked.log")
    merged_path = folder / getattr(config, "MERGED_LOG", "merged.log")
    picked_path.write_text(picked, encoding="utf-8", errors="ignore")
    merged_path.write_text(cleaned, encoding="utf-8", errors="ignore")

    # 6) 丟給模型（用 issue + cleaned）
    user_prompt = prompts.USER_TEMPLATE.format(issue=issue, log_text=cleaned)
    messages = [
        {"role": "system", "content": prompts.SYSTEM},
        {"role": "user", "content": user_prompt},
    ]

    print("\n--- Ollama parsing start ---\n")
    reply = stream_chat(messages)
    print("\n\n--- Ollama parsing end ---\n")

    data = try_parse_json(reply)
    out_path = folder / getattr(config, "OUTPUT_JSON", "result.json")

    if data is None:
        out_path.write_text(reply, encoding="utf-8", errors="ignore")
        messagebox.showwarning(
            "輸出不是純 JSON",
            f"模型輸出可能夾雜文字，已原樣存檔：\n{out_path}\n\n"
            f"Stage1(挑選後)：\n{picked_path}\n"
            f"Stage2(清洗後)：\n{merged_path}\n"
            f"Token(after clean)：{tok}"
        )
    else:
        out_path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        messagebox.showinfo(
            "完成",
            f"已完成解析並輸出：\n{out_path}\n\n"
            f"Stage1(挑選後)：\n{picked_path}\n"
            f"Stage2(清洗後)：\n{merged_path}\n"
            f"Token(after clean)：{tok}"
        )


if __name__ == "__main__":
    main()