# Repository Guidelines

## Project Structure & Modules
- `src/`: Rust crate (`pitch_detector`) with real‑time pitch detection and inline unit tests.
- `PitchDetector/`: JUCE/CMake C++ GUI app scaffolding and sources.
- `JUCE/`: Vendored JUCE framework (do not modify unless updating vendor).
- `target/`: Rust build artifacts (ignored by Git).
- Supporting docs: `PITCH_DETECTION_PLAN.md`, `INTEGRATION_STRATEGY.md`, `CODE_PATTERN_EXTRACTION.md`.

## Build, Test, and Run
- Rust build: `cargo build` — compiles the crate (Rust 2021 edition).
- Rust tests: `cargo test` — runs unit tests (currently inline in `src/main.rs`).
- Rust run: `cargo run` — executes the CLI demo (prints detected pitch per chunk).
- C++/JUCE configure: `cmake -S PitchDetector -B PitchDetector/build -DCMAKE_BUILD_TYPE=Debug`.
- C++/JUCE build: `cmake --build PitchDetector/build`.

## Coding Style & Naming
- Rust: `cargo fmt` (rustfmt) and `cargo clippy -D warnings` before pushing.
  - Naming: `snake_case` for functions/vars, `PascalCase` for types, `SCREAMING_SNAKE_CASE` for consts.
- C++: follow `.clang-tidy` in `JUCE/`; prefer `clang-format` if configured.
  - Naming: `CamelCase` for classes/types, `lower_snake_case` for functions/vars.

## Testing Guidelines
- Frameworks: Rust built‑in test harness (`#[test]`). No formal C++ tests yet.
- Scope: Add unit tests close to logic; use tolerance checks for DSP (e.g., ±5%).
- Naming: `mod tests { ... }` for Rust inline; place future Rust integration tests under `tests/`.
- Run: `cargo test` locally; include failing case reproduction in PRs when fixing bugs.

## Commit & PR Guidelines
- Commits: Imperative, concise subject (≤72 chars). Scope prefix when helpful (e.g., `rust:`, `juce:`).
- Include: brief rationale, notable trade‑offs, and testing notes.
- PRs: clear description, linked issues, reproduction steps, and screenshots/GIFs for UI changes.
- CI/readiness: code formatted, lints clean, tests passing.

## Security & Config Tips
- No secrets in repo; do not commit local build artifacts.
- macOS: Microphone permission required for real‑time audio; document platform quirks in PRs.
