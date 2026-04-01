# services/analyzer.py
import re
from pathlib import Path

import ollama
import tiktoken

import config
import prompts
from parsers.project_parser import (
    parse as parse_project,
    detect_project_from_raw_logs,
    extract_metadata_from_raw_logs,
)
from parsers.temperature import build_temperature_section
from event_flow import analyze_event_flow

_SN_RE = re.compile(r"SN([A-Za-z0-9]{12})", re.IGNORECASE)

# Progress hook — server sets this to update /progress endpoint
_stage_callback = None


def set_stage_callback(fn):
    global _stage_callback
    _stage_callback = fn


def _stage(msg: str) -> None:
    if _stage_callback:
        _stage_callback(msg)


def _count_tokens(text: str) -> int:
    try:
        enc = tiktoken.get_encoding(config.TIKTOKEN_ENCODING)
        return len(enc.encode(text))
    except Exception:
        return max(1, len(text) // 4)


def _run_llm(cleaned: str, project: str, fw_version: str, issue: str) -> str:
    user_prompt = prompts.USER_TEMPLATE.format(
        model=project,
        fw_version=fw_version,
        issue=issue,
        log_text=cleaned,
    )
    resp = ollama.chat(
        model=config.MODEL,
        messages=[
            {"role": "system", "content": prompts.SYSTEM},
            {"role": "user", "content": user_prompt},
        ],
        options={
            "temperature": config.TEMPERATURE,
            "top_p": config.TOP_P,
            "top_k": config.TOP_K,
            "repeat_penalty": config.REPEAT_PENALTY,
            "num_ctx": config.NUM_CTX,
        },
    )
    return resp.get("message", {}).get("content", "")


def _inject_extra_sections(cleaned: str, sections: list[str]) -> str:
    if not sections:
        return cleaned
    block = "\n\n".join(sections)
    header = (
        "================================================================================\n"
        "EVENT DETAIL\n"
        "================================================================================"
    )
    if header in cleaned:
        return cleaned.replace(header, f"{block}\n\n{header}", 1)
    return cleaned + "\n\n" + block


def analyze(
    log_folder: Path,
    issue: str,
    folder_name: str = "",
) -> dict:
    """
    Run the full analysis pipeline on a folder of log files.

    Returns a dict with keys:
        project, fw_version, serial, cleaned_log,
        token_count, llm_output
    """
    _stage("偵測 project")
    project = detect_project_from_raw_logs(log_folder)
    if not project:
        raise ValueError("無法從 log 偵測 project 類型")

    _stage("解析 log")
    cleaned = parse_project(project, log_folder)

    _stage("分析 Event Flow")
    flow_block = analyze_event_flow(log_folder)

    _stage("分析溫度")
    temp_section = build_temperature_section(log_folder)

    extra = [s for s in [temp_section, flow_block] if s]
    cleaned = _inject_extra_sections(cleaned, extra)

    if not cleaned.strip():
        raise ValueError("清理後的 log 為空")

    _stage("抽取 metadata")
    meta = extract_metadata_from_raw_logs(log_folder, folder_name)
    project = meta.get("project") or project
    fw_version = meta.get("fw_version", "")
    serial = meta.get("serial", "")

    _stage("計算 token")
    token_count = _count_tokens(cleaned)

    _stage("LLM 分析中")
    llm_output = _run_llm(cleaned, project, fw_version, issue)

    return {
        "project": project,
        "fw_version": fw_version,
        "serial": serial,
        "cleaned_log": cleaned,
        "token_count": token_count,
        "llm_output": llm_output,
    }
