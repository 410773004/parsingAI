from pathlib import Path

from parsers.filter import load_settings, run_filter
from parsers.simplifier import simplify_text




import re

PJ_FW_RE = re.compile(r"\b(LJ1)\(SSSTC\)\s+FW\s+(FG\d+)\b")
PJ_SN_RE = re.compile(r"\bSN:\s([A-Za-z0-9]+)\b")

ER_MODEL_RE = re.compile(r"\bSSSTC\s+(ER3-[A-Za-z0-9\-]+)\b")
ER_FW_RE = re.compile(r"\b(F2N\d+)\s+FW\s+SubVersion:")
ER_SN_RE = re.compile(r"Device_SN:\s*\n\s*([A-Za-z0-9]+)\b", re.MULTILINE)


def detect_project_from_raw_logs(log_folder: str | Path) -> str:
    log_folder = Path(log_folder)

    for p in log_folder.rglob("*.log"):
        try:
            text = p.read_text(errors="ignore")[:5000]

            # ===== PJ =====
            if "LJ1(SSSTC)" in text:
                return "PJ1"

            # ===== ER =====
            if "SSSTC ER3-" in text or "FWInfo" in text:
                return "ER3"

        except Exception:
            continue

    return ""

# ===== PJ =====
PJ_FW_RE = re.compile(r"LJ1\(SSSTC\)\s+FW\s+(FG\d+)")
PJ_SN_RE = re.compile(r"\bSN:\s([A-Za-z0-9]+)")  # 冒號後要有空格


# ===== ER =====
ER_FW_RE = re.compile(r"(F2N\d+)\s+FW\s+SubVersion:")
ER_SN_RE = re.compile(r"Device_SN:\s*\n\s*([A-Za-z0-9]+)", re.MULTILINE)


def extract_metadata_from_text(text: str) -> dict:
    project = ""
    fw_version = ""
    serial = ""

    # =========================
    # PJ 規則
    # =========================
    m = PJ_FW_RE.search(text)
    if m:
        project = "PJ1"
        fw_version = m.group(1)

        s = PJ_SN_RE.search(text)
        if s:
            serial = f"SN{s.group(1)}"

        return {
            "project": project,
            "fw_version": fw_version,
            "serial": serial,
        }

    # =========================
    # ER 規則
    # =========================
    m_fw = ER_FW_RE.search(text)
    if m_fw:
        project = "ER3"
        fw_version = m_fw.group(1)

    m_sn = ER_SN_RE.search(text)
    if m_sn:
        serial = f"SN{m_sn.group(1)}"

    return {
        "project": project,
        "fw_version": fw_version,
        "serial": serial,
    }