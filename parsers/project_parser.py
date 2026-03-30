#project_parser.py
from pathlib import Path
import re
import config
from .filter import load_settings, run_filter
from .simplifier import simplify_text


BASE_DIR = Path(__file__).resolve().parent.parent

SEARCH_JSON_MAP = {
    "PJ1": BASE_DIR / "search_strings" / "PJ_search_string.json",
    "ER3": BASE_DIR / "search_strings" / "ER_search_string.json",
    # "CX3": BASE_DIR / "search_strings" / "CX3_search_string.json",
}


PJ_FW_RE = re.compile(r"LJ1\(SSSTC\)\s+FW\s+(FG[A-Za-z0-9]+)")
PJ_SN_RE = re.compile(r"\bSN:\s+([A-Za-z0-9]+)\b")

ER_FW_RE = re.compile(r"\b(F2N\d+)\s+FW\s+SubVersion:")
ER_SN_RE = re.compile(r"Device_SN:\s*\n\s*([A-Za-z0-9]+)\b", re.MULTILINE)

def parse_logs(search_json: str | Path, log_folder: str | Path) -> str:
    settings = load_settings(search_json)
    raw_output = run_filter(settings, log_folder)
    return simplify_text(raw_output)

def parse(project: str, log_folder: str | Path) -> str:
    search_json = SEARCH_JSON_MAP.get(project)
    if not search_json:
        raise ValueError(f"unsupported project: {project}")
    return parse_logs(search_json, log_folder)


def detect_project_from_raw_logs(log_folder: str | Path) -> str:
    log_folder = Path(log_folder)

    for p in log_folder.rglob("*.log"):
        try:
            text = p.read_text(errors="ignore")[:5000]

            if "LJ1(SSSTC)" in text:
                return "PJ1"

            if "SSSTC ER3-" in text or "FWInfo" in text:
                return "ER3"

        except Exception:
            continue

    return ""


def extract_serial_from_folder_name(folder_name: str) -> str:
    sn_re = re.compile(r"SN([A-Za-z0-9]{12})", re.IGNORECASE)
    m = sn_re.search(folder_name or "")
    if m:
        return f"SN{m.group(1)}".upper()
    return ""


def extract_metadata_from_raw_logs(log_folder: str | Path, folder_name: str = "") -> dict:
    log_folder = Path(log_folder)

    project = ""
    fw_version = ""
    serial = extract_serial_from_folder_name(folder_name)

    for p in log_folder.rglob("*.log"):
        try:
            text = p.read_text(errors="ignore")

            # 先判 project
            if not project:
                if "LJ1(SSSTC)" in text:
                    project = "PJ1"
                elif "SSSTC ER3-" in text or "FWInfo" in text:
                    project = "ER3"

            # PJ1 規則
            if project == "PJ1":
                if not fw_version:
                    m = PJ_FW_RE.search(text)
                    if m:
                        fw_version = m.group(1)

                if not serial:
                    s = PJ_SN_RE.search(text)
                    if s:
                        serial = f"SN{s.group(1)}"

            # ER3 規則
            elif project == "ER3":
                if not fw_version:
                    m = ER_FW_RE.search(text)
                    if m:
                        fw_version = m.group(1)

                if not serial:
                    s = ER_SN_RE.search(text)
                    if s:
                        serial = f"SN{s.group(1)}"

            if project and fw_version and serial:
                break

        except Exception:
            continue

    return {
        "project": project,
        "fw_version": fw_version,
        "serial": serial,
    }

def build_temperature_section(log_folder: str | Path) -> str:
    log_folder = Path(log_folder)

    temp_re = re.compile(
        r"Idx:(\d+),Temp:([-]?\d+),\s*soc:\s*(\d+)",
        re.IGNORECASE
    )

    temps = []
    socs = []

    for log_file in log_folder.glob("Hs*.log"):
        with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if "GetSensorTemp" not in line:
                    continue

                m = temp_re.search(line)
                if not m:
                    continue

                temp = int(m.group(2))
                soc = int(m.group(3))

                temps.append(temp)
                socs.append(soc)

    if not temps:
        return ""

    sample_count = len(temps)
    temp_min = min(temps)
    temp_max = max(temps)
    temp_avg = sum(temps) / sample_count

    soc_min = min(socs)
    soc_max = max(socs)
    soc_avg = sum(socs) / sample_count

    low_count = sum(1 for t in temps if t < config.TEMP_LOW_THRESHOLD)
    high_count = sum(1 for t in temps if t > config.TEMP_HIGH_THRESHOLD)

    return f"""================================================================================
TEMPERATURE SUMMARY
================================================================================
Sample Count    : {sample_count}
Temp Min / Max  : {temp_min} / {temp_max}
Temp Avg        : {temp_avg:.1f}
SOC Min / Max   : {soc_min} / {soc_max}
SOC Avg         : {soc_avg:.1f}
Below {config.TEMP_LOW_THRESHOLD}C Count : {low_count}
Above {config.TEMP_HIGH_THRESHOLD}C Count : {high_count}"""