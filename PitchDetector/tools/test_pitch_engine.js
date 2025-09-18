// Simple Node test for PitchDetectionEngine on a synthetic sine wave
const { PitchDetectionEngine } = require('../web_audio_engine.js');

function genSine(freq, sampleRate, length) {
  const buf = new Float32Array(length);
  const dt = 1 / sampleRate;
  let phase = 0;
  const twoPi = 2 * Math.PI;
  for (let i = 0; i < length; i++) {
    buf[i] = Math.sin(phase);
    phase += twoPi * freq * dt;
    if (phase > twoPi) phase -= twoPi;
  }
  return buf;
}

async function main() {
  const engine = new PitchDetectionEngine();
  const sampleRate = 44100;
  engine.setSampleRate(sampleRate);

  const freqs = [220, 440, 523.25, 659.25];
  const N = 2048;
  for (const f of freqs) {
    const buf = genSine(f, sampleRate, N);
    const res = engine.detectPitch(buf);
    const err = Math.abs(res.frequency - f);
    console.log(`Input ${f.toFixed(2)} Hz -> Detected ${res.frequency.toFixed(2)} Hz, err ${err.toFixed(2)} Hz, conf ${res.confidence?.toFixed(2)}`);
  }
}

main().catch(e => {
  console.error('Test failed:', e);
  process.exit(1);
});

