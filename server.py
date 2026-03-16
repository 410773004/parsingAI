from __future__ import annotations

import sqlite3
import shutil
import tempfile
from datetime import datetime
from pathlib import Path
from typing import Optional

import ollama
from fastapi import FastAPI, UploadFile, File, Form, Request
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel

import config
import prompts
from parsers import parse_pj, parse_er
from fastapi.concurrency import run_in_threadpool


app = FastAPI(title="ParsingAI Server")

DB_PATH = Path("data/fa_dataset.db")
templates = Jinja2Templates(directory="templates")

current_stage = "idle"


class AnalyzeResponse(BaseModel):
    ok: bool
    mode: str
    token_count: int
    llm_output: str
    db_action: Optional[str] = None
    case_id: Optional[int] = None
    error: Optional[str] = None


def set_stage(stage: str):
    global current_stage
    current_stage = stage


def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")


def connect_db() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH))
    conn.execute("PRAGMA journal_mode=WAL;")
    return conn


def init_db() -> None:
    conn = connect_db()

    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS cases (
            case_id INTEGER PRIMARY KEY AUTOINCREMENT,
            serial TEXT UNIQUE,
            model TEXT,
            fw_version TEXT,
            cleaned_log TEXT,
            llm_output TEXT,
            ground_truth TEXT,
            created_at TEXT,
            updated_at TEXT
        );
        """
    )

    conn.commit()
    conn.close()


@app.on_event("startup")
def startup_event() -> None:
    init_db()


def detect_product_from_fw(fw: str) -> str:
    fw = (fw or "").strip()

    if len(fw) < 2:
        return "PJ"

    prefix = fw[:2]

    if prefix.isalpha():
        return "PJ"

    return "ER"


def count_tokens(text: str) -> int:
    try:
        import tiktoken
        enc = tiktoken.get_encoding("cl100k_base")
        return len(enc.encode(text))
    except Exception:
        return max(1, len(text) // 4)


def run_llm(cleaned: str, model: str, fw_version: str, issue: str) -> str:
    user_prompt = prompts.USER_TEMPLATE.format(
        model=model,
        fw_version=fw_version,
        issue=issue,
        log_text=cleaned,
    )

    messages = [
        {"role": "system", "content": prompts.SYSTEM},
        {"role": "user", "content": user_prompt},
    ]

    resp = ollama.chat(
        model=config.MODEL,
        messages=messages,
        options={"temperature": config.TEMPERATURE},
    )

    return resp["message"]["content"]


def upsert_case_to_db(
    serial: str,
    model: str,
    fw_version: str,
    cleaned_log: str,
    llm_output: str,
) -> tuple[str, int]:
    now = now_iso()
    conn = connect_db()

    row = conn.execute(
        "SELECT case_id FROM cases WHERE serial=?",
        (serial,),
    ).fetchone()

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
            (
                serial,
                model,
                fw_version,
                cleaned_log,
                llm_output,
                "",
                now,
                now,
            ),
        )

        conn.commit()
        cid = conn.execute("SELECT last_insert_rowid()").fetchone()[0]
        conn.close()
        return "inserted", cid

    cid = row[0]

    conn.execute(
        """
        UPDATE cases
        SET model=?,
            fw_version=?,
            cleaned_log=?,
            llm_output=?,
            updated_at=?
        WHERE case_id=?
        """,
        (
            model,
            fw_version,
            cleaned_log,
            llm_output,
            now,
            cid,
        ),
    )

    conn.commit()
    conn.close()
    return "updated", cid


@app.get("/", response_class=HTMLResponse)
def home():
    html_path = Path("index.html")

    if not html_path.exists():
        return "<h1>index.html not found</h1>"

    return html_path.read_text(encoding="utf-8")


@app.get("/progress")
def progress():
    return {"stage": current_stage}


@app.post("/analyze", response_model=AnalyzeResponse)
async def analyze(
    mode: str = Form(...),
    serial: str = Form(...),
    issue: str = Form(...),
    model: str = Form(...),
    fw_version: str = Form(""),
    save_to_db: bool = Form(False),
    log_files: list[UploadFile] | None = File(None),
    cleaned_file: UploadFile | None = File(None),
) -> AnalyzeResponse:

    temp_dir: Path | None = None

    try:
        set_stage("收到請求")

        if not serial.strip():
            return AnalyzeResponse(ok=False, mode=mode, token_count=0, llm_output="", error="serial required")

        if not issue.strip():
            return AnalyzeResponse(ok=False, mode=mode, token_count=0, llm_output="", error="issue required")

        if not model.strip():
            return AnalyzeResponse(ok=False, mode=mode, token_count=0, llm_output="", error="model required")

        if not fw_version.strip():
            return AnalyzeResponse(ok=False, mode=mode, token_count=0, llm_output="", error="fw_version required")

        if mode == "raw_folder":
            set_stage("讀取 log files")

            if not log_files:
                return AnalyzeResponse(ok=False, mode=mode, token_count=0, llm_output="", error="log_files required")

            temp_dir = Path(tempfile.mkdtemp())
            valid_count = 0

            for f in log_files:
                fname = Path(f.filename or "").name
                lower_name = fname.lower()

                if not (
                    lower_name.endswith(".log")
                    and(
                        lower_name.startswith("hs")
                        or "norlog" in lower_name
                        or "lognor" in lower_name
                    )
                ):
                    continue

                data = await f.read()
                (temp_dir / fname).write_bytes(data)
                valid_count += 1

            if valid_count == 0:
                return AnalyzeResponse(
                    ok=False,
                    mode=mode,
                    token_count=0,
                    llm_output="",
                    error="no Hs*.log or norlog.log found",
                )

            product = detect_product_from_fw(fw_version)

            set_stage("執行 parser")

            if product == "PJ":
                cleaned = await run_in_threadpool(parse_pj, temp_dir)
            else:
                cleaned = await run_in_threadpool(parse_er, temp_dir)

        elif mode == "parsed_file":
            set_stage("讀取 cleaned log")

            if cleaned_file is None:
                return AnalyzeResponse(ok=False, mode=mode, token_count=0, llm_output="", error="cleaned_file required")

            raw = await cleaned_file.read()
            cleaned = raw.decode("utf-8", errors="ignore")

        else:
            return AnalyzeResponse(ok=False, mode=mode, token_count=0, llm_output="", error="invalid mode")

        if not cleaned.strip():
            return AnalyzeResponse(ok=False, mode=mode, token_count=0, llm_output="", error="cleaned log empty")

        set_stage("計算 token")
        tok = await run_in_threadpool(count_tokens, cleaned)
        print("CLEANED TOKENS:", tok)

        set_stage("LLM 分析")
        reply = await run_in_threadpool(run_llm, cleaned, model, fw_version, issue)

        db_action = None
        case_id = None

        if save_to_db:
            set_stage("儲存資料庫")
            db_action, case_id = await run_in_threadpool(
                upsert_case_to_db,
                serial,
                model,
                fw_version,
                cleaned,
                reply,
            )

        set_stage("完成")

        return AnalyzeResponse(
            ok=True,
            mode=mode,
            token_count=tok,
            llm_output=reply,
            db_action=db_action,
            case_id=case_id,
        )

    except Exception as e:
        set_stage(f"失敗: {str(e)}")
        return AnalyzeResponse(
            ok=False,
            mode=mode,
            token_count=0,
            llm_output="",
            error=str(e),
        )

    finally:
        if temp_dir is not None and temp_dir.exists():
            shutil.rmtree(temp_dir, ignore_errors=True)


@app.get("/dataset")
def dataset_list(request: Request):
    conn = connect_db()

    rows = conn.execute(
        """
        SELECT case_id, serial, model, fw_version, updated_at
        FROM cases
        ORDER BY case_id DESC
        LIMIT 100
        """
    ).fetchall()

    conn.close()

    return templates.TemplateResponse(
        "dataset_list.html",
        {
            "request": request,
            "cases": rows,
        },
    )


@app.get("/dataset/new")
def dataset_new(request: Request):
    return templates.TemplateResponse(
        "dataset_new.html",
        {"request": request},
    )


@app.post("/dataset/new")
def dataset_new_submit(
    serial: str = Form(...),
    model: str = Form(...),
    fw_version: str = Form(""),
    cleaned_log: str = Form(""),
    llm_output: str = Form(""),
    ground_truth: str = Form(""),
):
    now = now_iso()
    conn = connect_db()

    conn.execute(
        """
        INSERT INTO cases(
            serial, model, fw_version,
            cleaned_log, llm_output, ground_truth,
            created_at, updated_at
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            serial,
            model,
            fw_version,
            cleaned_log,
            llm_output,
            ground_truth,
            now,
            now,
        ),
    )

    conn.commit()
    conn.close()

    return RedirectResponse("/dataset", status_code=303)


@app.get("/dataset/{case_id}/edit")
def dataset_edit(request: Request, case_id: int):
    conn = connect_db()

    row = conn.execute(
        """
        SELECT case_id, serial, model, fw_version,
               cleaned_log, llm_output, ground_truth
        FROM cases
        WHERE case_id=?
        """,
        (case_id,),
    ).fetchone()

    conn.close()

    if row is None:
        return HTMLResponse("<h1>Case not found</h1>", status_code=404)

    return templates.TemplateResponse(
        "dataset_edit.html",
        {
            "request": request,
            "case": row,
        },
    )


@app.post("/dataset/{case_id}/edit")
def dataset_edit_submit(
    case_id: int,
    serial: str = Form(...),
    model: str = Form(...),
    fw_version: str = Form(""),
    cleaned_log: str = Form(""),
    llm_output: str = Form(""),
    ground_truth: str = Form(""),
):
    conn = connect_db()

    conn.execute(
        """
        UPDATE cases
        SET serial=?,
            model=?,
            fw_version=?,
            cleaned_log=?,
            llm_output=?,
            ground_truth=?,
            updated_at=?
        WHERE case_id=?
        """,
        (
            serial,
            model,
            fw_version,
            cleaned_log,
            llm_output,
            ground_truth,
            now_iso(),
            case_id,
        ),
    )

    conn.commit()
    conn.close()

    return RedirectResponse(f"/dataset/{case_id}", status_code=303)

@app.post("/dataset/{case_id}/delete")
def dataset_delete(case_id: int):

    conn = connect_db()

    # 刪除指定 case
    conn.execute(
        "DELETE FROM cases WHERE case_id=?",
        (case_id,)
    )

    conn.commit()

    # 重新排序 case_id
    rows = conn.execute(
        """
        SELECT serial, model, fw_version,
               cleaned_log, llm_output,
               ground_truth, created_at, updated_at
        FROM cases
        ORDER BY case_id
        """
    ).fetchall()

    # 清空 table
    conn.execute("DELETE FROM cases")
    conn.execute("DELETE FROM sqlite_sequence WHERE name='cases'")

    # 重新 insert
    for r in rows:
        conn.execute(
            """
            INSERT INTO cases(
                serial, model, fw_version,
                cleaned_log, llm_output,
                ground_truth, created_at, updated_at
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            r
        )

    conn.commit()
    conn.close()

    return RedirectResponse("/dataset", status_code=303)

@app.get("/dataset/{case_id}")
def dataset_view(request: Request, case_id: int):
    conn = connect_db()

    row = conn.execute(
        """
        SELECT case_id, serial, model, fw_version,
               cleaned_log, llm_output, ground_truth
        FROM cases
        WHERE case_id=?
        """,
        (case_id,),
    ).fetchone()

    conn.close()

    if row is None:
        return HTMLResponse("<h1>Case not found</h1>", status_code=404)

    return templates.TemplateResponse(
        "dataset_view.html",
        {
            "request": request,
            "case": row,
        },
    )