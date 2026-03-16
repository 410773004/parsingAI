from pathlib import Path

from .parser_core import parse_logs


BASE_DIR = Path(__file__).resolve().parent.parent
ER_JSON = BASE_DIR / "search_strings" / "ER_search_string.json"


def parse(log_folder: str | Path) -> str:
    return parse_logs(ER_JSON, log_folder)