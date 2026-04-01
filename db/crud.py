# db/crud.py
from datetime import datetime
from .database import connect


def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")


def upsert_case(
    serial: str,
    model: str,
    fw_version: str,
    issue: str,
    cleaned_log: str,
    llm_output: str,
) -> tuple[str, int]:
    now = now_iso()
    conn = connect()
    try:
        row = conn.execute(
            "SELECT case_id FROM cases WHERE serial=?", (serial,)
        ).fetchone()

        if row is None:
            conn.execute(
                """
                INSERT INTO cases(
                    serial, model, fw_version, issue,
                    cleaned_log, llm_output, ground_truth,
                    created_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (serial, model, fw_version, issue, cleaned_log, llm_output, "", now, now),
            )
            conn.commit()
            cid = conn.execute("SELECT last_insert_rowid()").fetchone()[0]
            return "inserted", cid

        cid = row[0]
        conn.execute(
            """
            UPDATE cases
            SET model=?, fw_version=?, issue=?, cleaned_log=?,
                llm_output=?, updated_at=?
            WHERE case_id=?
            """,
            (model, fw_version, issue, cleaned_log, llm_output, now, cid),
        )
        conn.commit()
        return "updated", cid
    finally:
        conn.close()


def list_cases(limit: int = 100) -> list:
    conn = connect()
    try:
        rows = conn.execute(
            """
            SELECT case_id, serial, model, fw_version, issue, updated_at
            FROM cases ORDER BY case_id DESC LIMIT ?
            """,
            (limit,),
        ).fetchall()
        return rows
    finally:
        conn.close()


def get_case(case_id: int):
    conn = connect()
    try:
        row = conn.execute(
            """
            SELECT case_id, serial, model, fw_version, issue,
                   cleaned_log, llm_output, ground_truth
            FROM cases WHERE case_id=?
            """,
            (case_id,),
        ).fetchone()
        return row
    finally:
        conn.close()


def create_case(
    serial: str,
    model: str,
    fw_version: str,
    issue: str,
    cleaned_log: str,
    llm_output: str,
    ground_truth: str,
) -> None:
    now = now_iso()
    conn = connect()
    try:
        conn.execute(
            """
            INSERT INTO cases(
                serial, model, fw_version, issue,
                cleaned_log, llm_output, ground_truth,
                created_at, updated_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (serial, model, fw_version, issue, cleaned_log, llm_output, ground_truth, now, now),
        )
        conn.commit()
    finally:
        conn.close()


def update_case(
    case_id: int,
    serial: str,
    model: str,
    fw_version: str,
    issue: str,
    cleaned_log: str,
    llm_output: str,
    ground_truth: str,
) -> None:
    conn = connect()
    try:
        conn.execute(
            """
            UPDATE cases
            SET serial=?, model=?, fw_version=?, issue=?, cleaned_log=?,
                llm_output=?, ground_truth=?, updated_at=?
            WHERE case_id=?
            """,
            (serial, model, fw_version, issue, cleaned_log, llm_output, ground_truth, now_iso(), case_id),
        )
        conn.commit()
    finally:
        conn.close()


def delete_case(case_id: int) -> None:
    conn = connect()
    try:
        conn.execute("DELETE FROM cases WHERE case_id=?", (case_id,))
        conn.commit()
    finally:
        conn.close()
