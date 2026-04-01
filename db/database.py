# db/database.py
import sqlite3
from pathlib import Path

DB_PATH = Path("data/fa_dataset.db")


def connect() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH))
    conn.execute("PRAGMA journal_mode=WAL;")
    return conn


def init() -> None:
    conn = connect()
    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS cases (
            case_id     INTEGER PRIMARY KEY AUTOINCREMENT,
            serial      TEXT UNIQUE,
            model       TEXT,
            fw_version  TEXT,
            issue       TEXT,
            cleaned_log TEXT,
            llm_output  TEXT,
            ground_truth TEXT,
            created_at  TEXT,
            updated_at  TEXT
        );
        """
    )
    try:
        conn.execute("ALTER TABLE cases ADD COLUMN issue TEXT")
    except Exception:
        pass
    conn.commit()
    conn.close()
