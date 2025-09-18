# Repository Guidelines

## Project Structure & Module Organization
- `src/`: Rust crate `pitch_detector` (real‑time pitch DSP) with inline unit tests.
- `PitchDetector/`: JUCE/CMake C++ GUI app scaffolding and sources.
- `JUCE/`: Vendored JUCE framework (do not modify unless updating vendor).
- `target/`: Rust build artifacts (ignored by Git).
- Docs: `PITCH_DETECTION_PLAN.md`, `INTEGRATION_STRATEGY.md`, `CODE_PATTERN_EXTRACTION.md`.

## Build, Test, and Development Commands
- Rust build: `cargo build` — compiles the crate (Rust 2021).
- Rust tests: `cargo test` — runs inline unit tests in `src/`.
- Rust run: `cargo run` — CLI demo; prints detected pitch per chunk.
- C++/JUCE configure: `cmake -S PitchDetector -B PitchDetector/build -DCMAKE_BUILD_TYPE=Debug`.
- C++/JUCE build: `cmake --build PitchDetector/build`.
- Lint/format: `cargo fmt && cargo clippy -D warnings` before pushing.

## Coding Style & Naming Conventions
- Rust: `snake_case` (functions/vars), `PascalCase` (types), `SCREAMING_SNAKE_CASE` (consts). Use `rustfmt` defaults.
- C++: follow `JUCE/.clang-tidy`; prefer `clang-format` if configured. `CamelCase` (classes/types), `lower_snake_case` (functions/vars).

## Testing Guidelines
- Framework: Rust built‑in `#[test]` colocated near logic in `src/`.
- Tolerance: Use DSP‑friendly checks (e.g., frequency within ±5%).
- Naming: Group with `mod tests { ... }`; future integration tests live under `tests/`.
- Run: `cargo test`; when fixing bugs, include a failing repro.

## Commit & Pull Request Guidelines
- Commits: imperative, concise subject ≤72 chars; add scope when useful (e.g., `rust:`, `juce:`). Include rationale, trade‑offs, and testing notes.
- PRs: clear description, linked issues, repro steps; screenshots/GIFs for UI changes. Ensure code is formatted, lints are clean, and tests pass.

## Security & Configuration Tips
- No secrets or local build artifacts in Git.
- macOS: microphone permission required for real‑time audio; note platform quirks in PRs.
- Treat `JUCE/` as vendor code; change only when updating the vendor.
