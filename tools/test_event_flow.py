"""
tools/test_event_flow.py
用法：python tools/test_event_flow.py <log_folder> [top_n]

測試 event_flow 模組：分段、萃取 event、統計 path。
top_n 預設 20，可自訂要顯示幾個最常見的 flow。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from event_flow import build_path_map, format_flow, format_flow_detail


def main():
    if len(sys.argv) < 2:
        print("用法：python tools/test_event_flow.py <log_folder> [top_n]")
        sys.exit(1)

    folder = Path(sys.argv[1])
    if not folder.is_dir():
        print(f"錯誤：找不到資料夾 {folder}")
        sys.exit(1)

    top_n = int(sys.argv[2]) if len(sys.argv) >= 3 else 20

    print(f"[1] 讀取並分段 log（top_n={top_n}）...")
    counter, samples, total_lines, total_segments = build_path_map(folder)
    print(f"    → 總行數：{total_lines}")
    print(f"    → 總 segment 數：{total_segments}")
    print(f"    → 不重複 path 數：{len(counter)}")

    print("\n" + format_flow(counter, total_lines, total_segments, top_n=top_n))
    print(format_flow_detail(counter, samples, top_n=top_n))


if __name__ == "__main__":
    main()
