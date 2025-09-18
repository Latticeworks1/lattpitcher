# Rust WASM Web Server + DSP Module

This workspace contains:

- `server`: Minimal HTTP server in Rust (no dependencies) serving static files and a WASM binary
- `wasm_dsp`: Minimal Rust `cdylib` compiled to WebAssembly exporting DSP functions

## Build WASM

Requires Rust target `wasm32-unknown-unknown`.

```
rustup target add wasm32-unknown-unknown
cargo build -p wasm_dsp --target wasm32-unknown-unknown --release
```

The output will be at:

```
rust/wasm_dsp/target/wasm32-unknown-unknown/release/wasm_dsp.wasm
```

## Run Server

From repo root:

```
cargo run -p server
```

Server listens on `http://127.0.0.1:8088` and serves:

- `/` -> `rust/static/index.html`
- `/static/*` -> files under `rust/static`
- `/wasm/wasm_dsp.wasm` -> compiled WASM (ensure you built it first)

Open in your browser:

```
http://127.0.0.1:8088/
```

Click “Run DSP” to load the WASM and call `process_sample`.

## Notes

- The server sets `Access-Control-Allow-Origin: *` to simplify local testing.
- The WASM exports are plain C ABI (no wasm-bindgen) so you can call from JS via `instance.exports.*`.
- You can evolve `wasm_dsp` to include vectorized processing and more advanced DSP while keeping the same export API.

