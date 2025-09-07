# Gemini Integration (VST in FL Studio)

This outlines a DAW‑safe integration where the JUCE VST3 plugin calls a local HTTP service that talks to Google Gemini. Keep the API key in the service, not in the plugin.

## Service Contract
- Endpoint: `POST http://127.0.0.1:8080/analyze-video`
- Body: `{ "youtubeUrl": "https://youtu.be/..." }`
- Response:
  - `snippets`: array of `{ name: string, start_sec: number, end_sec: number }`
  - `markdown`: preformatted list (mm:ss.mmm) for copy/paste

## JUCE Editor Flow
- Add a URL input + Analyze button.
- On click, run HTTP POST on a background thread.
- On completion, update UI via `MessageManager::callAsync`.

### Minimal POST helper (run on background thread)
```cpp
juce::String postAnalyzeVideo(const juce::String& youtubeUrl) {
  juce::URL url("http://127.0.0.1:8080/analyze-video");
  juce::DynamicObject::Ptr obj(new juce::DynamicObject());
  obj->setProperty("youtubeUrl", youtubeUrl);
  juce::var root(obj.get());
  auto json = juce::JSON::toString(root);

  juce::StringPairArray headers; headers.set("Content-Type", "application/json");
  auto in = url.withPOSTData(json)
               .createInputStream(false, nullptr, nullptr, {}, 15000, &headers);
  return in ? in->readEntireStreamAsString() : juce::String();
}
```

### Parsing JSON hits
```cpp
struct Hit { juce::String name; double start=0, end=0; };
std::vector<Hit> parseHits(const juce::String& jsonText) {
  std::vector<Hit> out; auto v = juce::JSON::parse(jsonText);
  if (! v.isObject()) return out; auto* o = v.getDynamicObject();
  auto arr = o->getProperty("snippets"); if (!arr.isArray()) return out;
  for (auto& el : *arr.getArray()) {
    if (auto* it = el.getDynamicObject()) {
      Hit h; h.name = it->getProperty("name").toString();
      h.start = (double) it->getProperty("start_sec");
      h.end   = (double) it->getProperty("end_sec");
      out.push_back(h);
    }
  }
  return out;
}
```

### Time formatting
```cpp
static inline juce::String formatTimeMs(double sec) {
  if (sec < 0) sec = 0; int m = int(sec / 60.0); double r = sec - m * 60.0;
  return juce::String::formatted("%d:%06.3f", m, r);
}
```

## DAW Safety
- No network I/O in `processBlock` / `prepareToPlay`.
- UI updates on message thread only.
- Skip analysis when `isNonRealtime()` is true (export/bounce).

## Service Setup
- See `tools/gemini_service/README.md` to run the FastAPI service.
- Requires `yt-dlp`, `ffmpeg`, and `GOOGLE_API_KEY` in env.
