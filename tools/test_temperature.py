"""
tools/test_temperature.py
用法：python tools/test_temperature.py <log_folder>

測試 temperature 模組：從 Hs*.log 抓 GetSensorTemp 資料並輸出統計。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from parsers.temperature import build_temperature_section


def main():
    if len(sys.argv) != 2:
        print("用法：python tools/test_temperature.py <log_folder>")
        sys.exit(1)

    folder = Path(sys.argv[1])
    if not folder.is_dir():
        print(f"錯誤：找不到資料夾 {folder}")
        sys.exit(1)

    log_files = list(folder.glob("Hs*.log"))
    print(f"[1] 找到 {len(log_files)} 個 Hs*.log 檔案")
    for f in log_files:
        print(f"    - {f.name}")

    print(f"\n[2] 分析溫度資料...")
    result = build_temperature_section(folder)

    if not result:
        print("    → 未找到任何 GetSensorTemp 資料，請確認 log 檔案。")
        sys.exit(0)

    print("\n" + "=" * 80)
    print(result)


if __name__ == "__main__":
    main()
