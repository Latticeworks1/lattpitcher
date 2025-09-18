use std::fs;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::path::{Path, PathBuf};

const ADDR: &str = "127.0.0.1:8088";
const STATIC_DIR: &str = "rust/static";
const WASM_OUT: &str = "rust/wasm_dsp/target/wasm32-unknown-unknown/release/wasm_dsp.wasm";

fn main() -> std::io::Result<()> {
    println!("Rust minimal HTTP server listening on http://{ADDR}");
    let listener = TcpListener::bind(ADDR)?;
    for stream in listener.incoming() {
        match stream {
            Ok(mut stream) => {
                if let Err(e) = handle_client(&mut stream) {
                    eprintln!("Client error: {e}");
                }
            }
            Err(e) => eprintln!("Accept error: {e}"),
        }
    }
    Ok(())
}

fn handle_client(stream: &mut TcpStream) -> std::io::Result<()> {
    stream.set_read_timeout(Some(std::time::Duration::from_secs(2)))?;
    let mut buf = [0u8; 4096];
    let n = stream.read(&mut buf)?;
    if n == 0 { return Ok(()); }
    let req = String::from_utf8_lossy(&buf[..n]);
    let mut lines = req.lines();
    let first = lines.next().unwrap_or("");
    let mut parts = first.split_whitespace();
    let method = parts.next().unwrap_or("");
    let path = parts.next().unwrap_or("/");

    if method != "GET" && method != "HEAD" {
        return respond(stream, 405, "Method Not Allowed", b"Method Not Allowed", "text/plain", true);
    }

    match path {
        "/" => serve_static(stream, "index.html"),
        p if p.starts_with("/static/") => {
            let rel = &p[8..];
            serve_static(stream, rel)
        }
        "/web_audio_engine.js" => serve_repo_file(stream, Path::new("web_audio_engine.js")),
        "/wasm/wasm_dsp.wasm" => serve_wasm(stream),
        _ => {
            // Try static fallback
            let clean = path.trim_start_matches('/');
            if !clean.is_empty() {
                return serve_static(stream, clean);
            }
            respond(stream, 404, "Not Found", b"Not Found", "text/plain", true)
        }
    }
}

fn serve_static(stream: &mut TcpStream, rel: &str) -> std::io::Result<()> {
    let mut path = PathBuf::from(STATIC_DIR);
    let relpath = Path::new(rel);
    if relpath.components().any(|c| matches!(c, std::path::Component::ParentDir)) {
        return respond(stream, 400, "Bad Request", b"Invalid path", "text/plain", true);
    }
    path.push(relpath);
    if !path.exists() {
        return respond(stream, 404, "Not Found", b"Not Found", "text/plain", true);
    }
    let data = fs::read(&path)?;
    let ctype = content_type(&path);
    respond(stream, 200, "OK", &data, ctype, true)
}

fn serve_repo_file(stream: &mut TcpStream, rel: &Path) -> std::io::Result<()> {
    // Serves a specific file from the repository root. Use sparingly.
    if rel.components().any(|c| matches!(c, std::path::Component::ParentDir)) {
        return respond(stream, 400, "Bad Request", b"Invalid path", "text/plain", true);
    }
    if !rel.exists() {
        return respond(stream, 404, "Not Found", b"Not Found", "text/plain", true);
    }
    let data = fs::read(rel)?;
    let ctype = content_type(rel);
    respond(stream, 200, "OK", &data, ctype, true)
}

fn serve_wasm(stream: &mut TcpStream) -> std::io::Result<()> {
    match fs::read(WASM_OUT) {
        Ok(data) => respond(stream, 200, "OK", &data, "application/wasm", true),
        Err(_) => {
            let msg = b"WASM not built yet. Build with:\n  rustup target add wasm32-unknown-unknown\n  cargo build -p wasm_dsp --target wasm32-unknown-unknown --release\n";
            respond(stream, 404, "WASM Not Built", msg, "text/plain", true)
        }
    }
}

fn content_type(path: &Path) -> &'static str {
    match path.extension().and_then(|e| e.to_str()).unwrap_or("") {
        "html" => "text/html; charset=utf-8",
        "js" => "application/javascript",
        "css" => "text/css",
        "json" => "application/json",
        "wasm" => "application/wasm",
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "svg" => "image/svg+xml",
        _ => "application/octet-stream",
    }
}

fn respond(
    stream: &mut TcpStream,
    code: u16,
    status: &str,
    body: &[u8],
    content_type: &str,
    cors: bool,
) -> std::io::Result<()> {
    let mut headers = format!(
        "HTTP/1.1 {code} {status}\r\nContent-Type: {content_type}\r\nContent-Length: {}\r\nConnection: close\r\n",
        body.len()
    );
    if cors {
        headers.push_str("Access-Control-Allow-Origin: *\r\n");
    }
    headers.push_str("\r\n");
    stream.write_all(headers.as_bytes())?;
    if code != 204 && !matches!(content_type, "HEAD") {
        stream.write_all(body)?;
    }
    Ok(())
}
