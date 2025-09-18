// Simple Node smoke test for pitch detection and autotune processing
const PitchDetectionEngine = require('./pitch-detection.js');
const AutotuneEngine = require('./autotune-engine.js');

function genSine(freq, sampleRate, length) {
  const out = new Float32Array(length);
  const w = 2 * Math.PI * freq / sampleRate;
  for (let i = 0; i < length; i++) out[i] = Math.sin(w * i) * 0.5;
  return out;
}

(async () => {
  const sampleRate = 44100;
  const bufLen = 2048;
  const freq = 440; // A4
  const buffer = genSine(freq, sampleRate, bufLen);

  const pitch = new PitchDetectionEngine();
  pitch.setSampleRate(sampleRate);
  const result = pitch.detectPitch(buffer);

  const detected = result.frequency || 0;
  const err = Math.abs(detected - freq) / freq;
  console.log('Detected frequency:', detected.toFixed(2), 'Hz', 'error', (err * 100).toFixed(2), '%');
  if (!(detected > 0) || err > 0.1) {
    console.error('Pitch detection failed or too inaccurate');
    process.exit(1);
  }

  // Autotune processing in time-domain fallback
  const engine = new AutotuneEngine({ sampleRate });
  engine.setScaleType('chromatic');
  engine.setRootNote(9); // A
  engine.setCorrectionStrength(1.0);
  engine.setCorrectionSpeed(0.8);
  engine.setMixAmount(1.0);

  const processed = engine.processBlock(buffer, result);
  if (!processed || processed.length !== buffer.length || !isFinite(processed[0])) {
    console.error('Autotune processing produced invalid output');
    process.exit(1);
  }

  // Basic sanity: output energy not zero
  const energy = processed.reduce((s, v) => s + v * v, 0) / processed.length;
  console.log('Output RMS^2:', energy.toFixed(6));
  if (energy <= 0) {
    console.error('Autotune produced silent output');
    process.exit(1);
  }

  console.log('Smoke test passed');
})();

