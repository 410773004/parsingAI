# parsers/temperature.py
import re
from pathlib import Path
from settings import config

_TEMP_RE = re.compile(
    r"Idx:(\d+),Temp:([-]?\d+),\s*soc:\s*(\d+)",
    re.IGNORECASE,
)

# ER3: "  Temperature =   75  degree,Sensor =   42, Poh = 4098, ..."
_ER3_TEMP_RE = re.compile(
    r"Temperature\s*=\s*([-]?\d+)\s+degree,Sensor\s*=\s*(\d+)",
    re.IGNORECASE,
)


def build_temperature_section(log_folder: str | Path, project: str = "PJ1") -> str:
    from parsers.filter import resolve_log_folder
    log_folder = resolve_log_folder(Path(log_folder))
    temps: list[int] = []
    socs: list[int] = []

    if project == "ER3":
        for log_file in sorted(log_folder.glob("Hs*.log")):
            with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    m = _ER3_TEMP_RE.search(line)
                    if not m:
                        continue
                    temps.append(int(m.group(1)))
                    socs.append(int(m.group(2)))
    else:
        for log_file in log_folder.glob("Hs*.log"):
            with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    if "GetSensorTemp" not in line:
                        continue
                    m = _TEMP_RE.search(line)
                    if not m:
                        continue
                    temps.append(int(m.group(2)))
                    socs.append(int(m.group(3)))

    if not temps:
        return ""

    count = len(temps)
    soc_label = "Sensor" if project == "ER3" else "SOC"
    return (
        "================================================================================\n"
        "TEMPERATURE SUMMARY\n"
        "================================================================================\n"
        f"Sample Count    : {count}\n"
        f"Temp Min / Max  : {min(temps)} / {max(temps)}\n"
        f"Temp Avg        : {sum(temps) / count:.1f}\n"
        f"{soc_label} Min / Max   : {min(socs)} / {max(socs)}\n"
        f"{soc_label} Avg         : {sum(socs) / count:.1f}\n"
        f"Below {config.TEMP_LOW_THRESHOLD}C Count : {sum(1 for t in temps if t < config.TEMP_LOW_THRESHOLD)}\n"
        f"Above {config.TEMP_HIGH_THRESHOLD}C Count : {sum(1 for t in temps if t > config.TEMP_HIGH_THRESHOLD)}"
    )
