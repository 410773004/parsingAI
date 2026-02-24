# main.py
from pathlib import Path
import ollama

from retriever_fw import generate_related_code_md

# ====== 你要改的設定 ======
MODEL = "qwen3:4b"
FW_ROOT = Path("src")                 # firmware repo root
PARSING_TXT = Path("TEST.txt")        # parsing output
RELATED_CODE_MD = Path("related_code.md")               # output from retriever

CONTEXT_LINES = 20
MAX_FILES = 20
MAX_SNIPS_PER_FILE = 8
TEMPERATURE = 0.0
# =========================

SYSTEM = """
Given the log text, do three tasks:

(1) INDICATORS: Extract lines that match failure-like evidence (error/warn/fail/reset/panic/assert/timeout/pcie/aer/smart/critical/spare/ecc/uncorr/corr). Keep the exact original line text.
(2) CLUSTERS: Group similar indicators by a normalized pattern (replace hex numbers, decimals, timestamps, IDs with <NUM>/<HEX>). Count occurrences.
(3) STATS: Provide totals.

Output JSON ONLY with this exact schema:
{
  "indicators":[
    {"type":"","evidence":"","count":1,"first_seen":"","last_seen":"","source":""}
  ],
  "clusters":[
    {"pattern":"","count":0,"samples":["",""],"sources":["",""]}
  ],
  "stats":{"total_indicators":0,"unique_patterns":0}
}

Constraints:
- evidence MUST be copied verbatim from the log.
- type must be one of: ["PCIE","SMART","NAND","FTL","POWER","RESET","PANIC","TIMEOUT","OTHER"]
- source should be the module/file/function if present in the line, otherwise empty string.
- first_seen/last_seen: use timestamp/line-id found in text; if none, use "".

<LOG>
{log_text}
</LOG>
"""

USER_TEMPLATE = """

Analyze the provided log excerpts.



<LOG>
{log_text}
</LOG>

"""

# Rules:
# - Use only information from the provided log.
# - No speculation.
# - If evidence is insufficient, set root_cause to "INSUFFICIENT_EVIDENCE".
# - Output JSON only.
# - No extra text.
#
# Required JSON format:
#
# {{
#   "key_indicators": [
#     {{
#       "indicator": "",
#       "evidence": "",
#       "count": 0,
#       "module": ""
#     }}
#   ],
#   "error_summary": {{
#     "unique_patterns": 0,
#     "most_frequent": "",
#     "near_reset_or_panic": ""
#   }},
#   "root_cause": "",
#   "confidence": 0
# }}

# <CODE>
# {code_text}
# </CODE>


def stream_chat(messages):
    resp = ollama.chat(
        model=MODEL,
        messages=messages,
        stream=True,
        options={"temperature": TEMPERATURE},
    )
    full = ""
    for chunk in resp:
        content = chunk["message"]["content"]
        print(content, end="", flush=True)
        full += content
    return full

def main():
    # 1) 先產 related_code.md（去重+合併）
    md_path = generate_related_code_md(
        fw_root=FW_ROOT,
        parsing_txt=PARSING_TXT,
        out_md=RELATED_CODE_MD,
        context_lines=CONTEXT_LINES,
        max_files=MAX_FILES,
        max_snips_per_file=MAX_SNIPS_PER_FILE,
    )
    print(f"\n✅ related code saved: {md_path.resolve()}\n")

    # 2) 讀 log + code（注意：code_text 太大會慢，可自行截斷）
    log_text = PARSING_TXT.read_text(encoding="utf-8", errors="ignore")
    code_text = md_path.read_text(encoding="utf-8", errors="ignore")

    messages = [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": USER_TEMPLATE.format(log_text=log_text, code_text=code_text)},
    ]

    while True:
        print("\nThinking...\n")
        reply = stream_chat(messages)
        messages.append({"role": "assistant", "content": reply})

        user_input = input("\n\nYou: ").strip()
        if user_input.lower() in ["exit", "quit"]:
            break

        messages.append({"role": "user", "content": user_input})

if __name__ == "__main__":
    main()