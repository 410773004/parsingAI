# prompts.py
SYSTEM = """
You are a log parser.
Extract only what is directly written in the log.
No explanation.
No speculation.
Output JSON only.
""".strip()

USER_TEMPLATE = r"""
From the log below:

1) Extract lines containing:
error, fail, failed, warn, warning, critical, panic, reset, timeout, pcie, aer, smart, ecc, uncorr, corr, spare

2) Group similar lines by normalized pattern
(replace numbers and hex values with <NUM>)

3) Count occurrences.

Return JSON in this exact structure:

{"indicators":[],"clusters":[],"stats":{"total":0,"unique":0}}

<LOG>
{log_text}
</LOG>
""".lstrip()