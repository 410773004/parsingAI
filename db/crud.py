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
        conn.close()
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
    conn.close()
    return "updated", cid


def list_cases(limit: int = 100) -> list:
    conn = connect()
    rows = conn.execute(
        """
        SELECT case_id, serial, model, fw_version, issue, updated_at
        FROM cases ORDER BY case_id DESC LIMIT ?
        """,
        (limit,),
    ).fetchall()
    conn.close()
    return rows


def get_case(case_id: int):
    conn = connect()
    row = conn.execute(
        """
        SELECT case_id, serial, model, fw_version,
               cleaned_log, llm_output, ground_truth
        FROM cases WHERE case_id=?
        """,
        (case_id,),
    ).fetchone()
    conn.close()
    return row


def create_case(
    serial: str,
    model: str,
    fw_version: str,
    cleaned_log: str,
    llm_output: str,
    ground_truth: str,
) -> None:
    now = now_iso()
    conn = connect()
    conn.execute(
        """
        INSERT INTO cases(
            serial, model, fw_version,
            cleaned_log, llm_output, ground_truth,
            created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (serial, model, fw_version, cleaned_log, llm_output, ground_truth, now, now),
    )
    conn.commit()
    conn.close()


def update_case(
    case_id: int,
    serial: str,
    model: str,
    fw_version: str,
    cleaned_log: str,
    llm_output: str,
    ground_truth: str,
) -> None:
    conn = connect()
    conn.execute(
        """
        UPDATE cases
        SET serial=?, model=?, fw_version=?, cleaned_log=?,
            llm_output=?, ground_truth=?, updated_at=?
        WHERE case_id=?
        """,
        (serial, model, fw_version, cleaned_log, llm_output, ground_truth, now_iso(), case_id),
    )
    conn.commit()
    conn.close()


def delete_case(case_id: int) -> None:
    conn = connect()
    conn.execute("DELETE FROM cases WHERE case_id=?", (case_id,))
    conn.commit()

    # Re-sequence auto-increment IDs
    rows = conn.execute(
        """
        SELECT serial, model, fw_version, cleaned_log, llm_output,
               ground_truth, created_at, updated_at
        FROM cases ORDER BY case_id
        """
    ).fetchall()
    conn.execute("DELETE FROM cases")
    conn.execute("DELETE FROM sqlite_sequence WHERE name='cases'")
    for r in rows:
        conn.execute(
            """
            INSERT INTO cases(
                serial, model, fw_version, cleaned_log, llm_output,
                ground_truth, created_at, updated_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            r,
        )
    conn.commit()
    conn.close()
