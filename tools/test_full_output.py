"""
tools/test_full_output.py
用法：python tools/test_full_output.py <log_folder> [output_file]

輸出完整分析結果：parsing + compress後的event_flow + temperature 合併成一個檔案。
若未指定 output_file，印到 stdout。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from parsers.project_parser import detect_project_from_raw_logs, parse, extract_metadata_from_raw_logs, SEARCH_JSON_MAP
from parsers.filter import load_settings
from parsers.temperature import build_temperature_section
from parsers.event_flow import build_path_map, build_compressed_flow
from parsers.compress import count_tokens


def main():
    if len(sys.argv) < 2:
        print("用法：python tools/test_full_output.py <log_folder> [output_file]")
        sys.exit(1)

    folder = Path(sys.argv[1])
    if not folder.is_dir():
        print(f"錯誤：找不到資料夾 {folder}")
        sys.exit(1)

    output_file = Path(sys.argv[2]) if len(sys.argv) >= 3 else None

    print("[1] 偵測 project 類型...")
    project = detect_project_from_raw_logs(folder)
    if not project:
        print("無法偵測 project 類型")
        sys.exit(1)
    print(f"    → Project: {project}")

    meta = extract_metadata_from_raw_logs(folder)
    print(f"    → FW Version : {meta.get('fw_version') or '(未找到)'}")
    print(f"    → Serial     : {meta.get('serial') or '(未找到)'}")

    print("[2] 解析 log...")
    parsed = parse(project, folder)

    print("[3] 分析 Event Flow 並壓縮...")
    _settings = load_settings(SEARCH_JSON_MAP[project])
    ignore = {s.lower() for s in _settings.get("ignore_event_signatures", [])}
    counter, samples, total_lines, total_segments = build_path_map(folder, ignore, project=project)
    compressed_flow = build_compressed_flow(counter, samples, total_lines, total_segments, project=project)

    print("[4] 分析溫度...")
    temp = build_temperature_section(folder, project=project)

    print("[5] 組合輸出...")
    sep = "=" * 80
    parts = [parsed]
    if temp:
        parts.append(f"{sep}\n{temp}")
    parts.append(compressed_flow)
    full_output = "\n\n".join(parts)

    token_count = count_tokens(full_output)
    print(f"    → Token 數：{token_count}")

    if output_file:
        output_file.write_text(full_output, encoding="utf-8")
        print(f"    → 已儲存到：{output_file}")
    else:
        print("\n" + "=" * 80)
        print(full_output)


if __name__ == "__main__":
    main()
