# parsers/project_parser.py
from pathlib import Path
import re

from .filter import load_settings, run_filter, resolve_log_folder
from .simplifier import simplify_text

BASE_DIR = Path(__file__).resolve().parent.parent

SEARCH_JSON_MAP = {
    "PJ1": BASE_DIR / "search_strings" / "PJ_search_string.json",
    "ER3": BASE_DIR / "search_strings" / "ER_search_string.json",
}

_PJ_FW_RE = re.compile(r"LJ1\(SSSTC\)\s+FW\s+(FG[A-Za-z0-9]+)")
_PJ_SN_RE = re.compile(r"\bSN:\s+([A-Za-z0-9]+)\b")
_ER_FW_RE = re.compile(r"(F2[MN]\d+)\s+FW\s+SubVersion:")
_ER_SN_RE = re.compile(r"\d?Device_SN:\s*\n\s*([A-Za-z0-9]+)", re.MULTILINE)
_FOLDER_SN_RE = re.compile(r"SN([A-Za-z0-9]{12})", re.IGNORECASE)


def parse(project: str, log_folder: str | Path) -> str:
    search_json = SEARCH_JSON_MAP.get(project)
    if not search_json:
        raise ValueError(f"unsupported project: {project}")
    settings = load_settings(search_json)
    raw_output = run_filter(settings, log_folder, project=project)
    return simplify_text(raw_output)


def detect_project_from_raw_logs(log_folder: str | Path) -> str:
    for p in Path(log_folder).rglob("*.log"):
        try:
            text = p.read_text(errors="ignore")[:5000]
            if "LJ1(SSSTC)" in text:
                return "PJ1"
            if "SSSTC ER3-" in text or "FWInfo" in text:
                return "ER3"
        except Exception:
            continue
    return ""


def extract_metadata_from_raw_logs(
    log_folder: str | Path, folder_name: str = ""
) -> dict:
    log_folder = resolve_log_folder(Path(log_folder))
    project = ""
    fw_version = ""

    m = _FOLDER_SN_RE.search(folder_name or "")
    serial = f"SN{m.group(1)}".upper() if m else ""

    log_files = list(log_folder.glob("Hs*.log"))
    log_files += [f for f in log_folder.glob("*.log") if f.name not in {l.name for l in log_files}]

    for p in log_files:
        try:
            text = p.read_text(errors="ignore")

            if not project:
                if "LJ1(SSSTC)" in text:
                    project = "PJ1"
                elif "SSSTC ER3-" in text or "FWInfo" in text:
                    project = "ER3"

            if project == "PJ1":
                if not fw_version:
                    if hit := _PJ_FW_RE.search(text):
                        fw_version = hit.group(1)
                if not serial:
                    if hit := _PJ_SN_RE.search(text):
                        serial = f"SN{hit.group(1)}"
            elif project == "ER3":
                if not fw_version:
                    if hit := _ER_FW_RE.search(text):
                        fw_version = hit.group(1)
                if not serial:
                    if hit := _ER_SN_RE.search(text):
                        serial = f"SN{hit.group(1)}"

            if project and fw_version and serial:
                break
        except Exception:
            continue

    return {"project": project, "fw_version": fw_version, "serial": serial}
