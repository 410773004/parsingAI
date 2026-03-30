import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext

from event_flow import analyze_event_flow


class EventFlowGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Event Flow Analyzer")
        self.root.geometry("1000x700")

        self.folder_var = tk.StringVar()

        frame = tk.Frame(root)
        frame.pack(fill="x", padx=10, pady=10)

        tk.Label(frame, text="Log Folder:").pack(side="left")
        tk.Entry(frame, textvariable=self.folder_var, width=80).pack(side="left", padx=5)
        tk.Button(frame, text="Browse", command=self.browse).pack(side="left")
        tk.Button(frame, text="Analyze", command=self.analyze).pack(side="left")

        self.output = scrolledtext.ScrolledText(root, font=("Consolas", 10))
        self.output.pack(fill="both", expand=True, padx=10, pady=10)

    def browse(self):
        folder = filedialog.askdirectory(title="選擇 log 資料夾")
        if folder:
            self.folder_var.set(folder)

    def analyze(self):
        folder = self.folder_var.get().strip()

        if not folder:
            messagebox.showwarning("Warning", "請先選擇資料夾")
            return

        try:
            result = analyze_event_flow(folder)

            # 顯示在 GUI
            self.output.delete("1.0", tk.END)
            self.output.insert(tk.END, result)

            # 同時 print 到 console（debug用）
            print(result)

        except Exception as e:
            messagebox.showerror("Error", str(e))


if __name__ == "__main__":
    root = tk.Tk()
    app = EventFlowGUI(root)
    root.mainloop()