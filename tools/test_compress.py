"""
tools/test_compress.py
用法：python tools/test_compress.py <log_file>

測試 compress 模組：對單一 log 檔執行壓縮，印出壓縮前後的行數與 token 統計。
壓縮結果同時寫出到 <原檔名>_compressed.txt。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from compress import process_lines, count_tokens


def main():
    if len(sys.argv) != 2:
        print("用法：python tools/test_compress.py <log_file>")
        sys.exit(1)

    input_path = Path(sys.argv[1])
    if not input_path.is_file():
        print(f"錯誤：找不到檔案 {input_path}")
        sys.exit(1)

    print(f"[1] 讀取 {input_path.name}...")
    with input_path.open("r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    original_text = "".join(lines)
    original_tokens = count_tokens(original_text)
    print(f"    → 原始行數：{len(lines)}")
    print(f"    → 原始 token 數：{original_tokens}")

    print(f"\n[2] 執行壓縮...")
    result = process_lines(lines)
    compressed_text = "\n".join(result)
    compressed_tokens = count_tokens(compressed_text)

    reduction = (1 - compressed_tokens / original_tokens) * 100 if original_tokens > 0 else 0
    print(f"    → 壓縮後行數：{len(result)}")
    print(f"    → 壓縮後 token 數：{compressed_tokens}")
    print(f"    → Token 減少：{reduction:.1f}%")

    output_path = input_path.with_name(input_path.stem + "_compressed.txt")
    with output_path.open("w", encoding="utf-8") as f:
        f.write(compressed_text)
    print(f"\n[3] 輸出檔案：{output_path}")

    print("\n" + "=" * 80)
    print("壓縮結果預覽（前 50 行）")
    print("=" * 80)
    for line in result[:50]:
        print(line)
    if len(result) > 50:
        print(f"... 還有 {len(result) - 50} 行")


if __name__ == "__main__":
    main()
