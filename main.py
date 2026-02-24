# main.py
# main.py
from __future__ import annotations

from pathlib import Path
import json
import tkinter as tk
from tkinter import filedialog, messagebox

import ollama

import config
import prompts


def choose_folder() -> Path | None:
    """用視窗選資料夾（不走 CLI）。"""
    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)

    folder = filedialog.askdirectory(title="選擇包含 Hs*.log 的資料夾")
    root.destroy()

    if not folder:
        return None
    return Path(folder)


def find_logs(folder: Path) -> list[Path]:
    """
    找出資料夾內所有符合：檔名開頭 Hs，結尾 .log 的檔案
    會遞迴子資料夾（如果你只想抓最上層，把 rglob 改 glob）
    """
    logs = sorted([p for p in folder.rglob("Hs*.log") if p.is_file()])
    return logs


def merge_logs(files: list[Path]) -> str:
    """把多個 log 合併成一個字串（加上檔名分隔，方便回溯）。"""
    parts: list[str] = []
    for f in files:
        try:
            text = f.read_text(encoding="utf-8", errors="ignore")
        except Exception as e:
            text = f"[READ_FAIL] {f} : {e}\n"
        parts.append(f"\n===== FILE: {f.name} =====\n")
        parts.append(text)
    return "".join(parts)


def filter_merged_log(log_text: str) -> str:
    """
    這裡先留空白 function。
    之後你想到「合併後要怎麼過濾」就只改這裡即可。

    例子（未啟用）：
    - 去掉重複行
    - 只保留某段時間範圍
    - 去掉 noise pattern
    """
    return log_text


def stream_chat(messages: list[dict]) -> str:
    resp = ollama.chat(
        model=config.MODEL,
        messages=messages,
        stream=True,
        options={"temperature": config.TEMPERATURE},
    )
    full = ""
    for chunk in resp:
        content = chunk.get("message", {}).get("content", "")
        if content:
            print(content, end="", flush=True)
            full += content
    return full


def try_parse_json(text: str) -> dict | None:
    """ollama 偶爾會多吐字；這邊嘗試抽出最外層 JSON。"""
    text = text.strip()

    # 直接嘗試整段
    try:
        return json.loads(text)
    except Exception:
        pass

    # 嘗試找第一個 { 到最後一個 } 之間
    l = text.find("{")
    r = text.rfind("}")
    if l != -1 and r != -1 and r > l:
        candidate = text[l : r + 1]
        try:
            return json.loads(candidate)
        except Exception:
            return None
    return None


def main():
    folder = choose_folder()
    if folder is None:
        messagebox.showinfo("取消", "未選擇資料夾，程式結束。")
        return

    logs = find_logs(folder)
    if not logs:
        messagebox.showerror("找不到檔案", f"在此資料夾找不到 Hs*.log：\n{folder}")
        return

    merged = merge_logs(logs)
    merged = filter_merged_log(merged)

    # 可選：把合併後的 log 存一份，方便你檢查 filter 有沒有做對
    merged_path = folder / config.MERGED_LOG
    merged_path.write_text(merged, encoding="utf-8", errors="ignore")

    user_prompt = prompts.USER_TEMPLATE.replace("{log_text}", merged)

    messages = [
        {"role": "system", "content": prompts.SYSTEM},
        {"role": "user", "content": user_prompt},
    ]

    print("\n--- Ollama parsing start ---\n")
    reply = stream_chat(messages)
    print("\n\n--- Ollama parsing end ---\n")

    data = try_parse_json(reply)
    out_path = folder / config.OUTPUT_JSON

    if data is None:
        # 如果不是乾淨 JSON，也先把原文存起來，避免你資料丟了
        out_path.write_text(reply, encoding="utf-8", errors="ignore")
        messagebox.showwarning(
            "輸出不是純 JSON",
            f"模型輸出可能夾雜文字，已原樣存檔：\n{out_path}\n\n"
            f"合併後 log 也已存：\n{merged_path}"
        )
    else:
        out_path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        messagebox.showinfo(
            "完成",
            f"已完成解析並輸出：\n{out_path}\n\n合併後 log：\n{merged_path}"
        )


if __name__ == "__main__":
    main()