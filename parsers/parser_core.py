from pathlib import Path

from .filter import load_settings, run_filter
from .simplifier import simplify_results


def parse_logs(search_json: str | Path, log_folder: str | Path) -> str:
    settings = load_settings(search_json)
    results = run_filter(settings, log_folder)
    cleaned_log = simplify_results(results)
    return cleaned_log