# Gemini Companion Service (Local)

This is a small FastAPI service that runs locally and talks to Google Gemini. Your VST3 plugin (JUCE) calls this service over HTTP, so the plugin never ships or handles the API key.

## Why
- Real‑time safety: Avoid network calls in the audio thread.
- Security: Keep `GOOGLE_API_KEY` off the plugin binary.
- Simplicity: Use Python SDKs for Gemini, `yt-dlp` for YouTube.

## Endpoints
- `POST /analyze-video` — Body: `{ "youtubeUrl": "https://youtu.be/..." }`
  - Downloads video, extracts audio, uploads to Gemini Files API, runs a prompt, and returns structured snippets `{ file, start, end }`.

## Quick Start
1. Python 3.10+
2. Create venv and install deps:
   - `python -m venv .venv && source .venv/bin/activate`
   - `pip install -r requirements.txt`
3. Set API key:
   - `export GOOGLE_API_KEY=your_key_here`
4. Run service:
   - `uvicorn main:app --reload --host 127.0.0.1 --port 8080`

## JUCE Plugin Usage (concept)
- From your editor, POST JSON to `http://127.0.0.1:8080/analyze-video` on a background thread.
- Parse JSON response and update UI via `MessageManager::callAsync`.

See `INTEGRATION_GEMINI.md` at repo root for JUCE-side example code.
