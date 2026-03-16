import json
import re
from pathlib import Path
from typing import Any


def load_settings(json_filepath: str | Path) -> dict:
    json_path = Path(json_filepath)

    with json_path.open("r", encoding="utf-8") as f:
        settings = json.load(f)

    settings["keywords"] = [
        k for k in settings.get("keywords", [])
        if k.get("enabled", True)
    ]

    return settings


def find_log_files(log_folder: str | Path) -> list[Path]:
    logs=[]
    for f in log_folder.glob("*.log"):
        name=f.name.lower()

        if(
            name.startswith("hs")
            or "norlog" in name
            or "lognor" in name
        ):
            logs.append(f)
    return sorted(logs)



def process_file_info(full_path: Path, keywords_with_counts: list[dict]) -> list[dict[str, Any]]:
    """
    info 類 keyword:
    找到 keyword 後，往下擷取 lines_to_capture 行（含該行）
    """
    results: list[dict[str, Any]] = []

    try:
        with full_path.open("r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Can't read the file {full_path}: {e}")
        return results

    hit_keywords: set[str] = set()
    powercycle_count = 0

    for idx, line in enumerate(lines):
        if "LJ1(SSSTC) FW" in line:
            powercycle_count += 1

        for temp in keywords_with_counts:
            keyword = str(temp["keyword"])
            cap = int(temp.get("lines_to_capture", 1) or 1)

            if keyword == "powercycle_count":
                continue

            if keyword in line:
                start = idx
                end = min(len(lines), idx + cap)
                extracted = lines[start:end]

                results.append(
                    {
                        "file": full_path.name,
                        "kind": "info",
                        "keyword": keyword,
                        "start_line": start + 1,
                        "lines": extracted,
                    }
                )
                hit_keywords.add(keyword)

    for temp in keywords_with_counts:
        keyword = str(temp["keyword"])

        if keyword == "powercycle_count":
            results.append(
                {
                    "file": full_path.name,
                    "kind": "info",
                    "keyword": keyword,
                    "start_line": None,
                    "lines": [f"{powercycle_count} time(s)\n"],
                }
            )
            continue

        if keyword not in hit_keywords:
            results.append(
                {
                    "file": full_path.name,
                    "kind": "info",
                    "keyword": keyword,
                    "start_line": 0,
                    "lines": [" not found.\n"],
                }
            )

    return results


def process_file_regex(full_path: Path, keywords_with_counts: list[dict]) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []

    try:
        with full_path.open("r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Can't read the file {full_path}: {e}")
        return results

    compiled: list[tuple[str, re.Pattern[str]]] = []

    for temp in keywords_with_counts:
        regex_pattern = temp.get("regex", "")
        if not regex_pattern:
            continue
        compiled.append((str(temp["keyword"]), re.compile(regex_pattern)))

    for idx, line in enumerate(lines):
        for keyword, rx in compiled:
            if rx.search(line):
                results.append(
                    {
                        "file": full_path.name,
                        "kind": "regex",
                        "keyword": keyword,
                        "start_line": idx + 1,
                        "lines": [line],
                    }
                )

    return results


def process_file_error(full_path: Path, keywords_with_counts: list[dict]) -> list[dict[str, Any]]:
    """
    error 類 keyword:
    - 一般情況：從關鍵字行往前抓 lines_to_capture 行（含該行）
    - 若 keyword == 'evt by cpu' 且該行含 panic：從該行往後抓
    - 原文保留，不做清洗
    """
    results: list[dict[str, Any]] = []

    try:
        with full_path.open("r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Can't read the file {full_path}: {e}")
        return results

    hit_keywords: set[str] = set()

    for idx, line in enumerate(lines):
        for temp in keywords_with_counts:
            keyword = str(temp["keyword"])
            cap = int(temp.get("lines_to_capture", 1) or 1)

            if keyword in line:
                # 例外：evt by cpu 若是 PCIE Link Error，直接跳過不抓
                if keyword == "evt by cpu" and "PCIE Link Error" in line:
                    continue

                # panic 特例：從命中行往後抓
                if keyword == "evt by cpu" and "panic" in line:
                    start = idx
                    end = min(len(lines), idx + cap)
                else:
                    # 一般 error：從命中行往前抓（含命中行）
                    start = max(0, idx - cap + 1)
                    end = idx + 1

                extracted = lines[start:end]

                results.append(
                    {
                        "file": full_path.name,
                        "kind": "error",
                        "keyword": keyword,
                        "start_line": start + 1,
                        "lines": extracted,
                    }
                )
                hit_keywords.add(keyword)

    for temp in keywords_with_counts:
        keyword = str(temp["keyword"])
        if keyword not in hit_keywords:
            results.append(
                {
                    "file": full_path.name,
                    "kind": "error",
                    "keyword": keyword,
                    "start_line": 0,
                    "lines": [" not found.\n"],
                }
            )

    return results


def run_filter(settings: dict, log_folder: str | Path) -> list[dict[str, Any]]:
    files = find_log_files(log_folder)

    info_keywords = [k for k in settings["keywords"] if k.get("type") == "info"]
    regex_keywords = [k for k in settings["keywords"] if k.get("type") == "regex"]
    error_keywords = [k for k in settings["keywords"] if k.get("type") == "error"]

    print(f"Parsing files: {[f.name for f in files]}")
    print(f"Info keywords: {[k['keyword'] for k in info_keywords]}")
    print(f"Error keywords: {[k['keyword'] for k in error_keywords]}")
    print()

    all_results: list[dict[str, Any]] = []

    for full_path in files:
        if info_keywords:
            all_results.extend(process_file_info(full_path, info_keywords))

        if regex_keywords:
            all_results.extend(process_file_regex(full_path, regex_keywords))

        if error_keywords:
            all_results.extend(process_file_error(full_path, error_keywords))

    return all_results