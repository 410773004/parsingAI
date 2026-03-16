from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox

from parsers.er_parser import parse
from parsers.pj_parser import parse


def choose_folder() -> Path | None:
    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)

    folder = filedialog.askdirectory(title="請選擇 log 資料夾")
    root.destroy()

    if not folder:
        return None

    return Path(folder)


def main():
    log_dir = choose_folder()
    if log_dir is None:
        print("未選擇資料夾，程式結束。")
        return

    if not log_dir.exists() or not log_dir.is_dir():
        print(f"無效資料夾: {log_dir}")
        return

    print(f"已選擇資料夾: {log_dir}")
    print()

    cleaned_log = parse(log_dir)

    print("========== CLEANED LOG ==========\n")
    print(cleaned_log)

    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)

    save = messagebox.askyesno("儲存結果", "要不要把 cleaned log 存成 txt？")

    if save:
        out_file = log_dir / "cleaned_log.txt"
        out_file.write_text(cleaned_log, encoding="utf-8")
        messagebox.showinfo("完成", f"已儲存:\n{out_file}")

    root.destroy()


if __name__ == "__main__":
    main()