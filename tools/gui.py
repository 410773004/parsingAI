# tools/gui.py
# 用法：python tools/gui.py
import sys
import threading
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from parsers.project_parser import (
    detect_project_from_raw_logs,
    parse,
    extract_metadata_from_raw_logs,
    SEARCH_JSON_MAP,
)
from parsers.filter import load_settings
from parsers.temperature import build_temperature_section
from parsers.pcie_link import build_pcie_link_section
from parsers.event_flow import build_path_map, format_flow, format_flow_detail, build_compressed_flow
from parsers.compress import process_lines, count_tokens


def _load_ignore(folder) -> set[str]:
    project = detect_project_from_raw_logs(folder)
    if project and project in SEARCH_JSON_MAP:
        s = load_settings(SEARCH_JSON_MAP[project])
        return {e.lower() for e in s.get("ignore_event_signatures", [])}
    return set()


# ── 共用元件 ──────────────────────────────────────────────────────────────────

_last_upload_dir = [""]


def make_folder_row(parent, label_text: str) -> tk.StringVar:
    """資料夾選擇列，回傳路徑 StringVar。"""
    var = tk.StringVar()
    frame = tk.Frame(parent)
    frame.pack(fill="x", padx=12, pady=(10, 0))
    tk.Label(frame, text=label_text, width=10, anchor="w").pack(side="left")
    tk.Entry(frame, textvariable=var, width=50).pack(side="left", padx=4)

    def browse():
        chosen = filedialog.askdirectory(initialdir=_last_upload_dir[0] or None)
        if chosen:
            var.set(chosen)
            _last_upload_dir[0] = chosen

    tk.Button(frame, text="瀏覽", command=browse).pack(side="left")
    return var


def make_file_row(parent, label_text: str) -> tk.StringVar:
    """單檔選擇列，回傳路徑 StringVar。"""
    var = tk.StringVar()
    frame = tk.Frame(parent)
    frame.pack(fill="x", padx=12, pady=(10, 0))
    tk.Label(frame, text=label_text, width=10, anchor="w").pack(side="left")
    tk.Entry(frame, textvariable=var, width=50).pack(side="left", padx=4)
    tk.Button(
        frame, text="瀏覽",
        command=lambda: var.set(
            filedialog.askopenfilename(filetypes=[("Log files", "*.log *.txt"), ("All", "*.*")])
        )
    ).pack(side="left")
    return var


def make_output(parent) -> scrolledtext.ScrolledText:
    """輸出區，回傳 ScrolledText widget。"""
    out = scrolledtext.ScrolledText(parent, wrap="none", font=("Courier New", 10), state="disabled")
    out.pack(fill="both", expand=True, padx=12, pady=8)
    return out


def write_output(widget: scrolledtext.ScrolledText, text: str) -> None:
    widget.configure(state="normal")
    widget.delete("1.0", "end")
    widget.insert("end", text)
    widget.configure(state="disabled")


def run_in_thread(fn):
    """在背景執行緒跑 fn，避免 UI 卡住。"""
    threading.Thread(target=fn, daemon=True).start()


# ── Tab 1：Parser ──────────────────────────────────────────────────────────────

def build_parser_tab(nb: ttk.Notebook) -> None:
    tab = ttk.Frame(nb)
    nb.add(tab, text="  Parser  ")

    folder_var = make_folder_row(tab, "Log 資料夾")

    btn_frame = tk.Frame(tab)
    btn_frame.pack(fill="x", padx=12, pady=6)
    status = tk.StringVar(value="")
    tk.Label(btn_frame, textvariable=status, fg="gray").pack(side="left", padx=6)

    out = make_output(tab)

    def run():
        folder = Path(folder_var.get())
        if not folder.is_dir():
            messagebox.showerror("錯誤", "請先選擇有效的 Log 資料夾")
            return

        def task():
            status.set("偵測 project 中...")
            project = detect_project_from_raw_logs(folder)
            if not project:
                status.set("")
                messagebox.showerror("錯誤", "無法偵測 project 類型")
                return
            meta = extract_metadata_from_raw_logs(folder)
            status.set("解析 log 中...")
            cleaned = parse(project, folder)
            lines = cleaned.splitlines()
            header = (
                f"Project    : {project}\n"
                f"FW Version : {meta.get('fw_version') or '(未找到)'}\n"
                f"Serial     : {meta.get('serial') or '(未找到)'}\n"
                f"輸出行數   : {len(lines)}\n"
                f"輸出字元數 : {len(cleaned)}\n"
                f"{'=' * 80}\n"
            )
            write_output(out, header + cleaned)
            status.set("完成")

        run_in_thread(task)

    tk.Button(btn_frame, text="執行解析", command=run, width=12).pack(side="left")


# ── Tab 2：Event Flow ─────────────────────────────────────────────────────────

def build_event_flow_tab(nb: ttk.Notebook) -> None:
    tab = ttk.Frame(nb)
    nb.add(tab, text="  Event Flow  ")

    folder_var = make_folder_row(tab, "Log 資料夾")

    ctrl = tk.Frame(tab)
    ctrl.pack(fill="x", padx=12, pady=6)
    tk.Label(ctrl, text="Top N").pack(side="left")
    top_n_var = tk.IntVar(value=20)
    tk.Spinbox(ctrl, from_=1, to=100, textvariable=top_n_var, width=5).pack(side="left", padx=4)
    status = tk.StringVar(value="")
    tk.Label(ctrl, textvariable=status, fg="gray").pack(side="left", padx=10)

    out = make_output(tab)

    def run():
        folder = Path(folder_var.get())
        if not folder.is_dir():
            messagebox.showerror("錯誤", "請先選擇有效的 Log 資料夾")
            return

        def task():
            status.set("分析中...")
            top_n = top_n_var.get()
            project = detect_project_from_raw_logs(folder) or "PJ1"
            counter, samples, total_lines, total_segments = build_path_map(folder, _load_ignore(folder), project=project)
            result = (
                f"總行數      : {total_lines}\n"
                f"總 segment  : {total_segments}\n"
                f"不重複 path : {len(counter)}\n"
                f"{'=' * 80}\n\n"
                + format_flow(counter, total_lines, total_segments, top_n=top_n)
                + "\n"
                + format_flow_detail(counter, samples, top_n=top_n, project=project)
            )
            write_output(out, result)
            status.set("完成")

        run_in_thread(task)

    tk.Button(ctrl, text="執行分析", command=run, width=12).pack(side="left")


# ── Tab 3：Temperature ────────────────────────────────────────────────────────

def build_temperature_tab(nb: ttk.Notebook) -> None:
    tab = ttk.Frame(nb)
    nb.add(tab, text="  Temperature  ")

    folder_var = make_folder_row(tab, "Log 資料夾")

    btn_frame = tk.Frame(tab)
    btn_frame.pack(fill="x", padx=12, pady=6)
    status = tk.StringVar(value="")
    tk.Label(btn_frame, textvariable=status, fg="gray").pack(side="left", padx=6)

    out = make_output(tab)

    def run():
        folder = Path(folder_var.get())
        if not folder.is_dir():
            messagebox.showerror("錯誤", "請先選擇有效的 Log 資料夾")
            return

        def task():
            status.set("分析中...")
            log_files = list(folder.glob("Hs*.log"))
            project = detect_project_from_raw_logs(folder) or "PJ1"
            result = build_temperature_section(folder, project=project)
            if not result:
                write_output(out, "未找到任何溫度資料。\n請確認資料夾內有 Hs*.log 檔案。")
            else:
                header = f"找到 {len(log_files)} 個 Hs*.log 檔案\n{'=' * 80}\n\n"
                write_output(out, header + result)
            status.set("完成")

        run_in_thread(task)

    tk.Button(btn_frame, text="執行分析", command=run, width=12).pack(side="left")


# ── Tab 4：PCIe Link ──────────────────────────────────────────────────────────

def build_pcie_link_tab(nb: ttk.Notebook) -> None:
    tab = ttk.Frame(nb)
    nb.add(tab, text="  PCIe Link  ")

    folder_var = make_folder_row(tab, "Log 資料夾")

    btn_frame = tk.Frame(tab)
    btn_frame.pack(fill="x", padx=12, pady=6)
    status = tk.StringVar(value="")
    tk.Label(btn_frame, textvariable=status, fg="gray").pack(side="left", padx=6)

    out = make_output(tab)

    def run(save=False):
        folder = Path(folder_var.get())
        if not folder.is_dir():
            messagebox.showerror("錯誤", "請先選擇有效的資料夾")
            return

        def task():
            status.set("掃描中...")
            # 判斷是單台(含 Hs*.log)還是批次(子資料夾含 Hs*.log)
            sub_folders = sorted(set(f.parent for f in folder.rglob("Hs*.log")))
            if not sub_folders:
                write_output(out, "未找到任何 Hs*.log 檔案。")
                status.set("")
                return

            lines = []
            save_dir = None
            if save:
                save_dir = Path(filedialog.askdirectory(title="選擇儲存資料夾"))
                if not save_dir or not save_dir.is_dir():
                    status.set("")
                    return

            for sf in sub_folders:
                sn = sf.name
                result = build_pcie_link_section(sf)
                data_lines = result.splitlines()[3:] if result else ["(no PCIe link data)"]
                lines.append(f"[{sn}]")
                lines.extend(f"  {l}" for l in data_lines)
                lines.append("")
                if save and save_dir:
                    (save_dir / f"{sn}.txt").write_text(
                        "\n".join(data_lines) + "\n", encoding="utf-8"
                    )

            write_output(out, "\n".join(lines))
            status.set(f"完成，共 {len(sub_folders)} 台" + (f"，已儲存至 {save_dir}" if save_dir else ""))

        run_in_thread(task)

    tk.Button(btn_frame, text="執行分析", command=lambda: run(False), width=12).pack(side="left")
    tk.Button(btn_frame, text="執行並儲存", command=lambda: run(True), width=14).pack(side="left", padx=6)


# ── Tab 5：Compress ───────────────────────────────────────────────────────────

def build_compress_tab(nb: ttk.Notebook) -> None:
    tab = ttk.Frame(nb)
    nb.add(tab, text="  Compress  ")

    file_var = make_file_row(tab, "Log 檔案")

    btn_frame = tk.Frame(tab)
    btn_frame.pack(fill="x", padx=12, pady=6)
    status = tk.StringVar(value="")
    tk.Label(btn_frame, textvariable=status, fg="gray").pack(side="left", padx=6)

    out = make_output(tab)

    def run():
        input_path = Path(file_var.get())
        if not input_path.is_file():
            messagebox.showerror("錯誤", "請先選擇有效的 Log 檔案")
            return

        def task():
            status.set("壓縮中...")
            with input_path.open("r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()

            original_text = "".join(lines)
            original_tokens = count_tokens(original_text)
            result = process_lines(lines)
            compressed_text = "\n".join(result)
            compressed_tokens = count_tokens(compressed_text)
            reduction = (1 - compressed_tokens / original_tokens) * 100 if original_tokens > 0 else 0

            output_path = input_path.with_name(input_path.stem + "_compressed.txt")
            with output_path.open("w", encoding="utf-8") as f:
                f.write(compressed_text)

            summary = (
                f"原始行數       : {len(lines)}\n"
                f"壓縮後行數     : {len(result)}\n"
                f"原始 token     : {original_tokens}\n"
                f"壓縮後 token   : {compressed_tokens}\n"
                f"Token 減少     : {reduction:.1f}%\n"
                f"輸出檔案       : {output_path}\n"
                f"{'=' * 80}\n\n"
                + compressed_text
            )
            write_output(out, summary)
            status.set("完成")

        run_in_thread(task)

    tk.Button(btn_frame, text="執行壓縮", command=run, width=12).pack(side="left")


# ── Tab 5：Full Output ────────────────────────────────────────────────────────

def build_full_output_tab(nb: ttk.Notebook) -> None:
    tab = ttk.Frame(nb)
    nb.add(tab, text="  Full Output  ")

    folder_var = make_folder_row(tab, "Log 資料夾")

    btn_frame = tk.Frame(tab)
    btn_frame.pack(fill="x", padx=12, pady=6)
    status = tk.StringVar(value="")
    tk.Label(btn_frame, textvariable=status, fg="gray").pack(side="left", padx=6)

    out = make_output(tab)
    _serial = [""]

    _result_dir = Path.home() / "Desktop" / "parsing_result"
    _result_dir.mkdir(parents=True, exist_ok=True)

    def run(auto_save=False):
        folder = Path(folder_var.get())
        if not folder.is_dir():
            messagebox.showerror("錯誤", "請先選擇有效的 Log 資料夾")
            return

        def task():
            status.set("偵測 project 中...")
            project = detect_project_from_raw_logs(folder)
            if not project:
                status.set("")
                messagebox.showerror("錯誤", "無法偵測 project 類型")
                return

            meta = extract_metadata_from_raw_logs(folder)
            _serial[0] = meta.get("serial") or ""

            status.set("解析 log 中...")
            parsed = parse(project, folder)

            status.set("分析 Event Flow 中...")
            counter, samples, total_lines, total_segments = build_path_map(folder, _load_ignore(folder), project=project)
            compressed_flow = build_compressed_flow(counter, samples, total_lines, total_segments, project=project)

            status.set("分析溫度中...")
            temp = build_temperature_section(folder, project=project)

            status.set("組合輸出...")
            sep = "=" * 80
            parts = [parsed]
            if temp:
                parts.append(f"{sep}\n{temp}")
            parts.append(compressed_flow)
            full_output = "\n\n".join(parts)

            token_count = count_tokens(full_output)
            header = (
                f"Project    : {project}\n"
                f"FW Version : {meta.get('fw_version') or '(未找到)'}\n"
                f"Serial     : {meta.get('serial') or '(未找到)'}\n"
                f"Token 數   : {token_count}\n"
                f"{'=' * 80}\n\n"
            )
            write_output(out, header + full_output)

            status.set("完成")

            if auto_save:
                fname = f"{_serial[0]}_parsing.txt" if _serial[0] else "full_output.txt"
                path = filedialog.asksaveasfilename(
                    defaultextension=".txt",
                    filetypes=[("Text Files", "*.txt"), ("All Files", "*.*")],
                    initialfile=fname,
                    initialdir=str(_result_dir),
                )
                if path:
                    Path(path).write_text(header + full_output, encoding="utf-8")
                    status.set(f"已儲存：{Path(path).name}")

        run_in_thread(task)

    tk.Button(btn_frame, text="執行", command=lambda: run(False), width=12).pack(side="left")
    tk.Button(btn_frame, text="執行並儲存", command=lambda: run(True), width=14).pack(side="left", padx=6)


# ── 主程式 ────────────────────────────────────────────────────────────────────

def main():
    root = tk.Tk()
    root.title("ParsingAI Tools")
    root.geometry("800x600")
    root.minsize(700, 500)

    nb = ttk.Notebook(root)
    nb.pack(fill="both", expand=True, padx=8, pady=8)

    build_parser_tab(nb)
    build_event_flow_tab(nb)
    build_temperature_tab(nb)
    build_pcie_link_tab(nb)
    build_compress_tab(nb)
    build_full_output_tab(nb)

    root.mainloop()


if __name__ == "__main__":
    main()
