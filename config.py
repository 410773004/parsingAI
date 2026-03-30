#config.py
# 必要
MODEL = "qwen3:4b"
TEMPERATURE = 0.0

# log 檔案匹配
LOG_PATTERNS = ["Hs*.log"]  # 你要加 norlog.log 也可以：["Hs*.log", "norlog.log"]

# search_string.json 路徑（相對於 main.py）
SEARCH_JSON = "search_string.json"

# 輸出檔名
PICKED_LOG = "picked.log"   # Stage1 挑選後
MERGED_LOG = "merged.log"   # Stage2 清洗後（實際送模型）
OUTPUT_JSON = "result.json"

# token 計算（可選）
TIKTOKEN_ENCODING = "cl100k_base"

TEMP_LOW_THRESHOLD = 0
TEMP_HIGH_THRESHOLD = 85
TEMP_OUTLIER_LIMIT = 10   # 最多列幾筆異常樣本

FLOW_DETAIL_PRE_LINES = 200
EVENT_FLOW_TOP_N = 20
EVENTS_PER_LINE = 5