from datetime import datetime
import sqlite3
from pathlib import Path

DB_PATH = Path("data/fa_dataset.db")


def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")


def connect():
    return sqlite3.connect(DB_PATH)


# =====================
# Add one case manually
# =====================
def add_case_manual():
    serial = input("Serial number: ").strip()
    model = input("Model: ").strip()
    fw_version = input("FW version: ").strip()

    if not serial or not model:
        print("Serial and Model are required")
        return

    print("\nPaste CLEANED LOG")
    print("End with: END\n")
    log_lines = []
    while True:
        l = input()
        if l == "END":
            break
        log_lines.append(l)
    cleaned_log = "\n".join(log_lines).strip()

    print("\nPaste LLM OUTPUT")
    print("End with: END\n")
    llm_lines = []
    while True:
        l = input()
        if l == "END":
            break
        llm_lines.append(l)
    llm_output = "\n".join(llm_lines).strip()

    print("\nPaste GROUND TRUTH (optional)")
    print("End with: END\n")
    gt_lines = []
    while True:
        l = input()
        if l == "END":
            break
        gt_lines.append(l)
    ground_truth = "\n".join(gt_lines).strip()

    conn = connect()

    row = conn.execute(
        "SELECT case_id FROM cases WHERE serial=?",
        (serial,)
    ).fetchone()

    now = now_iso()

    if row is None:
        conn.execute(
            """
            INSERT INTO cases(
                serial, model, fw_version,
                cleaned_log, llm_output, ground_truth,
                created_at, updated_at
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (serial, model, fw_version, cleaned_log, llm_output, ground_truth, now, now)
        )
        print("Inserted new case")
    else:
        conn.execute(
            """
            UPDATE cases
            SET model=?,
                fw_version=?,
                cleaned_log=?,
                llm_output=?,
                ground_truth=?,
                updated_at=?
            WHERE serial=?
            """,
            (model, fw_version, cleaned_log, llm_output, ground_truth, now, serial)
        )
        print("Updated existing case")

    conn.commit()
    conn.close()


# =====================
# List
# =====================
def list_cases(limit: int = 50):
    conn = connect()

    rows = conn.execute(
        """
        SELECT case_id, serial, model, fw_version,
               created_at, updated_at
        FROM cases
        ORDER BY case_id ASC
        LIMIT ?
        """,
        (limit,)
    ).fetchall()

    conn.close()

    if not rows:
        print("No cases")
        return

    print("\n=== Cases ===\n")

    for r in rows:
        print(
            f"case_id:{r[0]} | SN:{r[1]} | Model:{r[2]} | FW:{r[3]} | Updated:{r[5]}"
        )


# =====================
# View
# =====================
def view_case():
    cid = input("case_id: ").strip()

    if not cid.isdigit():
        print("case_id must be a number")
        return

    conn = connect()

    row = conn.execute(
        """
        SELECT case_id, serial, model, fw_version,
               cleaned_log, llm_output, ground_truth,
               created_at, updated_at
        FROM cases
        WHERE case_id=?
        """,
        (cid,)
    ).fetchone()

    conn.close()

    if not row:
        print("Case not found")
        return

    print("\n==============================")
    print(f"case_id    : {row[0]}")
    print(f"serial     : {row[1]}")
    print(f"model      : {row[2]}")
    print(f"fw_version : {row[3]}")
    print(f"created_at : {row[7]}")
    print(f"updated_at : {row[8]}")
    print("==============================\n")

    print("=== CLEANED LOG ===\n")
    print(row[4] if row[4] else "(empty)")

    print("\n=== LLM OUTPUT ===\n")
    print(row[5] if row[5] else "(empty)")

    print("\n=== GROUND TRUTH ===\n")
    print(row[6] if row[6] else "(empty)")
    print("\n==============================\n")


# =====================
# Edit
# =====================
def edit_case():
    cid = input("case_id: ").strip()

    print("\n1) serial")
    print("2) model")
    print("3) fw_version")
    print("4) cleaned_log")
    print("5) llm_output")
    print("6) ground_truth")
    choice = input("Choose field: ").strip()

    field_map = {
        "1": "serial",
        "2": "model",
        "3": "fw_version",
        "4": "cleaned_log",
        "5": "llm_output",
        "6": "ground_truth",
    }

    if choice not in field_map:
        print("Invalid choice")
        return

    field = field_map[choice]

    if field in ("serial", "model", "fw_version"):
        new_value = input(f"New {field}: ").strip()
    else:
        print(f"\nPaste new {field}")
        print("End with: END\n")
        lines = []
        while True:
            l = input()
            if l == "END":
                break
            lines.append(l)
        new_value = "\n".join(lines).strip()

    conn = connect()
    conn.execute(
        f"UPDATE cases SET {field}=?, updated_at=? WHERE case_id=?",
        (new_value, now_iso(), cid)
    )
    conn.commit()
    conn.close()

    print("Updated")


# =====================
# Delete
# =====================
def delete_case():
    cid = input("case_id: ").strip()

    conn = connect()
    conn.execute("DELETE FROM cases WHERE case_id=?", (cid,))
    conn.commit()
    conn.close()

    print("Deleted")


# =====================
# Main
# =====================
def main():
    while True:
        print("\n=== Dataset Tool ===")
        print("1) Add one case manually")
        print("2) List cases (50)")
        print("3) View case details")
        print("4) Edit case")
        print("5) Delete case")
        print("0) Exit")

        c = input("Choose: ").strip()

        if c == "1":
            add_case_manual()
        elif c == "2":
            list_cases()
        elif c == "3":
            view_case()
        elif c == "4":
            edit_case()
        elif c == "5":
            delete_case()
        elif c == "0":
            break


if __name__ == "__main__":
    main()