# ===== 模型設定 =====
MODEL = "qwen3:4b"
TEMPERATURE = 0

# ===== LLM 參數 =====
TOP_P = 0.95
TOP_K = 20
REPEAT_PENALTY = 1

NUM_CTX = 1000  # 可調（最大262144）

# ===== log 設定 =====
LOG_PATTERNS = ["Hs*.log"]

SEARCH_JSON = "search_string.json"

PICKED_LOG = "picked.log"
MERGED_LOG = "merged.log"
OUTPUT_JSON = "result.json"

TIKTOKEN_ENCODING = "cl100k_base"

# ===== 溫度 =====
TEMP_LOW_THRESHOLD = 0
TEMP_HIGH_THRESHOLD = 85
TEMP_OUTLIER_LIMIT = 10

# ===== event flow =====
FLOW_DETAIL_PRE_LINES = 200
EVENT_FLOW_TOP_N = 20
EVENTS_PER_LINE = 5