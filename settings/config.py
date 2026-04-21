# ===== 模型設定 =====
MODEL = "qwen3:4b"
TEMPERATURE = 0

# ===== LLM 參數 =====
TOP_P = 0.95
TOP_K = 20
REPEAT_PENALTY = 1

NUM_CTX = 100000  # 可調（最大262144）

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
FLOW_DETAIL_PRE_LINES = 150     # primary: lines before first event
FLOW_DETAIL_POST_LINES = 10     # primary: lines after last event
FLOW_TOTAL_TOKEN_THRESHOLD = 15000  # fallback trigger on total output
FLOW_DETAIL_PRE_CONTEXT = 80    # fallback lv1: lines before each event
FLOW_DETAIL_POST_CONTEXT = 20   # fallback lv1: lines after each event
FLOW_LV2_TOP_N_STEPS = [5, 3, 1]  # fallback lv2: try reducing top_n
EVENT_FLOW_TOP_N = 20
EVENTS_PER_LINE = 5