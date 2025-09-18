const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

class SimplePitchDetector {
  constructor(sampleRate = 44100) {
    this.sampleRate = sampleRate;
  }

  setSampleRate(sr) { this.sampleRate = sr; }

  detect(buffer) {
    if (!buffer || buffer.length === 0) return { frequency: 0, confidence: 0, note: null };
    // Remove DC offset
    let mean = 0;
    for (let i = 0; i < buffer.length; i++) mean += buffer[i];
    mean /= buffer.length;
    const x = new Float32Array(buffer.length);
    for (let i = 0; i < buffer.length; i++) x[i] = buffer[i] - mean;

    // Autocorrelation
    const size = x.length;
    const minF = 80, maxF = 1200;
    const minLag = Math.floor(this.sampleRate / maxF);
    const maxLag = Math.min(size - 1, Math.floor(this.sampleRate / minF));
    const ac = new Float32Array(maxLag + 1);
    for (let lag = 0; lag <= maxLag; lag++) {
      let sum = 0;
      for (let i = 0; i < size - lag; i++) sum += x[i] * x[i + lag];
      ac[lag] = sum;
    }
    const norm = ac[0] || 1;
    for (let i = 0; i <= maxLag; i++) ac[i] /= norm;

    // Find peak in [minLag, maxLag]
    let bestLag = 0, best = 0;
    for (let lag = minLag; lag <= maxLag; lag++) {
      const v = ac[lag];
      if (v > best && v > ac[lag - 1] && v > (ac[lag + 1] || 0)) {
        best = v;
        bestLag = lag;
      }
    }
    if (bestLag === 0) return { frequency: 0, confidence: 0, note: null };
    const freq = this.sampleRate / bestLag;
    return { frequency: freq, confidence: best, note: this.frequencyToNote(freq) };
  }

  frequencyToNote(freq) {
    if (freq <= 0) return null;
    const A4 = 440;
    const n = Math.round(12 * Math.log2(freq / A4)) + 69; // MIDI
    const name = NOTE_NAMES[(n % 12 + 12) % 12];
    const octave = Math.floor(n / 12) - 1;
    const nearestFreq = A4 * Math.pow(2, (n - 69) / 12);
    const cents = Math.round(1200 * Math.log2(freq / nearestFreq));
    return { name, octave, cents };
  }
}

class App {
  constructor() {
    this.ctx = null;
    this.srcNode = null; // mic or oscillator
    this.proc = null;
    this.gain = null;
    this.detector = new SimplePitchDetector(44100);
    this.useMic = new URLSearchParams(location.search).get('mic') === '1';

    this.settings = {
      enabled: true,
      strength: 0.8,
      speed: 0.5,
      mix: 1.0,
      scale: 'major',
      root: 0,
      prevRatio: 1.0
    };

    this.$ = {
      start: document.getElementById('start'),
      stop: document.getElementById('stop'),
      status: document.getElementById('status'),
      level: document.getElementById('level'),
      note: document.getElementById('note'),
      freq: document.getElementById('freq'),
      cents: document.getElementById('cents'),
      auto: document.getElementById('auto'),
      strength: document.getElementById('strength'),
      speed: document.getElementById('speed'),
      mix: document.getElementById('mix'),
      scale: document.getElementById('scale'),
      root: document.getElementById('root')
    };
    this.bind();
  }

  bind() {
    this.$.start.addEventListener('click', () => this.start());
    this.$.stop.addEventListener('click', () => this.stop());
    this.$.auto.addEventListener('change', () => { this.settings.enabled = this.$.auto.checked; });
    this.$.strength.addEventListener('input', () => { this.settings.strength = parseFloat(this.$.strength.value); });
    this.$.speed.addEventListener('input', () => { this.settings.speed = parseFloat(this.$.speed.value); });
    this.$.mix.addEventListener('input', () => { this.settings.mix = parseFloat(this.$.mix.value); });
    this.$.scale.addEventListener('change', () => { this.settings.scale = this.$.scale.value; });
    this.$.root.addEventListener('change', () => { this.settings.root = parseInt(this.$.root.value, 10) || 0; });
  }

  async start() {
    try {
      if (!this.ctx) this.ctx = new (window.AudioContext || window.webkitAudioContext)();
      if (this.ctx.state === 'suspended') await this.ctx.resume();
      this.detector.setSampleRate(this.ctx.sampleRate);

      // Build graph
      this.gain = this.ctx.createGain();
      this.gain.gain.value = 0.8;

      if (this.useMic) {
        const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 1 } });
        this.srcNode = this.ctx.createMediaStreamSource(stream);
      } else {
        const osc = this.ctx.createOscillator();
        osc.type = 'sine';
        osc.frequency.value = 440;
        osc.start();
        this.srcNode = osc;
      }

      this.proc = this.ctx.createScriptProcessor(1024, 1, 1);
      this.proc.onaudioprocess = (e) => this.process(e);

      this.srcNode.connect(this.proc);
      this.proc.connect(this.gain);
      this.gain.connect(this.ctx.destination);

      this.setStatus(this.useMic ? 'Running (microphone)' : 'Running (oscillator A4 440Hz)');
    } catch (err) {
      this.setStatus('Error: ' + err.message, true);
    }
  }

  stop() {
    try {
      if (this.proc) { this.proc.disconnect(); this.proc.onaudioprocess = null; }
      if (this.srcNode) { try { this.srcNode.disconnect(); } catch (_) {} }
      if (this.gain) { try { this.gain.disconnect(); } catch (_) {} }
      this.proc = this.srcNode = this.gain = null;
      this.setStatus('Stopped');
    } catch (err) {
      this.setStatus('Stop error: ' + err.message, true);
    }
  }

  process(evt) {
    const ib = evt.inputBuffer;
    const ob = evt.outputBuffer;
    const input = ib.getChannelData(0);
    const output = ob.getChannelData(0);

    // Pitch detect
    const res = this.detector.detect(input);

    // Compute correction ratio
    let ratio = 1.0;
    if (this.settings.enabled && res && res.frequency > 0 && res.note && res.confidence > 0.2) {
      const targetFreq = this.computeTargetFrequency(res.frequency);
      if (targetFreq > 0) {
        const ideal = targetFreq / res.frequency;
        ratio = 1.0 + (ideal - 1.0) * this.settings.strength;
        // Smooth with simple one-pole controlled by speed (0=slow,1=fast)
        const a = Math.min(Math.max(this.settings.speed, 0), 1);
        ratio = a * ratio + (1 - a) * this.settings.prevRatio;
        this.settings.prevRatio = ratio;
      }
    } else {
      this.settings.prevRatio = 1.0;
    }

    // Time-domain pitch shift (naive resampling) + mix
    const wet = this.pitchShiftBlock(input, ratio);
    const mix = this.settings.mix;
    for (let i = 0; i < output.length; i++) {
      output[i] = input[i] * (1 - mix) + wet[i] * mix;
    }

    // Level
    let sum = 0; for (let i = 0; i < input.length; i++) sum += input[i] * input[i];
    const rms = Math.sqrt(sum / input.length);
    this.updateLevel(Math.min(rms * 100, 100));

    if (res && res.frequency > 0 && res.note && res.confidence > 0.2) {
      this.$.note.textContent = `${res.note.name}${res.note.octave}`;
      this.$.freq.textContent = res.frequency.toFixed(1);
      this.$.cents.textContent = (res.note.cents >= 0 ? '+' : '') + res.note.cents;
    } else {
      this.$.note.textContent = '--';
      this.$.freq.textContent = '0.0';
      this.$.cents.textContent = '0';
    }
  }

  pitchShiftBlock(input, ratio) {
    if (!isFinite(ratio) || ratio <= 0) ratio = 1.0;
    const n = input.length;
    const out = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      const srcIndex = i / ratio;
      const i0 = Math.floor(srcIndex);
      const i1 = i0 + 1;
      const t = srcIndex - i0;
      const s0 = (i0 >= 0 && i0 < n) ? input[i0] : 0;
      const s1 = (i1 >= 0 && i1 < n) ? input[i1] : 0;
      out[i] = s0 + (s1 - s0) * t;
    }
    return out;
  }

  computeTargetFrequency(freq) {
    // Quantize to selected scale around nearest octave
    const midi = Math.round(69 + 12 * Math.log2(freq / 440));
    const root = this.settings.root | 0;
    const patterns = {
      chromatic: [true,true,true,true,true,true,true,true,true,true,true,true],
      major:     [true,false,true,false,true,true,false,true,false,true,false,true],
      minor:     [true,false,true,true,false,true,false,true,true,false,true,false]
    };
    const scale = patterns[this.settings.scale] || patterns.major;
    const nearest = this.nearestScaleMidi(midi, root, scale);
    return 440 * Math.pow(2, (nearest - 69) / 12);
  }

  nearestScaleMidi(midi, root, mask) {
    const inOct = (midi - root) % 12;
    const wrapped = ((inOct % 12) + 12) % 12;
    if (mask[wrapped]) return midi; // already in scale
    let best = midi, bestDist = 128;
    for (let d = 1; d <= 12; d++) {
      // up
      let up = midi + d; const upIn = ((up - root) % 12 + 12) % 12; if (mask[upIn]) { best = up; bestDist = d; break; }
      // down
      let dn = midi - d; const dnIn = ((dn - root) % 12 + 12) % 12; if (mask[dnIn]) { best = dn; bestDist = d; break; }
    }
    return best;
  }

  updateLevel(pct) { this.$.level.style.width = pct + '%'; }
  setStatus(msg, isErr = false) { this.$.status.style.color = isErr ? '#f88' : '#9f9'; this.$.status.textContent = msg; }
}

window.addEventListener('DOMContentLoaded', () => new App());
