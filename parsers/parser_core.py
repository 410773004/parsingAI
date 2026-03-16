from pathlib import Path

from .filter import load_settings, run_filter


def parse_logs(search_json: str | Path, log_folder: str | Path) -> str:
    settings = load_settings(search_json)
    return run_filter(settings, log_folder)