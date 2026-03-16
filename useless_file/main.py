from __future__ import annotations

import json
import re
import sqlite3
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog
from datetime import datetime
from pathlib import Path
from typing import Any

import ollama
import config
import prompts


# =============================
# DB
# =============================

DB_PATH = Path("../data/fa_dataset.db")

SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS cases (
    case_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    serial       TEXT NOT NULL UNIQUE,
    model        TEXT NOT NULL,
    fw_version   TEXT,
    cleaned_log  TEXT NOT NULL,
    llm_output   TEXT,
    ground_truth TEXT,
    created_at   TEXT NOT NULL,
    updated_at   TEXT NOT NULL
);
"""


def now_iso():
    return datetime.now().isoformat(timespec="seconds")


def connect_db():
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH))
    conn.execute("PRAGMA journal_mode=WAL;")
    return conn


def init_db():
    conn = connect_db()
    conn.executescript(SCHEMA_SQL)
    conn.commit()
    conn.close()


def upsert_case_to_db(serial, model, fw_version, cleaned_log, llm_output):

    now = now_iso()
    conn = connect_db()

    row = conn.execute(
        "SELECT case_id FROM cases WHERE serial=?",
        (serial,)
    ).fetchone()

    if row is None:

        conn.execute(
            """
            INSERT INTO cases(
                serial, model, fw_version,
                cleaned_log, llm_output,
                ground_truth, created_at, updated_at
            )
            VALUES (?, ?, ?, ?, ?, '', ?, ?)
            """,
            (serial, model, fw_version, cleaned_log, llm_output, now, now)
        )

        conn.commit()
        case_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0]

        conn.close()
        return "inserted", case_id

    else:

        case_id = row[0]

        conn.execute(
            """
            UPDATE cases
            SET model=?,
                fw_version=?,
                cleaned_log=?,
                llm_output=?,
                updated_at=?
            WHERE serial=?
            """,
            (model, fw_version, cleaned_log, llm_output, now, serial)
        )

        conn.commit()
        conn.close()

        return "updated", case_id


# =============================
# UI
# =============================

def ask_input_mode():

    root = tk.Tk()
    root.withdraw()

    mode = simpledialog.askstring(
        "輸入模式",
        "1 = 選資料夾 (raw log)\n2 = 選檔案 (cleaned log)"
    )

    root.destroy()

    if mode not in ("1", "2"):
        return None

    return mode


def ask_case_metadata():

    root = tk.Tk()
    root.withdraw()

    serial = simpledialog.askstring("SN", "Serial Number")
    issue = simpledialog.askstring("Issue", "Customer issue")
    model = simpledialog.askstring("Model", "Model (PJ / CX3 / ER)")
    fw = simpledialog.askstring("FW", "FW version")

    root.destroy()

    if not serial or not issue or not model:
        return None

    fw = fw if fw else ""

    return serial.strip(), issue.strip(), model.strip(), fw.strip()


def choose_folder():

    root = tk.Tk()
    root.withdraw()

    folder = filedialog.askdirectory(title="選擇 log folder")

    root.destroy()

    return Path(folder) if folder else None


def choose_file():

    root = tk.Tk()
    root.withdraw()

    file = filedialog.askopenfilename(title="選擇 cleaned log")

    root.destroy()

    return Path(file) if file else None


# =============================
# LOG
# =============================

def find_logs(folder):

    patterns = getattr(config, "LOG_PATTERNS", ["Hs*.log"])

    logs = []

    for pat in patterns:
        logs.extend(folder.rglob(pat))

    return sorted(set(logs))


def merge_logs(files):

    parts = []

    for f in files:

        parts.append(f"===== FILE: {f.name} =====\n")

        try:
            parts.append(f.read_text(errors="ignore"))
        except:
            parts.append("[READ_FAIL]\n")

        parts.append("\n")

    return "".join(parts)


# =============================
# SEARCH JSON
# =============================

def load_search_keywords(path):

    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    kws = [k for k in data.get("keywords", []) if k.get("enabled", True)]

    data["keywords"] = kws

    return data


# =============================
# PICK
# =============================

def pick_key_lines_by_search_json(text, cfg):

    keywords = cfg.get("keywords", [])

    lines = text.splitlines(keepends=True)

    keep = [False] * len(lines)

    for k in keywords:

        kw = k.get("keyword")

        cap = int(k.get("lines_to_capture", 1))

        for i, ln in enumerate(lines):

            if kw in ln:

                s = i
                e = min(len(lines), i + cap)

                for j in range(s, e):
                    keep[j] = True

    out = []

    for i, ln in enumerate(lines):
        if keep[i]:
            out.append(ln)

    return "".join(out)


# =============================
# CLEAN
# =============================

_evt = re.compile(r"^\[evt by cpu\]:\s*\d+\s*")
_cpu = re.compile(r"^CPU\d+:\s*0x[0-9a-fA-F]+\s*")


def clean_one_line(line):

    s = line.rstrip("\n")

    s = _evt.sub("", s)
    s = _cpu.sub("", s)

    return s


def filter_merged_log(text):

    out = []

    for ln in text.splitlines():

        cleaned = clean_one_line(ln)

        if cleaned:
            out.append(cleaned)

    return "\n".join(out)


# =============================
# TOKEN
# =============================

def count_tokens(text):

    try:
        import tiktoken
        enc = tiktoken.get_encoding("cl100k_base")
        return len(enc.encode(text))
    except:
        return len(text) // 4


# =============================
# LLM
# =============================

def stream_chat(messages):

    resp = ollama.chat(
        model=config.MODEL,
        messages=messages,
        stream=True,
        options={"temperature": config.TEMPERATURE},
    )

    out = ""

    for chunk in resp:

        content = chunk.get("message", {}).get("content", "")

        if content:
            print(content, end="", flush=True)
            out += content

    return out


# =============================
# MAIN
# =============================

def main():

    init_db()

    mode = ask_input_mode()

    if mode is None:
        return

    meta = ask_case_metadata()

    if meta is None:
        messagebox.showerror("Error", "Missing SN/Issue/Model")
        return

    serial, issue, model, fw_version = meta

    if mode == "1":

        folder = choose_folder()

        if folder is None:
            return

        logs = find_logs(folder)

        merged_raw = merge_logs(logs)

        search_json = Path(config.SEARCH_JSON)

        cfg = load_search_keywords(search_json)

        picked = pick_key_lines_by_search_json(merged_raw, cfg)

        cleaned = filter_merged_log(picked)

        out_dir = folder

    else:

        file = choose_file()

        if file is None:
            return

        cleaned = file.read_text(errors="ignore")

        out_dir = file.parent

    tok = count_tokens(cleaned)

    print("\nToken:", tok)

    prompt = prompts.USER_TEMPLATE.format(
        model=model,
        fw_version=fw_version,
        issue=issue,
        log_text=cleaned,
    )

    messages = [
        {"role": "system", "content": prompts.SYSTEM},
        {"role": "user", "content": prompt},
    ]

    print("\n--- LLM start ---\n")

    reply = stream_chat(messages)

    print("\n--- LLM end ---\n")

    out = out_dir / "result.json"

    out.write_text(reply, encoding="utf-8")

    if messagebox.askyesno("Save", "Save to DB?"):

        action, cid = upsert_case_to_db(
            serial,
            model,
            fw_version,
            cleaned,
            reply,
        )

        messagebox.showinfo("DB", f"{action} case {cid}")


if __name__ == "__main__":
    main()