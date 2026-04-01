# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Running the server

```bash
cd C:/Users/SSSTC/PycharmProjects/ParsingAI
.venv/Scripts/activate
uvicorn server:app --reload
```

Requires Ollama running locally with the model specified in `config.py` (default: `qwen3:4b`).

## Running tests

Tests live in `tools/`:

```bash
python tools/test_parser.py
python tools/test_compress.py
python tools/test_event_flow.py
python tools/test_temperature.py
```

## Running the standalone log compressor GUI

```bash
python parsers/compress.py
```

## Architecture

**Entry point:** `server.py` — FastAPI app that accepts log file uploads via `POST /analyze`, delegates to `services/analyzer.py`, and tracks progress via a global stage callback exposed at `GET /progress`. Also hosts dataset CRUD endpoints (`/dataset/*`) backed by SQLite.

**Analysis pipeline** (`services/analyzer.py → analyze()`):
1. Detect project type (PJ1 or ER3) by scanning raw log content
2. Filter/extract events via `parsers/filter.py` using keyword configs in `search_strings/`
3. Simplify extracted text via `parsers/simplifier.py`
4. Analyze event flow sequences across boot segments (`event_flow.py`)
5. Build temperature anomaly section (`parsers/temperature.py`)
6. Count tokens with tiktoken, then send to local Ollama LLM using prompts from `prompts.py`

**Project types:** Two SSD firmware platforms are supported — `PJ1` (detected by `LJ1(SSSTC)` in logs) and `ER3` (detected by `SSSTC ER3-` or `FWInfo`). Each has its own search string JSON in `search_strings/`.

**Log filtering** (`parsers/filter.py`): Loads a project-specific JSON defining error keywords and context line counts. Extracts surrounding context blocks for each keyword match, clusters identical event signatures, and compresses repeated lines.

**Log compression** (`compress.py`): Standalone pipeline (`process_lines()`) that normalizes and deduplicates repetitive log patterns — adjacent identical lines, metric pairs, patrol loops, and EPM update blocks — replacing them with count annotations like `[Nx] pattern`.

**Database:** SQLite at `data/fa_dataset.db`. Schema in `db/database.py`, CRUD in `db/crud.py`. Stores analyzed cases keyed by serial number (upsert on serial).

**Config:** All tuneable parameters (LLM model, temperature, context window, token thresholds, log file patterns) are in `config.py`. The active Ollama model is `MODEL`.

**Accepted log files:** `Hs*.log`, `*norlog*.log`, `*lognor*.log`, and `smart_info.txt`. Files not matching these patterns are silently skipped by the upload handler.
