from __future__ import annotations

import json
import sqlite3
import tkinter as tk
from tkinter import filedialog
from datetime import datetime
from pathlib import Path

DB_PATH = Path("data/fa_dataset.db")

SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS cases (
    case_id     INTEGER PRIMARY KEY,   -- auto increment
    model       TEXT NOT NULL,          -- PJ / ER / CX3
    cleaned_log TEXT NOT NULL,
    fa_report   TEXT,
    created_at  TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_cases_created_at ON cases(created_at);
"""


# -------------------------
# basic helpers
# -------------------------
def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")


def connect() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH))
    conn.execute("PRAGMA journal_mode=WAL;")
    return conn


def ensure_column(conn: sqlite3.Connection, table: str, column: str, col_def: str) -> None:
    """Add column if missing (simple schema upgrade)."""
    cur = conn.execute(f"PRAGMA table_info({table})")
    cols = {row[1] for row in cur.fetchall()}
    if column not in cols:
        conn.execute(f"ALTER TABLE {table} ADD COLUMN {column} {col_def}")
        conn.commit()


def init_db() -> None:
    conn = connect()
    try:
        conn.executescript(SCHEMA_SQL)
        # add fw_version column (new)
        ensure_column(conn, "cases", "fw_version", "TEXT NOT NULL DEFAULT ''")
    finally:
        conn.close()


def ask_model() -> str:
    while True:
        print("\nSelect SSD Model:")
        print("1) PJ")
        print("2) ER")
        print("3) CX3")
        c = input("Choose (1/2/3): ").strip()
        if c == "1":
            return "PJ"
        if c == "2":
            return "ER"
        if c == "3":
            return "CX3"
        print("invalid option")


def ask_fw_version() -> str:
    # allow empty
    return input("FW version (optional, e.g., FG2N9031): ").strip()


def normalize_json_if_possible(text: str) -> str:
    t = text.strip()
    if not t:
        return ""
    try:
        return json.dumps(json.loads(t), ensure_ascii=False, indent=2)
    except Exception:
        return text


# -------------------------
# CRUD
# -------------------------
def add_case_from_file():
    model = ask_model()
    fw_version = ask_fw_version()

    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)

    print("Select cleaned log file")
    log_path = filedialog.askopenfilename(title="Select cleaned log file")
    if not log_path:
        print("No log file selected")
        root.destroy()
        return

    print("Select FA report file (optional)")
    report_path = filedialog.askopenfilename(title="Select FA report file (optional)")
    root.destroy()

    try:
        cleaned_log = Path(log_path).read_text(encoding="utf-8", errors="ignore")
    except Exception as e:
        print("log file read error:", e)
        return

    if not cleaned_log.strip():
        print("[ERROR] cleaned_log is empty")
        return

    fa_report = ""
    if report_path:
        try:
            fa_report_raw = Path(report_path).read_text(encoding="utf-8", errors="ignore")
            fa_report = normalize_json_if_possible(fa_report_raw)
        except Exception as e:
            print("report file read error:", e)
            return

    conn = connect()
    try:
        ensure_column(conn, "cases", "fw_version", "TEXT NOT NULL DEFAULT ''")
        conn.execute(
            """
            INSERT INTO cases(model, fw_version, cleaned_log, fa_report, created_at)
            VALUES (?, ?, ?, ?, ?)
            """,
            (model, fw_version, cleaned_log, fa_report, now_iso()),
        )
        conn.commit()
        case_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0]
        print(f"[OK] Saved: case_id={case_id}, model={model}, fw={fw_version or '(empty)'}")
    finally:
        conn.close()


def add_case_manual():
    model = ask_model()
    fw_version = ask_fw_version()

    print("\nPaste CLEANED LOG. End with a single line: END")
    log_lines: list[str] = []
    while True:
        ln = input()
        if ln == "END":
            break
        log_lines.append(ln)
    cleaned_log = "\n".join(log_lines).strip()

    print("\nPaste FA REPORT (JSON or text). End with a single line: END")
    rep_lines: list[str] = []
    while True:
        ln = input()
        if ln == "END":
            break
        rep_lines.append(ln)
    fa_report_raw = "\n".join(rep_lines).strip()
    fa_report = normalize_json_if_possible(fa_report_raw)

    if not cleaned_log:
        print("[ERROR] cleaned_log is empty")
        return

    conn = connect()
    try:
        ensure_column(conn, "cases", "fw_version", "TEXT NOT NULL DEFAULT ''")
        conn.execute(
            """
            INSERT INTO cases(model, fw_version, cleaned_log, fa_report, created_at)
            VALUES (?, ?, ?, ?, ?)
            """,
            (model, fw_version, cleaned_log, fa_report, now_iso()),
        )
        conn.commit()
        case_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0]
        print(f"[OK] Saved: case_id={case_id}, model={model}, fw={fw_version or '(empty)'}")
    finally:
        conn.close()


def list_cases():
    """Fixed limit = 20."""
    conn = connect()
    try:
        ensure_column(conn, "cases", "fw_version", "TEXT NOT NULL DEFAULT ''")
        cur = conn.execute(
            """
            SELECT case_id, created_at, model, fw_version
            FROM cases
            ORDER BY case_id DESC
            LIMIT 20
            """
        )
        rows = cur.fetchall()
        if not rows:
            print("(no cases)")
            return
        for case_id, created_at, model, fw in rows:
            fw_disp = fw if fw else "-"
            print(f"- {case_id} | {created_at} | {model} | FW:{fw_disp}")
    finally:
        conn.close()


def view_case():
    cid = input("case_id: ").strip()
    if not cid.isdigit():
        print("[ERROR] case_id must be a number")
        return

    conn = connect()
    try:
        ensure_column(conn, "cases", "fw_version", "TEXT NOT NULL DEFAULT ''")
        row = conn.execute(
            """
            SELECT case_id, model, fw_version, cleaned_log, fa_report, created_at
            FROM cases
            WHERE case_id=?
            """,
            (int(cid),),
        ).fetchone()

        if not row:
            print("[NOT FOUND] case not found")
            return

        case_id, model, fw, cleaned_log, fa_report, created_at = row
        print("\n==============================")
        print(f"case_id    : {case_id}")
        print(f"created_at : {created_at}")
        print(f"model      : {model}")
        print(f"fw_version : {fw or '(empty)'}")
        print("==============================\n")

        print("=== CLEANED LOG ===\n")
        print(cleaned_log)

        print("\n=== FA REPORT ===\n")
        print(fa_report if fa_report else "(empty)")
        print("\n==============================\n")
    finally:
        conn.close()


def delete_case():
    cid = input("case_id to delete: ").strip()
    if not cid.isdigit():
        print("[ERROR] case_id must be a number")
        return

    conn = connect()
    try:
        ensure_column(conn, "cases", "fw_version", "TEXT NOT NULL DEFAULT ''")
        row = conn.execute(
            "SELECT case_id, model, fw_version, created_at FROM cases WHERE case_id=?",
            (int(cid),),
        ).fetchone()

        if not row:
            print("[NOT FOUND] case not found")
            return

        case_id, model, fw, created_at = row
        confirm = input(
            f"Delete case {case_id} (model={model}, fw={fw or '-'}, created_at={created_at})? (y/n): "
        ).strip().lower()
        if confirm != "y":
            print("cancelled")
            return

        conn.execute("DELETE FROM cases WHERE case_id=?", (int(cid),))
        conn.commit()
        print(f"[OK] Deleted: {case_id}")
    finally:
        conn.close()


# -------------------------
# Edit any field
# -------------------------
EDITABLE_FIELDS = {
    "1": ("model", "text"),
    "2": ("fw_version", "text"),
    "3": ("cleaned_log", "multiline"),
    "4": ("fa_report", "multiline_json_ok"),
    "5": ("created_at", "text"),
}


def edit_case():
    cid = input("case_id to edit: ").strip()
    if not cid.isdigit():
        print("[ERROR] case_id must be a number")
        return
    case_id = int(cid)

    conn = connect()
    try:
        ensure_column(conn, "cases", "fw_version", "TEXT NOT NULL DEFAULT ''")

        row = conn.execute(
            """
            SELECT case_id, model, fw_version, cleaned_log, fa_report, created_at
            FROM cases
            WHERE case_id=?
            """,
            (case_id,),
        ).fetchone()

        if not row:
            print("[NOT FOUND] case not found")
            return

        _, model, fw, cleaned_log, fa_report, created_at = row

        print("\nCurrent values (summary):")
        print(f"1) model      : {model}")
        print(f"2) fw_version : {fw or '(empty)'}")
        print(f"3) cleaned_log: ({len(cleaned_log)} chars)")
        print(f"4) fa_report  : ({len(fa_report or '')} chars)")
        print(f"5) created_at : {created_at}")

        print("\nWhich field to edit?")
        print("1) model")
        print("2) fw_version")
        print("3) cleaned_log (paste, end with END)")
        print("4) fa_report (paste, end with END; JSON auto-format if possible)")
        print("5) created_at (manual text)")
        key = input("Choose (1-5): ").strip()

        if key not in EDITABLE_FIELDS:
            print("invalid option")
            return

        field, kind = EDITABLE_FIELDS[key]

        # get new value
        if kind == "text":
            new_val = input(f"New {field}: ").strip()
            # model field: optionally enforce allowed values
            if field == "model" and new_val not in ("PJ", "ER", "CX3"):
                print("[ERROR] model must be PJ / ER / CX3")
                return

        elif kind in ("multiline", "multiline_json_ok"):
            print(f"\nPaste new {field}. End with a single line: END")
            lines: list[str] = []
            while True:
                ln = input()
                if ln == "END":
                    break
                lines.append(ln)
            new_val = "\n".join(lines).strip()
            if kind == "multiline_json_ok":
                new_val = normalize_json_if_possible(new_val)

        else:
            print("internal error")
            return

        if field in ("model", "fw_version", "created_at") and new_val == "":
            # allow empty fw_version, but model/created_at should not be empty
            if field == "fw_version":
                pass
            else:
                print(f"[ERROR] {field} cannot be empty")
                return

        if field == "cleaned_log" and not new_val:
            print("[ERROR] cleaned_log cannot be empty")
            return

        # update
        conn.execute(f"UPDATE cases SET {field}=? WHERE case_id=?", (new_val, case_id))
        conn.commit()
        print(f"[OK] Updated case {case_id}: {field}")
    finally:
        conn.close()


def main():
    init_db()

    while True:
        print("\n=== Dataset Tool ===")
        print("1) Add case from file (choose files)")
        print("2) Add case manually (paste)")
        print("3) List recent cases (fixed 20)")
        print("4) View case detail")
        print("5) Edit case (any field)")
        print("6) Delete case")
        print("0) Exit")
        choice = input("Choose: ").strip()

        if choice == "1":
            add_case_from_file()
        elif choice == "2":
            add_case_manual()
        elif choice == "3":
            list_cases()
        elif choice == "4":
            view_case()
        elif choice == "5":
            edit_case()
        elif choice == "6":
            delete_case()
        elif choice == "0":
            print("bye")
            break
        else:
            print("invalid option")


if __name__ == "__main__":
    main()