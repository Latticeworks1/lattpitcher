const logEl = document.getElementById('log');
const runBtn = document.getElementById('runBtn');

function log(...args) {
  logEl.textContent += args.join(' ') + '\n';
}

async function loadWasm() {
  const url = '/wasm/wasm_dsp.wasm';
  log('Fetching WASM from', url);
  const resp = await fetch(url);
  if (!resp.ok) {
    const txt = await resp.text();
    log('Failed to load WASM:', resp.status, resp.statusText, '\n', txt);
    throw new Error('WASM not available');
  }
  const bytes = await resp.arrayBuffer();
  const { instance } = await WebAssembly.instantiate(bytes, {});
  return instance;
}

runBtn.addEventListener('click', async () => {
  try {
    const inst = await loadWasm();
    // Expect an exported function: process_sample(x: f32) -> f32
    if (!inst.exports || typeof inst.exports.process_sample !== 'function') {
      log('Export process_sample not found');
      return;
    }
    for (let i = 0; i < 5; i++) {
      const x = Math.sin(i);
      const y = inst.exports.process_sample(x);
      log(`process_sample(${x.toFixed(4)}) = ${y.toFixed(4)}`);
    }
  } catch (e) {
    console.error(e);
    log('Error:', e.message || e);
  }
});

