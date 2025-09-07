import os
import tempfile
import subprocess
from typing import List, Optional, Any, Dict

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, HttpUrl

try:
    # Newer Gemini SDK (google-genai)
    from google import genai
    from google.genai import types as genai_types
    HAS_GENAI = True
except Exception:
    HAS_GENAI = False

app = FastAPI(title="Local Gemini Companion", version="0.1.0")


class AnalyzeVideoRequest(BaseModel):
    youtubeUrl: HttpUrl
    prompt: Optional[str] = (
        "Please return a JSON array of objects with fields: file, start, end. "
        "Compute start/end times (seconds) for each distinct audio snippet. Use 3 decimal places."
    )


class Snippet(BaseModel):
    name: str
    start_sec: float
    end_sec: float
    file: Optional[str] = None


class AnalyzeVideoResponse(BaseModel):
    snippets: List[Snippet]
    markdown: str
    duration: Optional[float] = None


def _require_env(key: str) -> str:
    val = os.getenv(key)
    if not val:
        raise RuntimeError(f"Missing required environment variable: {key}")
    return val


def _download_audio(youtube_url: str, workdir: str) -> str:
    # Use yt-dlp to get best audio and write to .m4a (then transcode)
    m4a_path = os.path.join(workdir, "audio.m4a")
    cmd = [
        "yt-dlp",
        "-f",
        "bestaudio[ext=m4a]/bestaudio",
        "-o",
        m4a_path,
        youtube_url,
    ]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0 or not os.path.exists(m4a_path):
        raise RuntimeError(f"yt-dlp failed: {r.stderr.decode('utf-8', 'ignore')}")
    # Transcode to WAV 16-bit/48k
    wav_path = os.path.join(workdir, "audio.wav")
    cmd2 = [
        "ffmpeg",
        "-y",
        "-i",
        m4a_path,
        "-ac",
        "1",
        "-ar",
        "48000",
        "-sample_fmt",
        "s16",
        wav_path,
    ]
    r2 = subprocess.run(cmd2, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r2.returncode != 0 or not os.path.exists(wav_path):
        raise RuntimeError(f"ffmpeg failed: {r2.stderr.decode('utf-8', 'ignore')}")
    return wav_path


def _call_gemini(audio_path: str, prompt: str) -> str:
    if not HAS_GENAI:
        raise RuntimeError("google-genai not installed. See requirements.txt")

    api_key = _require_env("GOOGLE_API_KEY")
    client = genai.Client(api_key=api_key)

    # Upload audio file to Gemini Files API
    uploaded = client.files.upload(file=audio_path, config={"display_name": os.path.basename(audio_path)})

    # Build content with file reference and text prompt
    contents = genai_types.Content(
        parts=[
            genai_types.Part(file_data=genai_types.FileData(file_uri=uploaded.uri)),
            genai_types.Part(text=prompt),
        ]
    )

    resp = client.models.generate_content(
        model="models/gemini-2.0-flash-exp",
        contents=contents,
        config=genai_types.GenerateContentConfig(
            response_mime_type="application/json",
        ),
    )
    # The SDK returns text; we expect JSON per config above
    return resp.text


def _parse_time_to_seconds(val: Any) -> Optional[float]:
    """Accepts float seconds or strings like 'm:ss.mmm' and returns seconds."""
    if isinstance(val, (int, float)):
        return float(val)
    if isinstance(val, str):
        s = val.strip()
        # try m:ss.mmm
        if ":" in s:
            try:
                mins, rest = s.split(":", 1)
                return float(mins) * 60.0 + float(rest)
            except Exception:
                pass
        # plain float string
        try:
            return float(s)
        except Exception:
            return None
    return None


def _format_mm_ss_mmm(seconds: float) -> str:
    if seconds < 0:
        seconds = 0.0
    mins = int(seconds // 60)
    rem = seconds - mins * 60
    return f"{mins}:{rem:06.3f}"


@app.post("/analyze-video", response_model=AnalyzeVideoResponse)
def analyze_video(req: AnalyzeVideoRequest):
    try:
        with tempfile.TemporaryDirectory(prefix="gemini_yt_") as tmp:
            wav_path = _download_audio(req.youtubeUrl, tmp)
            raw_json = _call_gemini(wav_path, req.prompt or "")

            # Use orjson if available for speed
            try:
                import orjson as json
                data = json.loads(raw_json)
            except Exception:
                import json
                data = json.loads(raw_json)

            # Normalize items to Snippet(name, start_sec, end_sec)
            norm: List[Snippet] = []
            if isinstance(data, list):
                items = data
            elif isinstance(data, dict) and isinstance(data.get("snippets"), list):
                items = data.get("snippets")
            else:
                items = []

            for it in items:
                if not isinstance(it, dict):
                    continue
                name = it.get("name") or it.get("file") or "snippet"
                start_sec = _parse_time_to_seconds(it.get("start_sec") or it.get("start"))
                end_sec = _parse_time_to_seconds(it.get("end_sec") or it.get("end"))
                file_name = it.get("file")
                if start_sec is None or end_sec is None:
                    continue
                norm.append(Snippet(name=name, start_sec=start_sec, end_sec=end_sec, file=file_name))

            # Build markdown block
            lines: List[str] = ["Here is a list of the audio events with their start and end times:", ""]
            for idx, s in enumerate(norm, start=1):
                nm = s.name or f"snippet_{idx}"
                lines.append(f"- **{nm}**:")
                lines.append(f"  - Start: {_format_mm_ss_mmm(s.start_sec)}")
                lines.append(f"  - End: {_format_mm_ss_mmm(s.end_sec)}")
            markdown = "\n".join(lines)

            return AnalyzeVideoResponse(snippets=norm, markdown=markdown)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/healthz")
def healthz():
    return {"ok": True}
