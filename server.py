from __future__ import annotations

import sqlite3
import shutil
import tempfile
from datetime import datetime
from pathlib import Path
from typing import Optional

import ollama
from fastapi import FastAPI, UploadFile, File, Form, Request
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel

import config
import prompts
from event_flow import analyze_event_flow
from parsers.project_parser import (
    parse as parse_project,
    detect_project_from_raw_logs,
    extract_metadata_from_raw_logs,
    build_temperature_section,
)

app = FastAPI(title="ParsingAI Server")

DB_PATH = Path("data/fa_dataset.db")
templates = Jinja2Templates(directory="templates")

current_stage = "idle"


class AnalyzeResponse(BaseModel):
    ok: bool
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
            issue TEXT,
            cleaned_log TEXT,
            llm_output TEXT,
            ground_truth TEXT,
            created_at TEXT,
            updated_at TEXT
        );
        """
    )
    try:
        conn.execute("ALTER TABLE cases ADD COLUMN issue TEXT")
    except Exception:
        pass  # 已經有就忽略
    conn.commit()
    conn.close()

def load_smart_info(folder: str | Path) -> str:
    folder = Path(folder)

    for f in folder.glob("smart_info.txt"):
        try:
            content = f.read_text(encoding="utf-8", errors="ignore").strip()
            if not content:
                return ""

            return (
                "================================================================================\n"
                "SMART INFO\n"
                "================================================================================\n"
                f"{content}\n"
            )
        except Exception:
            continue

    return ""

@app.on_event("startup")
def startup_event() -> None:
    init_db()


def count_tokens(text: str) -> int:
    try:
        import tiktoken
        enc = tiktoken.get_encoding("cl100k_base")
        return len(enc.encode(text))
    except Exception:
        return max(1, len(text) // 4)


def run_llm(cleaned: str, project: str, fw_version: str, issue: str) -> str:
    user_prompt = prompts.USER_TEMPLATE.format(
        model=project,
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
    issue: str,
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
                serial, model, fw_version,issue,
                cleaned_log, llm_output, ground_truth,
                created_at, updated_at
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?,?)
            """,
            (
                serial,
                model,
                fw_version,
                issue,
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
            issue=?,
            cleaned_log=?,
            llm_output=?,
            updated_at=?
        WHERE case_id=?
        """,
        (
            model,
            fw_version,
            issue,
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
    issue: str = Form(...),
    issue_other: str = Form(""),
    folder_names: str = Form(""),
    save_to_db: bool = Form(False),
    log_files: list[UploadFile] | None = File(None),
) -> AnalyzeResponse:
    temp_dir: Path | None = None

    try:
        set_stage("收到請求")

        final_issue = issue_other.strip() if issue == "__OTHER__" else issue.strip()

        if not log_files:
            return AnalyzeResponse(
                ok=False,
                token_count=0,
                llm_output="",
                error="log_files required",
            )

        temp_dir = Path(tempfile.mkdtemp())
        valid_count = 0

        for f in log_files:
            fname = Path(f.filename or "").name
            lower_name = fname.lower()

            is_valid_log = (
                lower_name.endswith(".log")
                and (
                    lower_name.startswith("hs")
                    or "norlog" in lower_name
                    or "lognor" in lower_name
                )
            )

            is_smart_info = lower_name == "smart_info.txt"

            if not (is_valid_log or is_smart_info):
                continue

            data = await f.read()
            (temp_dir / fname).write_bytes(data)

            if is_valid_log:
                valid_count += 1

        if valid_count == 0:
            return AnalyzeResponse(
                ok=False,
                token_count=0,
                llm_output="",
                error="no valid log found",
            )

        set_stage("快速判斷 project")
        project = await run_in_threadpool(detect_project_from_raw_logs, temp_dir)

        if not project:
            return AnalyzeResponse(
                ok=False,
                token_count=0,
                llm_output="",
                error="cannot detect project from raw logs",
            )

        set_stage("執行 parser")
        cleaned = await run_in_threadpool(parse_project, project, temp_dir)

        set_stage("分析 Event Flow")
        flow_block = await run_in_threadpool(analyze_event_flow, temp_dir)

        set_stage("分析 Temperature")
        temperature_section = await run_in_threadpool(build_temperature_section, temp_dir)

        extra_sections = []

        if temperature_section:
            extra_sections.append(temperature_section)

        if flow_block:
            extra_sections.append(flow_block)

        if extra_sections:
            insert_block = "\n\n".join(extra_sections)

            event_detail_header = (
                "================================================================================\n"
                "EVENT DETAIL\n"
                "================================================================================"
            )

            if event_detail_header in cleaned:
                cleaned = cleaned.replace(
                    event_detail_header,
                    f"{insert_block}\n\n{event_detail_header}",
                    1,
                )
            else:
                cleaned = cleaned + "\n\n" + insert_block

        set_stage("抽取 raw metadata")
        primary_folder = folder_names.split("||")[0].strip() if folder_names else ""
        meta = await run_in_threadpool(extract_metadata_from_raw_logs, temp_dir, primary_folder)
        project = meta.get("project", "") or project
        fw_version = meta.get("fw_version", "")
        serial = meta.get("serial", "")

        if not cleaned.strip():
            return AnalyzeResponse(
                ok=False,
                token_count=0,
                llm_output="",
                error="cleaned log empty",
            )

        set_stage("計算 token")
        tok = await run_in_threadpool(count_tokens, cleaned)

        set_stage("LLM 分析")
        reply = await run_in_threadpool(run_llm, cleaned, project, fw_version, final_issue)

        db_action = None
        case_id = None

        if save_to_db:
            set_stage("儲存資料庫")
            print("DB save values:", serial, project, fw_version, final_issue)
            db_action, case_id = await run_in_threadpool(
                upsert_case_to_db,
                serial,
                project,
                fw_version,
                final_issue,
                cleaned,
                reply,
            )
            print("db_action:", db_action, "case_id:", case_id)

        set_stage("完成")

        return AnalyzeResponse(
            ok=True,
            token_count=tok,
            llm_output=reply,
            db_action=db_action,
            case_id=case_id,
        )

    except Exception as e:
        set_stage(f"失敗: {str(e)}")
        return AnalyzeResponse(
            ok=False,
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
        SELECT case_id, serial, model, fw_version,issue, updated_at
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

    conn.execute(
        "DELETE FROM cases WHERE case_id=?",
        (case_id,),
    )

    conn.commit()

    rows = conn.execute(
        """
        SELECT serial, model, fw_version,
               cleaned_log, llm_output,
               ground_truth, created_at, updated_at
        FROM cases
        ORDER BY case_id
        """
    ).fetchall()

    conn.execute("DELETE FROM cases")
    conn.execute("DELETE FROM sqlite_sequence WHERE name='cases'")

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
            r,
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