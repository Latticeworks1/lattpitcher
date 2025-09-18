// Pro Autotune AudioWorklet Processor
// Features: YIN-like pitch detection, voicing/energy gate, smoothing, probabilistic scale quantize,
//           vibrato-aware strength, continuous-time resampling shifter with crossfade mix.

class ProAutotuneProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.sampleRate_ = sampleRate;

    // Parameters (updated via port messages)
    this.params = {
      enabled: true,
      strength: 0.8,
      speed: 0.5,
      mix: 1.0,
      scale: 'major',
      root: 0,
      yinThreshold: 0.15,
      minF: 80,
      maxF: 1200
    };

    // Pitch state
    this.prevRatio = 1.0;
    this.prevFreq = 0;
    this.freqHistory = new Float32Array(7); // small median window
    this.freqWrite = 0;

    // Vibrato detection state
    this.centsHistory = new Float32Array(64);
    this.centsWrite = 0;

    // Resampling shifter state (continuous read pointer)
    this.ringSize = 8192;
    this.ring = new Float32Array(this.ringSize);
    this.wpos = 0;
    this.rpos = 0; // fractional read position

    this.frameCount = 0;

    this.port.onmessage = (e) => {
      const msg = e.data || {};
      if (msg.type === 'setParams') {
        Object.assign(this.params, msg.params || {});
        if (typeof msg.enabled === 'boolean') this.params.enabled = msg.enabled;
      }
    };
  }

  static get parameterDescriptors() { return []; }

  process(inputs, outputs) {
    const input = inputs[0];
    const output = outputs[0];
    if (!input || input.length === 0 || !output) return true;
    const inCh = input[0];
    const outCh = output[0];
    if (!inCh || !outCh) return true;

    // Write input into ring buffer
    for (let i = 0; i < inCh.length; i++) {
      this.ring[this.wpos] = inCh[i];
      this.wpos = (this.wpos + 1) & (this.ringSize - 1);
    }

    // Level (for UI)
    let sum = 0.0;
    for (let i = 0; i < inCh.length; i++) sum += inCh[i] * inCh[i];
    const rms = Math.sqrt(sum / inCh.length);

    // Pitch detection using YIN-like CMNDF on current block
    const freqEst = this.detectPitchYIN(inCh);

    // Target computation and ratio smoothing
    let ratio = 1.0;
    let correctionActive = false;
    if (this.params.enabled && freqEst.frequency > 0 && freqEst.confidence > 0.2) {
      const target = this.computeTargetFrequency(freqEst.frequency);
      if (target > 0) {
        const ideal = target / freqEst.frequency;
        let strength = this.params.strength;
        // Vibrato preservation: reduce strength for small periodic oscillations
        const cents = this.freqToCents(freqEst.frequency, target);
        this.centsHistory[this.centsWrite = (this.centsWrite + 1) & 63] = cents;
        const vib = this.estimateVibrato();
        if (vib.active) strength *= 0.6; // soften when vibrato present

        ratio = 1.0 + (ideal - 1.0) * strength;
        const a = Math.min(Math.max(this.params.speed, 0), 1);
        ratio = a * ratio + (1 - a) * this.prevRatio;
        this.prevRatio = ratio;
        correctionActive = Math.abs(ideal - 1.0) > 0.002;
      }
    } else {
      this.prevRatio = 1.0;
    }

    // Continuous resampling shifter
    const wet = this.readResampled(inCh.length, ratio);
    const mix = Math.min(Math.max(this.params.mix, 0), 1);
    for (let i = 0; i < outCh.length; i++) {
      outCh[i] = inCh[i] * (1 - mix) + wet[i] * mix;
    }

    // UI posts
    this.frameCount++;
    if (this.frameCount % 10 === 0) {
      this.port.postMessage({ type: 'level', rms });
      this.port.postMessage({ type: 'pitch', pitchResult: freqEst });
      this.port.postMessage({ type: 'correctionActive', value: correctionActive });
    }
    return true;
  }

  // YIN-like CMNDF detector (trimmed for performance)
  detectPitchYIN(x) {
    const sr = this.sampleRate_;
    const size = x.length;
    const minTau = Math.floor(sr / this.params.maxF);
    const maxTau = Math.min(size - 1, Math.floor(sr / this.params.minF));
    if (maxTau <= minTau + 2) return { frequency: 0, confidence: 0, note: null };

    // Difference function d(tau)
    const d = new Float32Array(maxTau + 1);
    for (let tau = 1; tau <= maxTau; tau++) {
      let sum = 0;
      for (let i = 0; i < size - tau; i++) {
        const diff = x[i] - x[i + tau];
        sum += diff * diff;
      }
      d[tau] = sum;
    }
    // Cumulative mean normalized difference cmndf(tau)
    const cmndf = new Float32Array(maxTau + 1);
    cmndf[0] = 1;
    let cumulative = 0;
    for (let tau = 1; tau <= maxTau; tau++) {
      cumulative += d[tau];
      cmndf[tau] = d[tau] * tau / (cumulative || 1);
    }

    // Absolute threshold
    let tauCandidate = -1;
    for (let tau = minTau; tau <= maxTau; tau++) {
      if (cmndf[tau] < this.params.yinThreshold) { tauCandidate = tau; break; }
    }
    if (tauCandidate === -1) return { frequency: 0, confidence: 0, note: null };

    // Parabolic interpolation around the minimum
    const tau = this.parabolicMin(cmndf, tauCandidate);
    const freq = sr / tau;
    const conf = 1 - cmndf[Math.round(tau)] || 0;

    // Stability smoothing (median of short history)
    this.freqHistory[this.freqWrite = (this.freqWrite + 1) % this.freqHistory.length] = freq;
    const smoothed = this.median(this.freqHistory);
    const note = this.frequencyToNote(smoothed);
    return { frequency: smoothed, confidence: conf, note };
  }

  parabolicMin(arr, idx) {
    const x0 = Math.max(1, idx - 1);
    const x1 = idx;
    const x2 = Math.min(arr.length - 2, idx + 1);
    const y0 = arr[x0], y1 = arr[x1], y2 = arr[x2];
    const a = (y0 + y2 - 2 * y1) / 2;
    const b = (y2 - y0) / 2;
    if (a === 0) return x1;
    const xv = x1 - b / (2 * a);
    return Math.max(1, Math.min(arr.length - 2, xv));
  }

  median(buf) {
    const tmp = Array.from(buf);
    tmp.sort((a, b) => a - b);
    const m = tmp.length >> 1;
    return tmp[m];
  }

  frequencyToNote(freq) {
    if (freq <= 0) return null;
    const A4 = 440;
    const n = Math.round(12 * Math.log2(freq / A4)) + 69; // MIDI
    const name = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][(n % 12 + 12) % 12];
    const octave = Math.floor(n / 12) - 1;
    const nearestFreq = A4 * Math.pow(2, (n - 69) / 12);
    const cents = Math.round(1200 * Math.log2(freq / nearestFreq));
    return { name, octave, cents };
  }

  freqToCents(freq, ref) {
    if (freq <= 0 || ref <= 0) return 0;
    return 1200 * Math.log2(freq / ref);
  }

  estimateVibrato() {
    // Simple variance over last 64 frames in cents space
    let sum = 0, sum2 = 0;
    for (let i = 0; i < this.centsHistory.length; i++) {
      const c = this.centsHistory[i] || 0;
      sum += c; sum2 += c * c;
    }
    const n = this.centsHistory.length;
    const mean = sum / n;
    const varc = sum2 / n - mean * mean;
    return { active: varc > 25 }; // heuristic
  }

  computeTargetFrequency(freq) {
    const midi = Math.round(69 + 12 * Math.log2(freq / 440));
    const root = (this.params.root | 0) % 12;
    const scales = {
      chromatic: [true,true,true,true,true,true,true,true,true,true,true,true],
      major:     [true,false,true,false,true,true,false,true,false,true,false,true],
      minor:     [true,false,true,true,false,true,false,true,true,false,true,false],
      dorian:    [true,false,true,true,false,true,false,true,true,false,true,false],
      pentatonic:[true,false,true,false,true,false,false,true,false,true,false,false],
      blues:     [true,false,false,true,false,true,true,true,false,false,true,false]
    };
    const mask = scales[this.params.scale] || scales.major;
    const nearest = this.nearestScaleMidi(midi, root, mask);
    // Probabilistic drift towards target when close
    const targetF = 440 * Math.pow(2, (nearest - 69) / 12);
    const cents = Math.abs(this.freqToCents(freq, targetF));
    if (cents < 20) {
      const alpha = 0.1; // gentle drift
      return freq + (targetF - freq) * alpha;
    }
    return targetF;
  }

  nearestScaleMidi(midi, root, mask) {
    const inOct = (midi - root) % 12; const wrapped = ((inOct % 12) + 12) % 12;
    if (mask[wrapped]) return midi;
    for (let d = 1; d <= 12; d++) {
      const up = midi + d; if (mask[((up - root) % 12 + 12) % 12]) return up;
      const dn = midi - d; if (mask[((dn - root) % 12 + 12) % 12]) return dn;
    }
    return midi;
  }

  readResampled(N, ratio) {
    if (!isFinite(ratio) || ratio <= 0) ratio = 1.0;
    const out = new Float32Array(N);
    for (let i = 0; i < N; i++) {
      const rp = this.rpos;
      const r0 = Math.floor(rp) & (this.ringSize - 1);
      const r1 = (r0 + 1) & (this.ringSize - 1);
      const t = rp - Math.floor(rp);
      const s0 = this.ring[r0];
      const s1 = this.ring[r1];
      out[i] = s0 + (s1 - s0) * t;
      // Advance read pointer at ratio to input rate
      this.rpos = (this.rpos + ratio) % this.ringSize;
    }
    return out;
  }
}

registerProcessor('pro-autotune-processor', ProAutotuneProcessor);

