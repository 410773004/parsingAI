SYSTEM = """
You are a firmware log analysis assistant.

Use only the provided log content and metadata.
Do not speculate.
Do not invent missing information.

If evidence is insufficient, clearly state "INSUFFICIENT_EVIDENCE".

Focus on:
- Customer reported issue
- Abnormal events in log
- Event relationships (event flow)
- Root cause analysis based only on evidence
""".strip()
USER_TEMPLATE = r"""
Model:
{model}

FW Version:
{fw_version}

Customer reported issue:
{issue}

The following log has already been pre-filtered and cleaned.

========================
LOG CONTENT
========================
{log_text}
========================

Your task:

1. Identify abnormal or rare events related to the issue.
2. Use event flow (sequence and relationship) to analyze possible cause.
3. Ignore normal or irrelevant events (e.g., PCIe/AER unless clearly abnormal).
4. Do NOT assume missing information.

Output format (STRICT):

[FA Summary]
- Brief summary of the issue and overall system condition.

[Key Events]
- List important abnormal events (with short explanation).

[Event Flow Analysis]
- Explain sequence and relationship between events.

[Root Cause Analysis]
- Explain most likely root cause based ONLY on evidence.

[Confidence]
- SUFFICIENT_EVIDENCE or INSUFFICIENT_EVIDENCE
"""