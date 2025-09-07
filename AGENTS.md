# Repository Guidelines

## Project Structure & Module Organization
- `src/`: Rust crate `pitch_detector` with real‑time pitch detection and inline unit tests.
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

## Coding Style & Naming Conventions
- Rust: run `cargo fmt` and `cargo clippy -D warnings` before pushing.
  - Naming: `snake_case` (functions/vars), `PascalCase` (types), `SCREAMING_SNAKE_CASE` (consts).
- C++: follow `.clang-tidy` in `JUCE/`; prefer `clang-format` if configured.
  - Naming: `CamelCase` (classes/types), `lower_snake_case` (functions/vars).

## Testing Guidelines
- Framework: Rust built‑in tests (`#[test]`) colocated near logic in `src/`.
- Tolerance: use DSP‑friendly checks (e.g., frequency within ±5%).
- Naming: group with `mod tests { ... }`; future integration tests under `tests/`.
- Run: `cargo test` locally; include failing repros when fixing bugs.

## Commit & Pull Request Guidelines
- Commits: imperative, concise subject ≤72 chars; scope prefix when helpful (e.g., `rust:`, `juce:`). Include rationale, trade‑offs, and testing notes.
- PRs: clear description, linked issues, repro steps; screenshots/GIFs for UI changes. Ensure code is formatted, lints are clean, and tests pass.

## Security & Configuration Tips
- No secrets or local build artifacts in Git.
- macOS: microphone permission required for real‑time audio; note platform quirks in PRs.
- Treat `JUCE/` as vendor code; change only when updating the vendor.
