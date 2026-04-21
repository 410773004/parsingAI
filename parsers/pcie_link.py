# parsers/pcie_link.py
import re
from pathlib import Path

_LINK_STATE_RE = re.compile(
    r"pcie_link_timer_handling\(\) - PCIe gen (\d) x (\d)"
)
_CE_CNT_RE = re.compile(r"CE Cnt: (\d+)")


def build_pcie_link_section(log_folder: str | Path) -> str:
    log_folder = Path(log_folder)
    link_states: list[tuple[str, str]] = []
    ce_counts: list[int] = []

    for log_file in log_folder.glob("Hs*.log"):
        with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                m = _LINK_STATE_RE.search(line)
                if m:
                    link_states.append((m.group(1), m.group(2)))
                    continue
                m = _CE_CNT_RE.search(line)
                if m:
                    ce_counts.append(int(m.group(1)))

    if not link_states:
        return ""

    # Deduplicate consecutive identical states for transition history
    seen: list[str] = []
    for g, w in link_states:
        state = f"Gen{g}x{w}"
        if not seen or seen[-1] != state:
            seen.append(state)

    final_gen, final_width = link_states[-1]
    final_link = f"Gen{final_gen} x{final_width}"
    ce_cnt = max(ce_counts) if ce_counts else 0

    return (
        "================================================================================\n"
        "PCIE LINK SUMMARY\n"
        "================================================================================\n"
        f"Link:    {' -> '.join(seen)}\n"
        f"Final:   {final_link}  CE Cnt: {ce_cnt}"
    )
