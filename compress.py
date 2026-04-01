import re
import json
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox

try:
    import tiktoken
    ENC = tiktoken.get_encoding("cl100k_base")
except Exception:
    ENC = None


HEX_RE = re.compile(r"0x[0-9A-Fa-f]+")
NUM_RE = re.compile(r"-?\d+")
RAW_HEX_RE = re.compile(r"\b[0-9A-Fa-f]{6,}\b")


def load_rules() -> dict:
    rule_path = Path(__file__).with_name("function_rules.json")
    if rule_path.exists():
        with rule_path.open("r", encoding="utf-8") as f:
            return json.load(f)
    return {
        "drop_prefix_only": [],
        "drop_entire_line": [],
    }


RULES = load_rules()


def count_tokens(text: str) -> int:
    if ENC:
        return len(ENC.encode(text))
    return max(1, len(text) // 4) if text else 0


def is_noise_line(line: str) -> bool:
    s = line.strip().lower()

    if "getsensortemp()" in s:
        return True

    return False


def get_func_name(line: str) -> str:
    s = line.strip()
    if "()" in s:
        return s.split("()", 1)[0]
    return ""


def apply_function_rules(line: str) -> str | None:
    s = line.rstrip("\n")
    s_stripped = s.strip()
    s_lower = s_stripped.lower()

    # 1. 整行刪除：看前綴
    for f in RULES.get("drop_entire_line", []):
        key = f.strip().lower()
        if s_lower.startswith(key):
            return None

    # 2. 刪前綴後面的內容，只保留前綴名
    for f in RULES.get("drop_prefix_only", []):
        key = f.strip().lower()
        if s_lower.startswith(key):
            # 保留 JSON 裡定義的名字，順便把 () 去掉
            return f.replace("()", "")

    return s


def normalize_line(line: str) -> str:
    s = line.rstrip("\n")

    if "nvmet_get_log_page()" in s:
        return "nvmet_get_log_page() - GetLogPage: LID(*) NUMD(*) PRP1(*) PRP2(*)"

    s = HEX_RE.sub("*", s)
    s = RAW_HEX_RE.sub("*", s)
    s = NUM_RE.sub("*", s)
    return s


def preprocess_lines(lines: list[str]) -> list[str]:
    filtered = []
    for l in lines:

        new_line = apply_function_rules(l)
        if new_line is None:
            continue

        filtered.append(new_line)

    merged = []
    i = 0
    while i < len(filtered):
        line = filtered[i]

        # merge nvmet_get_log_page() + next line P2(...)
        if "nvmet_get_log_page()" in line and i + 1 < len(filtered):
            next_line = filtered[i + 1].strip()
            if next_line.startswith("P2("):
                merged.append(line + next_line)
                i += 2
                continue

        # merge broken get_host_ns_spare_cnt line
        if "get_host_ns_spare_cnt()" in line and i + 1 < len(filtered):
            next_line = filtered[i + 1].strip()
            if "alloc pool cnt" in next_line:
                merged.append(line + " " + next_line)
                i += 2
                continue

        merged.append(line)
        i += 1

    return merged


def compress_adjacent_lines(lines: list[str]) -> list[str]:
    if not lines:
        return []

    out = []
    prev_raw = lines[0]
    prev_norm = normalize_line(prev_raw)
    count = 1

    for raw in lines[1:]:
        norm = normalize_line(raw)

        if norm == prev_norm:
            count += 1
        else:
            if count == 1:
                out.append(prev_raw)
            else:
                out.append(f"[{count}x] {prev_norm}")
            prev_raw = raw
            prev_norm = norm
            count = 1

    if count == 1:
        out.append(prev_raw)
    else:
        out.append(f"[{count}x] {prev_norm}")

    return out


def is_metric_pair(line1: str, line2: str) -> bool:
    return (
        line1.startswith("get_avg_erase_cnt()")
        and line2.startswith("get_host_ns_spare_cnt()")
    )


def is_patrol_block(line: str) -> bool:
    return "patrol_read() - blank block flag" in line


def compress_metric_pairs(lines: list[str]) -> list[str]:
    out = []
    i = 0
    n = len(lines)

    while i < n:
        if i + 1 < n:
            l1 = lines[i]
            l2 = lines[i + 1]

            if is_metric_pair(l1, l2):
                count = 1
                j = i + 2

                while j + 1 < n:
                    n1 = lines[j]
                    n2 = lines[j + 1]

                    if is_metric_pair(n1, n2):
                        count += 1
                        j += 2
                    else:
                        break

                if count == 1:
                    out.extend([l1, l2])
                else:
                    out.append(f"[{count}x pair]")
                    out.append("{")
                    out.append("get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *")
                    out.append(
                        "get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *"
                    )
                    out.append("}")

                i = j
                continue

        out.append(lines[i])
        i += 1

    return out


def compress_metric_patrol_loop(lines: list[str]) -> list[str]:
    out = []
    i = 0
    n = len(lines)

    while i < n:
        if i + 2 < n:
            l1 = lines[i]
            l2 = lines[i + 1]
            l3 = lines[i + 2]

            if is_metric_pair(l1, l2) and is_patrol_block(l3):
                count = 1
                j = i + 3

                while j + 2 < n:
                    n1 = lines[j]
                    n2 = lines[j + 1]
                    n3 = lines[j + 2]

                    if is_metric_pair(n1, n2) and is_patrol_block(n3):
                        count += 1
                        j += 3
                    else:
                        break

                if count == 1:
                    out.extend([l1, l2, l3])
                else:
                    out.append(f"[{count}x loop]")
                    out.append("{")
                    out.append("get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *")
                    out.append(
                        "get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *"
                    )
                    out.append("patrol_read() - blank block flag *, skip it")
                    out.append("}")

                i = j
                continue

        out.append(lines[i])
        i += 1

    return out


def is_epm_hdr_start(line: str) -> bool:
    return line == "epm_header_update" or line.startswith("epm_header_update() - epm_header_update")


def is_epm_hdr_done(line: str) -> bool:
    return line == "epm_header_update" or line.startswith("epm_header_update() - epm_header_update done")


def is_epm_sign(line: str) -> bool:
    return line == "epm_update" or line.startswith("epm_update() - epm_update epm_sign")


def is_epm_data_done(line: str) -> bool:
    return line == "epm_update" or line.startswith("epm_update() - epm_data_update done")


def compress_epm_update_blocks(lines: list[str]) -> list[str]:
    out = []
    i = 0
    n = len(lines)

    while i < n:
        if i + 3 < n:
            l1 = lines[i]
            l2 = lines[i + 1]
            l3 = lines[i + 2]
            l4 = lines[i + 3]

            if (
                is_epm_hdr_start(l1)
                and is_epm_hdr_done(l2)
                and is_epm_sign(l3)
                and is_epm_data_done(l4)
            ):
                count = 1
                j = i + 4

                while j + 3 < n:
                    n1 = lines[j]
                    n2 = lines[j + 1]
                    n3 = lines[j + 2]
                    n4 = lines[j + 3]

                    if (
                        is_epm_hdr_start(n1)
                        and is_epm_hdr_done(n2)
                        and is_epm_sign(n3)
                        and is_epm_data_done(n4)
                    ):
                        count += 1
                        j += 4
                    else:
                        break

                if count == 1:
                    out.extend([l1, l2, l3, l4])
                else:
                    out.append(f"[{count}x epm_update_block]")
                    out.append("{")
                    out.append("epm_header_update")
                    out.append("epm_header_update")
                    out.append("epm_update")
                    out.append("epm_update")
                    out.append("}")

                i = j
                continue

        out.append(lines[i])
        i += 1

    return out


def process_lines(lines: list[str]) -> list[str]:
    step0 = preprocess_lines(lines)
    step1 = compress_adjacent_lines(step0)
    step2 = compress_metric_patrol_loop(step1)
    step3 = compress_metric_pairs(step2)
    step4 = compress_epm_update_blocks(step3)
    return step4


def process_file():
    file_path = filedialog.askopenfilename(
        title="選擇 log / txt 檔",
        filetypes=[("Text Files", "*.txt *.log"), ("All Files", "*.*")]
    )

    if not file_path:
        return

    input_path = Path(file_path)
    output_path = input_path.with_name(input_path.stem + "_compressed.txt")

    try:
        with input_path.open("r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()

        original_text = "".join(lines)
        original_tokens = count_tokens(original_text)

        result = process_lines(lines)

        compressed_text = "\n".join(result)
        compressed_tokens = count_tokens(compressed_text)

        with output_path.open("w", encoding="utf-8") as f:
            f.write(compressed_text)

        reduction = 0.0
        if original_tokens > 0:
            reduction = (1 - compressed_tokens / original_tokens) * 100

        messagebox.showinfo(
            "完成",
            f"原始行數: {len(lines)}\n"
            f"壓縮後行數: {len(result)}\n\n"
            f"原始 token: {original_tokens}\n"
            f"壓縮後 token: {compressed_tokens}\n"
            f"減少: {reduction:.1f}%\n\n"
            f"輸出: {output_path}"
        )

    except Exception as e:
        messagebox.showerror("錯誤", str(e))


def main():
    root = tk.Tk()
    root.title("Log 壓縮工具")
    root.geometry("340x180")

    btn = tk.Button(
        root,
        text="選擇檔案並壓縮",
        command=process_file,
        height=2,
        width=20
    )
    btn.pack(expand=True)

    root.mainloop()


if __name__ == "__main__":
    main()