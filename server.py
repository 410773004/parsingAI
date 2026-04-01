# server.py
from __future__ import annotations

import shutil
import tempfile
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, File, Form, Request, UploadFile
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel

from db import database, crud
from services import analyzer

app = FastAPI(title="ParsingAI Server")
templates = Jinja2Templates(directory="templates")

_current_stage = "idle"


def _set_stage(stage: str) -> None:
    global _current_stage
    _current_stage = stage


analyzer.set_stage_callback(_set_stage)


class AnalyzeResponse(BaseModel):
    ok: bool
    token_count: int
    llm_output: str
    db_action: Optional[str] = None
    case_id: Optional[int] = None
    error: Optional[str] = None


@app.on_event("startup")
def startup_event() -> None:
    database.init()


@app.get("/progress")
def progress():
    return {"stage": _current_stage}


@app.get("/", response_class=HTMLResponse)
def home():
    html_path = Path("index.html")
    if not html_path.exists():
        return "<h1>index.html not found</h1>"
    return html_path.read_text(encoding="utf-8")


@app.post("/analyze", response_model=AnalyzeResponse)
async def analyze_endpoint(
    issue: str = Form(...),
    issue_other: str = Form(""),
    folder_names: str = Form(""),
    save_to_db: bool = Form(False),
    log_files: list[UploadFile] | None = File(None),
) -> AnalyzeResponse:
    temp_dir: Path | None = None
    try:
        _set_stage("上傳檔案")
        final_issue = issue_other.strip() if issue == "__OTHER__" else issue.strip()

        if not log_files:
            return AnalyzeResponse(ok=False, token_count=0, llm_output="", error="log_files required")

        temp_dir = Path(tempfile.mkdtemp())
        valid_count = 0
        for f in log_files:
            fname = Path(f.filename or "").name
            lower = fname.lower()
            is_log = lower.endswith(".log") and (
                lower.startswith("hs") or "norlog" in lower or "lognor" in lower
            )
            is_smart = lower == "smart_info.txt"
            if not (is_log or is_smart):
                continue
            (temp_dir / fname).write_bytes(await f.read())
            if is_log:
                valid_count += 1

        if valid_count == 0:
            return AnalyzeResponse(ok=False, token_count=0, llm_output="", error="no valid log found")

        primary_folder = folder_names.split("||")[0].strip() if folder_names else ""
        result = await run_in_threadpool(analyzer.analyze, temp_dir, final_issue, primary_folder)

        db_action = None
        case_id = None
        if save_to_db:
            _set_stage("儲存 DB")
            db_action, case_id = await run_in_threadpool(
                crud.upsert_case,
                result["serial"],
                result["project"],
                result["fw_version"],
                final_issue,
                result["cleaned_log"],
                result["llm_output"],
            )

        _set_stage("完成")
        return AnalyzeResponse(
            ok=True,
            token_count=result["token_count"],
            llm_output=result["llm_output"],
            db_action=db_action,
            case_id=case_id,
        )

    except Exception as e:
        _set_stage(f"錯誤: {e}")
        return AnalyzeResponse(ok=False, token_count=0, llm_output="", error=str(e))
    finally:
        if temp_dir and temp_dir.exists():
            shutil.rmtree(temp_dir, ignore_errors=True)


@app.get("/dataset")
def dataset_list(request: Request):
    return templates.TemplateResponse(
        "dataset_list.html",
        {"request": request, "cases": crud.list_cases()},
    )


@app.get("/dataset/new")
def dataset_new(request: Request):
    return templates.TemplateResponse("dataset_new.html", {"request": request})


@app.post("/dataset/new")
def dataset_new_submit(
    serial: str = Form(...),
    model: str = Form(...),
    fw_version: str = Form(""),
    cleaned_log: str = Form(""),
    llm_output: str = Form(""),
    ground_truth: str = Form(""),
):
    crud.create_case(serial, model, fw_version, cleaned_log, llm_output, ground_truth)
    return RedirectResponse("/dataset", status_code=303)


@app.get("/dataset/{case_id}/edit")
def dataset_edit(request: Request, case_id: int):
    row = crud.get_case(case_id)
    if row is None:
        return HTMLResponse("<h1>Case not found</h1>", status_code=404)
    return templates.TemplateResponse("dataset_edit.html", {"request": request, "case": row})


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
    crud.update_case(case_id, serial, model, fw_version, cleaned_log, llm_output, ground_truth)
    return RedirectResponse(f"/dataset/{case_id}", status_code=303)


@app.post("/dataset/{case_id}/delete")
def dataset_delete(case_id: int):
    crud.delete_case(case_id)
    return RedirectResponse("/dataset", status_code=303)


@app.get("/dataset/{case_id}")
def dataset_view(request: Request, case_id: int):
    row = crud.get_case(case_id)
    if row is None:
        return HTMLResponse("<h1>Case not found</h1>", status_code=404)
    return templates.TemplateResponse("dataset_view.html", {"request": request, "case": row})
