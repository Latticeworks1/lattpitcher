/**
 * Advanced WebGPU FFT Pipeline for Real-time Audio Processing
 * Corrected and optimized implementation
 */

async function createFFTPipeline(device, size) {
    // Validate size is power of 2
    if ((size & (size - 1)) !== 0) {
        throw new Error('FFT size must be power of 2');
    }

    const numBits = Math.log2(size);

    const bitReverseShader = `
        // Uniform buffers must be 16-byte aligned and sized.
        struct Uniforms {
            size: u32,
            numBits: u32,
            _pad0: u32,
            _pad1: u32,
        };

        @group(0) @binding(0) var<storage, read> input: array<vec2<f32>>;
        @group(0) @binding(1) var<storage, read_write> output: array<vec2<f32>>;
        @group(0) @binding(2) var<uniform> uniforms: Uniforms;

        // Optimized bit-reversal with lookup table approach
        fn reverseBits(n: u32, numBits: u32) -> u32 {
            var reversed: u32 = 0u;
            var val: u32 = n;
            for (var i: u32 = 0u; i < numBits; i = i + 1u) {
                reversed = (reversed << 1u) | (val & 1u);
                val = val >> 1u;
            }
            return reversed;
        }

        @compute @workgroup_size(256)
        fn main(@builtin(global_invocation_id) global_id: vec3<u32>) {
            let i = global_id.x;
            if (i >= uniforms.size) {
                return;
            }
            
            let j = reverseBits(i, uniforms.numBits);
            output[j] = input[i];
        }
    `;

    const butterflyShader = `
        // Uniform buffers must be 16-byte aligned and sized.
        struct Uniforms {
            stage: u32,
            size: u32,
            inverse: u32,
            _pad0: u32,
        };

        @group(0) @binding(0) var<storage, read_write> data: array<vec2<f32>>;
        @group(0) @binding(1) var<uniform> uniforms: Uniforms;

        const PI: f32 = 3.141592653589793;

        // Complex multiplication: (a + bi) * (c + di) = (ac - bd) + (ad + bc)i
        fn complexMul(a: vec2<f32>, b: vec2<f32>) -> vec2<f32> {
            return vec2<f32>(
                a.x * b.x - a.y * b.y,
                a.x * b.y + a.y * b.x
            );
        }

        @compute @workgroup_size(256)
        fn main(@builtin(global_invocation_id) global_id: vec3<u32>) {
            let i = global_id.x;
            let size = uniforms.size;
            let stage = uniforms.stage;

            if (i >= size / 2u) {
                return;
            }

            let m = 1u << (stage + 1u);
            let m_half = m >> 1u;

            let k = i % m_half;
            let j = ((i / m_half) * m) + k;

            if (j + m_half >= size) {
                return;
            }

            // Twiddle factor calculation with proper inverse handling
            var angle = -2.0 * PI * f32(k) / f32(m);
            if (uniforms.inverse == 1u) {
                angle = -angle;
            }
            
            let twiddle = vec2<f32>(cos(angle), sin(angle));

            let a = data[j];
            let b = data[j + m_half];

            let t = complexMul(b, twiddle);

            data[j] = a + t;
            data[j + m_half] = a - t;
        }
    `;

    // Create shader modules
    const bitReverseModule = device.createShaderModule({ 
        code: bitReverseShader,
        label: 'BitReverse Shader'
    });
    
    const butterflyModule = device.createShaderModule({ 
        code: butterflyShader,
        label: 'Butterfly Shader'
    });

    // Create compute pipelines
    const bitReversePipeline = await device.createComputePipelineAsync({
        layout: 'auto',
        compute: {
            module: bitReverseModule,
            entryPoint: "main"
        },
        label: 'BitReverse Pipeline'
    });

    const butterflyPipeline = await device.createComputePipelineAsync({
        layout: 'auto',
        compute: {
            module: butterflyModule,
            entryPoint: "main"
        },
        label: 'Butterfly Pipeline'
    });

    return { bitReversePipeline, butterflyPipeline, size, numBits };
}

async function runFFT(device, pipelines, data, inverse = false) {
    const size = pipelines.size;
    const complexData = new Float32Array(size * 2);
    
    // Convert real data to complex
    for (let i = 0; i < Math.min(size, data.length); i++) {
        complexData[i * 2] = data[i];
        complexData[i * 2 + 1] = 0.0;
    }

    // Create buffers with proper error handling
    const inputBuffer = device.createBuffer({
        size: complexData.byteLength,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        label: 'FFT Input Buffer'
    });

    const workingBuffer = device.createBuffer({
        size: complexData.byteLength,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC,
        label: 'FFT Working Buffer'
    });

    const outputBuffer = device.createBuffer({
        size: complexData.byteLength,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
        label: 'FFT Output Buffer'
    });

    // Upload data
    device.queue.writeBuffer(inputBuffer, 0, complexData);

    const commandEncoder = device.createCommandEncoder({ label: 'FFT Command Encoder' });

    // Step 1: Bit reversal
    // 16-byte uniform buffer (4 x u32) to satisfy WebGPU alignment
    const bitReverseUniformData = new Uint32Array([size, pipelines.numBits, 0, 0]);
    const bitReverseUniformBuffer = device.createBuffer({
        size: bitReverseUniformData.byteLength,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        label: 'BitReverse Uniform Buffer'
    });
    device.queue.writeBuffer(bitReverseUniformBuffer, 0, bitReverseUniformData);

    const bitReverseBindGroup = device.createBindGroup({
        layout: pipelines.bitReversePipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: { buffer: inputBuffer } },
            { binding: 1, resource: { buffer: workingBuffer } },
            { binding: 2, resource: { buffer: bitReverseUniformBuffer } }
        ],
        label: 'BitReverse Bind Group'
    });

    let passEncoder = commandEncoder.beginComputePass({ label: 'BitReverse Pass' });
    passEncoder.setPipeline(pipelines.bitReversePipeline);
    passEncoder.setBindGroup(0, bitReverseBindGroup);
    passEncoder.dispatchWorkgroups(Math.ceil(size / 256));
    passEncoder.end();

    // Step 2: Butterfly operations
    const numStages = Math.log2(size);
    for (let stage = 0; stage < numStages; stage++) {
        // 16-byte uniform buffer (4 x u32) to satisfy WebGPU alignment
        const stageUniformData = new Uint32Array([stage, size, inverse ? 1 : 0, 0]);
        const stageUniformBuffer = device.createBuffer({
            size: stageUniformData.byteLength,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
            label: `Stage ${stage} Uniform Buffer`
        });
        device.queue.writeBuffer(stageUniformBuffer, 0, stageUniformData);

        const butterflyBindGroup = device.createBindGroup({
            layout: pipelines.butterflyPipeline.getBindGroupLayout(0),
            entries: [
                { binding: 0, resource: { buffer: workingBuffer } },
                { binding: 1, resource: { buffer: stageUniformBuffer } }
            ],
            label: `Butterfly Stage ${stage} Bind Group`
        });

        passEncoder = commandEncoder.beginComputePass({ label: `Butterfly Stage ${stage}` });
        passEncoder.setPipeline(pipelines.butterflyPipeline);
        passEncoder.setBindGroup(0, butterflyBindGroup);
        passEncoder.dispatchWorkgroups(Math.ceil(size / 512)); // Fewer workgroups for butterfly
        passEncoder.end();
        // Free per-stage uniform buffer to avoid accumulating GPU memory
        stageUniformBuffer.destroy();
    }

    // Copy result to output buffer
    commandEncoder.copyBufferToBuffer(workingBuffer, 0, outputBuffer, 0, complexData.byteLength);

    // Submit commands
    device.queue.submit([commandEncoder.finish()]);

    // Read results
    await outputBuffer.mapAsync(GPUMapMode.READ);
    const result = new Float32Array(outputBuffer.getMappedRange()).slice();
    outputBuffer.unmap();

    // Clean up temporary buffers
    inputBuffer.destroy();
    workingBuffer.destroy();
    outputBuffer.destroy();
    bitReverseUniformBuffer.destroy();

    // Return magnitude spectrum for forward FFT, complex data for inverse
    if (!inverse) {
        const magnitude = new Float32Array(size / 2);
        for (let i = 0; i < size / 2; i++) {
            const real = result[i * 2];
            const imag = result[i * 2 + 1];
            magnitude[i] = Math.sqrt(real * real + imag * imag);
        }
        return magnitude;
    } else {
        // For inverse FFT, normalize and return real part
        const normalized = new Float32Array(size);
        for (let i = 0; i < size; i++) {
            normalized[i] = result[i * 2] / size; // Normalize
        }
        return normalized;
    }
}

/**
 * Advanced Web Audio Pitch Detection Engine
 * Enhanced McLeod Pitch Method (MPM) with JUCE-style algorithms
 */
class PitchDetectionEngine {
    constructor() {
        this.noiseThreshold = 0.005;
        this.minFrequency = 65.0; // C2
        this.maxFrequency = 1000.0; // B5
        this.correlationThreshold = 0.3;
        this.sampleRate = 44100;
        
        // YIN algorithm parameters (from JUCE implementation)
        this.yinFrameSize = 4096; // larger frame improves mid/high frequency accuracy
        // YIN CMND threshold: lower is more periodic. Typical 0.1-0.2
        this.yinThreshold = 0.2;
        this.yinEpsilon = 1e-12;
        
        // Stability filtering
        this.recentDetections = [];
        this.stabilityWindow = 5;
        this.stabilityThreshold = 0.03;
        
        // Statistics
        this.detectionCount = 0;
        this.successfulDetections = 0;
        
        // Note mapping (JUCE-style)
        this.noteNames = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
        
        // Pre-computed windows
        this.hannWindow = null;
        this.bufferSize = 2048;
        
        // YIN buffers (fixed size for performance)
        this.yinDifferenceFunction = new Float32Array(this.yinFrameSize);
        this.yinCumulativeMean = new Float32Array(this.yinFrameSize);
        
        this.initializeWindows();
    }
    
    initializeWindows() {
        // Precompute Hann window (like JUCE implementation)
        this.hannWindow = new Float32Array(this.yinFrameSize);
        for (let n = 0; n < this.yinFrameSize; n++) {
            this.hannWindow[n] = 0.5 * (1.0 - Math.cos(2.0 * Math.PI * n / (this.yinFrameSize - 1)));
        }
    }
    
    setSampleRate(sampleRate) {
        this.sampleRate = sampleRate;
    }
    
    /**
     * Main pitch detection with multi-algorithm approach (like JUCE)
     */
    detectPitch(audioBuffer) {
        if (!audioBuffer || audioBuffer.length === 0) {
            return { frequency: 0, confidence: 0, note: null };
        }
        
        // Check for sufficient energy
        const rms = this.calculateRMS(audioBuffer);
        if (rms < this.noiseThreshold) {
            return { frequency: 0, confidence: 0, note: null };
        }
        
        this.detectionCount++;
        
        // Use YIN algorithm (primary) with fallback to autocorrelation
        let result = this.detectPitchYIN(audioBuffer);
        
        if (result.frequency <= 0 || result.confidence < 0.3) {
            result = this.detectPitchAutocorr(audioBuffer);
        }
        
        if (result.frequency > 0 && result.confidence > 0.3) {
            // Apply stability filtering
            result.frequency = this.applyStabilityFilter(result.frequency);
            result.note = this.frequencyToNote(result.frequency);
            this.successfulDetections++;
        }
        
        return result;
    }
    
    /**
     * YIN algorithm implementation (from JUCE AutotuneEngine)
     */
    detectPitchYIN(buffer) {
        const N = Math.min(buffer.length, this.yinFrameSize);
        
        // Remove DC and apply window function
        const windowedFrame = new Float32Array(N);
        let mean = 0;
        for (let n = 0; n < N; n++) mean += buffer[n];
        mean /= N;
        for (let n = 0; n < N; n++) {
            windowedFrame[n] = (buffer[n] - mean) * this.hannWindow[n];
        }
        
        // Calculate difference function d_k(τ)
        const maxTau = Math.floor(this.sampleRate / this.minFrequency);
        const effectiveMaxTau = Math.min(maxTau, Math.floor(N / 2));
        
        for (let tau = 0; tau < effectiveMaxTau; tau++) {
            let diff = 0;
            for (let n = 0; n < N - tau; n++) {
                const delta = windowedFrame[n] - windowedFrame[n + tau];
                diff += delta * delta;
            }
            this.yinDifferenceFunction[tau] = diff;
        }
        
        // Calculate cumulative mean normalized difference d'(τ) = d(τ) / (1/τ * Σ_{j=1..τ} d(j))
        this.yinCumulativeMean[0] = 1.0;
        let runningSum = 0.0;
        for (let tau = 1; tau < effectiveMaxTau; tau++) {
            runningSum += this.yinDifferenceFunction[tau];
            const meanDiff = runningSum / tau;
            this.yinCumulativeMean[tau] = (meanDiff > this.yinEpsilon)
                ? (this.yinDifferenceFunction[tau] / meanDiff)
                : 1.0;
        }
        
        // Find first minimum below threshold (YIN heuristic); fallback to global minimum
        const tauMin = Math.max(2, Math.floor(this.sampleRate / this.maxFrequency));
        let bestTau = tauMin;
        let minValue = this.yinCumulativeMean[tauMin];
        // Choose global minimum in the search range for stability
        for (let tau = tauMin + 1; tau < effectiveMaxTau; tau++) {
            const val = this.yinCumulativeMean[tau];
            if (val < minValue) {
                minValue = val;
                bestTau = tau;
            }
        }
        
        // Optional octave error reduction: prefer subharmonic if much better
        if (bestTau > 0) {
            let candidate = bestTau;
            let candidateVal = minValue;
            for (let it = 0; it < 2; it++) { // try up to 2 halvings
                const half = Math.round(candidate / 2);
                if (half >= tauMin && half > 1) {
                    const v = this.yinCumulativeMean[half];
                    if (v < candidateVal * 0.9) { // significant improvement
                        candidate = half;
                        candidateVal = v;
                    } else {
                        break;
                    }
                }
            }
            if (candidate !== bestTau) {
                bestTau = candidate;
                minValue = candidateVal;
            }
        }

        // Check if we found a valid minimum
        if (bestTau === 0 || minValue > this.yinThreshold) {
            return { frequency: 0, confidence: 0 };
        }
        
        // Parabolic interpolation for sub-sample accuracy on CMND curve
        let refinedTau = bestTau;
        if (bestTau > 0 && bestTau < effectiveMaxTau - 1) {
            const [y0, y1, y2] = [
                this.yinCumulativeMean[bestTau - 1],
                this.yinCumulativeMean[bestTau],
                this.yinCumulativeMean[bestTau + 1]
            ];

            const a = (y0 - 2 * y1 + y2) / 2;
            const b = (y2 - y0) / 2;

            if (Math.abs(a) > this.yinEpsilon) {
                const correction = -b / (2 * a);
                refinedTau = bestTau + Math.max(-0.5, Math.min(0.5, correction));
            }
        }
        
        const frequency = this.sampleRate / refinedTau;
        const confidence = Math.max(0, Math.min(1, 1.0 - minValue));
        
        return { frequency, confidence };
    }
    
    /**
     * Autocorrelation method - robust fallback
     */
    detectPitchAutocorr(buffer) {
        const N = Math.min(buffer.length, this.yinFrameSize);
        if (N < 64) return { frequency: 0, confidence: 0 };

        // DC removal and center clipping
        let mean = 0;
        for (let i = 0; i < N; i++) mean += buffer[i];
        mean /= N;
        const x = new Float32Array(N);
        let maxAbs = 0;
        for (let i = 0; i < N; i++) {
            const v = buffer[i] - mean;
            x[i] = v;
            const a = Math.abs(v);
            if (a > maxAbs) maxAbs = a;
        }
        const clip = 0.3 * maxAbs;
        for (let i = 0; i < N; i++) {
            const v = x[i];
            x[i] = v >= clip ? v - clip : (v <= -clip ? v + clip : 0);
        }
        for (let i = 0; i < N; i++) x[i] *= this.hannWindow[i];

        // Autocorrelation
        const corr = new Float32Array(N);
        for (let lag = 0; lag < N; lag++) {
            let s = 0;
            for (let i = 0; i < N - lag; i++) s += x[i] * x[i + lag];
            corr[lag] = s;
        }
        const norm = corr[0] || 1e-9;
        for (let i = 0; i < N; i++) corr[i] /= norm;

        const minP = Math.max(2, Math.floor(this.sampleRate / this.maxFrequency));
        const maxP = Math.min(Math.floor(this.sampleRate / this.minFrequency), Math.floor(N / 2));
        let bestP = 0;
        let bestC = 0;
        for (let p = minP + 1; p < maxP - 1; p++) {
            const c = corr[p];
            if (c > this.correlationThreshold && c > corr[p - 1] && c > corr[p + 1] && c > bestC) {
                bestC = c;
                bestP = p;
            }
        }
        if (bestP === 0) return { frequency: 0, confidence: 0 };
        // Parabolic interpolation
        const y0 = corr[bestP - 1], y1 = corr[bestP], y2 = corr[bestP + 1];
        const denom = 2 * (y0 - 2 * y1 + y2);
        let off = 0;
        if (Math.abs(denom) > 1e-9) off = (y0 - y2) / denom;
        const refined = bestP + Math.max(-0.5, Math.min(0.5, off));
        const frequency = this.sampleRate / refined;
        const confidence = Math.max(0, Math.min(1, bestC));
        return { frequency, confidence };
    }
    
    /**
     * Convert frequency to musical note (JUCE-style)
     */
    frequencyToNote(frequency) {
        if (frequency <= 0) return null;
        
        const A4 = 440.0;
        const C0 = A4 * Math.pow(2, -4.75);
        
        const halfStepsFromC0 = Math.round(12 * Math.log2(frequency / C0));
        const octave = Math.floor(halfStepsFromC0 / 12);
        const noteIndex = ((halfStepsFromC0 % 12) + 12) % 12;
        const noteName = this.noteNames[noteIndex];
        
        // Calculate cents deviation
        const exactHalfSteps = 12 * Math.log2(frequency / C0);
        const centsDeviation = Math.round((exactHalfSteps - halfStepsFromC0) * 100);
        
        return {
            name: noteName,
            octave: octave,
            midiNote: halfStepsFromC0 + 12,
            cents: centsDeviation,
            frequency: frequency,
            isValid: true
        };
    }
    
    /**
     * Enhanced stability filtering with exponential smoothing
     */
    applyStabilityFilter(newFrequency) {
        this.recentDetections.push(newFrequency);
        if (this.recentDetections.length > this.stabilityWindow) {
            this.recentDetections.shift();
        }
        
        if (this.recentDetections.length < 2) {
            return newFrequency;
        }
        
        // Use exponential smoothing for better stability
        const alpha = 0.7;
        const avgRecent = this.recentDetections.reduce((a, b) => a + b, 0) / this.recentDetections.length;
        const deviation = Math.abs(newFrequency - avgRecent) / avgRecent;
        
        if (deviation > this.stabilityThreshold) {
            return alpha * avgRecent + (1 - alpha) * newFrequency;
        }
        
        return newFrequency;
    }
    
    /**
     * Utility methods
     */
    calculateRMS(buffer) {
        let sum = 0;
        for (let i = 0; i < buffer.length; i++) {
            sum += buffer[i] * buffer[i];
        }
        return Math.sqrt(sum / buffer.length);
    }
    
    getSuccessRate() {
        return this.detectionCount > 0 ? this.successfulDetections / this.detectionCount : 0;
    }
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { PitchDetectionEngine, createFFTPipeline, runFFT };
} else if (typeof window !== 'undefined') {
    window.PitchDetectionEngine = PitchDetectionEngine;
    window.createFFTPipeline = createFFTPipeline;
    window.runFFT = runFFT;
}
