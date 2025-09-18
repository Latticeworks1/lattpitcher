/**
 * Real-time Audio Processing Worklet for FL Studio Vocal Collaboration
 * Handles low-latency audio capture, processing, and encoding
 */

// Register the audio worklet processor
if (typeof AudioWorkletProcessor !== 'undefined') {
    class FLStudioAudioProcessor extends AudioWorkletProcessor {
        constructor(options) {
            super();
            
            // Initialize processing parameters
            this.bufferSize = 512; // Optimized for FL Studio
            this.sampleRate = 48000;
            this.channels = 1;
            
            // Audio buffers
            this.inputBuffer = new Float32Array(this.bufferSize);
            this.outputBuffer = new Float32Array(this.bufferSize);
            this.bufferIndex = 0;
            
            // Audio processing parameters
            this.inputGain = 1.0;
            this.noiseGateThreshold = -40; // dB
            this.lowCutFreq = 80; // Hz
            this.isGateOpen = false;
            
            // Audio analysis
            this.rmsWindow = new Float32Array(256);
            this.rmsIndex = 0;
            this.currentRMS = 0;
            this.peakLevel = 0;
            
            // Low-cut filter (high-pass) - simple IIR
            this.filterHistory = { x1: 0, x2: 0, y1: 0, y2: 0 };
            this.updateLowCutCoefficients();
            
            // Setup message handling
            this.port.onmessage = (event) => {
                this.handleMessage(event.data);
            };
            
            // Send initial status
            this.port.postMessage({
                type: 'initialized',
                bufferSize: this.bufferSize,
                sampleRate: this.sampleRate
            });
        }
        
        handleMessage(data) {
            switch (data.type) {
                case 'setInputGain':
                    this.inputGain = Math.max(0, Math.min(2.0, data.value));
                    break;
                    
                case 'setNoiseGate':
                    this.noiseGateThreshold = Math.max(-60, Math.min(-20, data.value));
                    break;
                    
                case 'setLowCut':
                    this.lowCutFreq = Math.max(20, Math.min(200, data.value));
                    this.updateLowCutCoefficients();
                    break;
                    
                case 'getAudioLevel':
                    this.port.postMessage({
                        type: 'audioLevel',
                        rms: this.currentRMS,
                        peak: this.peakLevel,
                        gateOpen: this.isGateOpen
                    });
                    break;
            }
        }
        
        updateLowCutCoefficients() {
            // Simple high-pass filter coefficients
            const omega = 2 * Math.PI * this.lowCutFreq / this.sampleRate;
            const sin = Math.sin(omega);
            const cos = Math.cos(omega);
            const alpha = sin / (2 * 0.707); // Q = 0.707 for Butterworth
            
            const b0 = (1 + cos) / 2;
            const b1 = -(1 + cos);
            const b2 = (1 + cos) / 2;
            const a0 = 1 + alpha;
            const a1 = -2 * cos;
            const a2 = 1 - alpha;
            
            // Normalize coefficients
            this.filterCoeffs = {
                b0: b0 / a0,
                b1: b1 / a0,
                b2: b2 / a0,
                a1: a1 / a0,
                a2: a2 / a0
            };
        }
        
        applyLowCutFilter(sample) {
            const { b0, b1, b2, a1, a2 } = this.filterCoeffs;
            const { x1, x2, y1, y2 } = this.filterHistory;
            
            // Direct Form II implementation
            const output = b0 * sample + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            
            // Update history
            this.filterHistory.x2 = x1;
            this.filterHistory.x1 = sample;
            this.filterHistory.y2 = y1;
            this.filterHistory.y1 = output;
            
            return output;
        }
        
        calculateRMS(samples) {
            let sum = 0;
            for (let i = 0; i < samples.length; i++) {
                sum += samples[i] * samples[i];
            }
            return Math.sqrt(sum / samples.length);
        }
        
        dBFromLinear(linear) {
            return linear > 0 ? 20 * Math.log10(linear) : -120;
        }
        
        processNoiseGate(rmsLevel) {
            const rmsDB = this.dBFromLinear(rmsLevel);
            
            // Simple noise gate with hysteresis
            if (!this.isGateOpen && rmsDB > this.noiseGateThreshold) {
                this.isGateOpen = true;
            } else if (this.isGateOpen && rmsDB < (this.noiseGateThreshold - 6)) {
                this.isGateOpen = false;
            }
            
            return this.isGateOpen;
        }
        
        process(inputs, outputs, parameters) {
            const input = inputs[0];
            if (!input || !input[0]) {
                return true;
            }
            
            const inputData = input[0];
            const frameLength = inputData.length;
            
            // Process each sample
            for (let i = 0; i < frameLength; i++) {
                // Apply input gain
                let sample = inputData[i] * this.inputGain;
                
                // Apply low-cut filter
                sample = this.applyLowCutFilter(sample);
                
                // Store in buffer
                this.inputBuffer[this.bufferIndex] = sample;
                
                // Update RMS calculation
                this.rmsWindow[this.rmsIndex] = Math.abs(sample);
                this.rmsIndex = (this.rmsIndex + 1) % this.rmsWindow.length;
                
                // Update peak level
                this.peakLevel = Math.max(this.peakLevel * 0.999, Math.abs(sample));
                
                this.bufferIndex++;
                
                // When buffer is full, send to main thread
                if (this.bufferIndex >= this.bufferSize) {
                    // Calculate audio level
                    this.currentRMS = this.calculateRMS(this.inputBuffer);
                    
                    // Apply noise gate
                    const gateOpen = this.processNoiseGate(this.currentRMS);
                    
                    // Create audio packet
                    const audioData = gateOpen ? 
                        Array.from(this.inputBuffer) : 
                        new Array(this.bufferSize).fill(0);
                    
                    // Send processed audio to main thread
                    this.port.postMessage({
                        type: 'audioData',
                        data: audioData,
                        timestamp: currentTime,
                        rmsLevel: this.currentRMS,
                        peakLevel: this.peakLevel,
                        gateOpen: gateOpen
                    });
                    
                    // Reset buffer
                    this.bufferIndex = 0;
                }
            }
            
            return true;
        }
    }
    
    registerProcessor('fl-studio-audio-processor', FLStudioAudioProcessor);
}

/**
 * Audio Level Meter Visualization
 */
class AudioMeter {
    constructor(canvasId) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas.getContext('2d');
        this.rmsLevel = 0;
        this.peakLevel = 0;
        this.peakHold = 0;
        this.peakHoldTime = 0;
        
        // Meter colors
        this.colors = {
            background: '#1a1a1a',
            rms: '#27ae60',
            peak: '#f39c12',
            clip: '#e74c3c',
            scale: '#7f8c8d'
        };
        
        this.render();
    }
    
    update(rms, peak) {
        this.rmsLevel = Math.max(0, Math.min(1, rms));
        this.peakLevel = Math.max(0, Math.min(1, peak));
        
        // Peak hold logic
        if (peak > this.peakHold) {
            this.peakHold = peak;
            this.peakHoldTime = Date.now();
        } else if (Date.now() - this.peakHoldTime > 1000) {
            this.peakHold *= 0.95; // Slow decay
        }
        
        this.render();
    }
    
    render() {
        if (!this.canvas) return;
        
        const { width, height } = this.canvas;
        const ctx = this.ctx;
        
        // Clear background
        ctx.fillStyle = this.colors.background;
        ctx.fillRect(0, 0, width, height);
        
        // Draw scale marks
        ctx.fillStyle = this.colors.scale;
        for (let db = -60; db <= 0; db += 10) {
            const x = this.dbToPixel(db, width);
            ctx.fillRect(x, height - 5, 1, 5);
        }
        
        // Draw RMS level
        const rmsWidth = this.rmsLevel * width;
        const gradient = ctx.createLinearGradient(0, 0, width, 0);
        gradient.addColorStop(0, this.colors.rms);
        gradient.addColorStop(0.8, this.colors.peak);
        gradient.addColorStop(1, this.colors.clip);
        
        ctx.fillStyle = gradient;
        ctx.fillRect(2, 2, rmsWidth - 4, height - 4);
        
        // Draw peak hold
        if (this.peakHold > 0) {
            const peakX = this.peakHold * width;
            ctx.fillStyle = this.colors.peak;
            ctx.fillRect(peakX - 1, 0, 2, height);
        }
        
        // Draw clip indicator
        if (this.peakLevel > 0.95) {
            ctx.fillStyle = this.colors.clip;
            ctx.fillRect(width - 10, 0, 10, height);
        }
    }
    
    dbToPixel(db, width) {
        // Convert dB to linear position (-60dB to 0dB range)
        const normalizedDb = Math.max(0, Math.min(1, (db + 60) / 60));
        return normalizedDb * width;
    }
    
    linearToDb(linear) {
        return linear > 0 ? 20 * Math.log10(linear) : -120;
    }
}

/**
 * Audio Buffer Manager for efficient streaming
 */
class AudioBufferManager {
    constructor(bufferSize = 512, maxBuffers = 32) {
        this.bufferSize = bufferSize;
        this.maxBuffers = maxBuffers;
        this.buffers = [];
        this.writeIndex = 0;
        this.readIndex = 0;
        this.sampleRate = 48000;
    }
    
    addBuffer(audioData, timestamp) {
        if (this.buffers.length >= this.maxBuffers) {
            // Remove oldest buffer if at capacity
            this.buffers.shift();
            if (this.readIndex > 0) this.readIndex--;
        }
        
        this.buffers.push({
            data: audioData,
            timestamp: timestamp,
            sequenceId: this.writeIndex++
        });
    }
    
    getNextBuffer() {
        if (this.readIndex >= this.buffers.length) {
            return null;
        }
        
        return this.buffers[this.readIndex++];
    }
    
    clear() {
        this.buffers = [];
        this.writeIndex = 0;
        this.readIndex = 0;
    }
    
    getBufferCount() {
        return this.buffers.length - this.readIndex;
    }
}

// Export for use in main application
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { AudioMeter, AudioBufferManager };
}