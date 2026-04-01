"""
tools/test_parser.py
用法：python tools/test_parser.py <log_folder>

測試 filter + simplifier + project_parser 的完整解析流程。
輸出解析後的 cleaned log 到 stdout，並印出基本統計。
"""
import sys
from pathlib import Path

# 讓 import 能找到上層模組
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from parsers.project_parser import detect_project_from_raw_logs, parse, extract_metadata_from_raw_logs


def main():
    if len(sys.argv) != 2:
        print("用法：python tools/test_parser.py <log_folder>")
        sys.exit(1)

    folder = Path(sys.argv[1])
    if not folder.is_dir():
        print(f"錯誤：找不到資料夾 {folder}")
        sys.exit(1)

    print(f"[1] 偵測 project 類型...")
    project = detect_project_from_raw_logs(folder)
    if not project:
        print("無法偵測 project 類型，請確認 log 檔案內容。")
        sys.exit(1)
    print(f"    → Project: {project}")

    print(f"[2] 抽取 metadata...")
    meta = extract_metadata_from_raw_logs(folder)
    print(f"    → FW Version : {meta.get('fw_version') or '(未找到)'}")
    print(f"    → Serial     : {meta.get('serial') or '(未找到)'}")

    print(f"[3] 解析 log...")
    cleaned = parse(project, folder)
    lines = cleaned.splitlines()
    print(f"    → 輸出行數：{len(lines)}")
    print(f"    → 輸出字元數：{len(cleaned)}")

    print("\n" + "=" * 80)
    print("CLEANED LOG OUTPUT")
    print("=" * 80)
    print(cleaned)


if __name__ == "__main__":
    main()
