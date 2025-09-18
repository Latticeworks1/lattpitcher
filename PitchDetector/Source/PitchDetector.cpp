#include "PitchDetector.h"
#include <algorithm>
#include <cmath>

//==============================================================================
// PITCH DETECTION ENGINE IMPLEMENTATION
//==============================================================================

PitchDetectionEngine::PitchDetectionEngine()
{
    initializeTelemetry();
    
    // Initialize fixed-size YIN buffers
    yinDifferenceFunction.fill(0.0f);
    yinCumulativeMean.fill(0.0f);
    
    // Precompute Hann window for YIN frame size
    for (int n = 0; n < PitchDetectorConstants::YIN_FRAME_SIZE; ++n) {
        hannWindow[static_cast<size_t>(n)] = 0.5f * (1.0f - std::cos(2.0f * MathConstants<float>::pi * n / (PitchDetectorConstants::YIN_FRAME_SIZE - 1)));
    }
}

float PitchDetectionEngine::detectPitch(const float* buffer, int size, double sampleRate)
{
    if (!buffer || size <= 0) return 0.0f;
    
    // Simple autocorrelation-based detection
    float frequency = autocorrelationPitchDetection(buffer, size, sampleRate);
    
    // Apply stability filter
    frequency = applyStabilityFilter(frequency);
    
    // Update telemetry (debug builds only)
    #ifdef DEBUG
    telemetry.totalDetectionAttempts++;
    if (frequency > 0.0f) {
        telemetry.successfulDetections++;
        // Use fixed-size circular buffer instead of vector operations
        static constexpr int MAX_RECENT_FREQUENCIES = 100;
        static float recentFrequencies[MAX_RECENT_FREQUENCIES];
        static std::atomic<int> frequencyIndex{0};
        recentFrequencies[frequencyIndex] = frequency;
        frequencyIndex = (frequencyIndex + 1) % MAX_RECENT_FREQUENCIES;
    }
    #endif
    
    return frequency;
}

float PitchDetectionEngine::autocorrelationPitchDetection(const float* buffer, int size, double sampleRate)
{
    if (size < 64) return 0.0f;
    
    int minPeriod = (int)(sampleRate / maxFrequency);
    int maxPeriod = (int)(sampleRate / minFrequency);
    
    if (maxPeriod > size / 2) maxPeriod = size / 2;
    if (minPeriod < 1) minPeriod = 1;
    
    // Use fixed-size buffer - NO DYNAMIC ALLOCATION IN AUDIO THREAD
    static constexpr int MAX_AUTOCORR_LAG = 2048; // covers down to ~21Hz at 44.1kHz
    static thread_local float autocorrBuffer[MAX_AUTOCORR_LAG];
    
    if (maxPeriod >= MAX_AUTOCORR_LAG) {
        maxPeriod = MAX_AUTOCORR_LAG - 1;
    }
    
    // Zero buffer only for used range
    std::fill_n(autocorrBuffer + minPeriod, maxPeriod - minPeriod + 1, 0.0f);
    
    // Compute autocorrelation with loop unrolling
    for (int lag = minPeriod; lag <= maxPeriod; ++lag) {
        float sum = 0.0f;
        int limit = size - lag;
        int i = 0;
        
        // Unroll by 4 for SIMD-friendly code
        for (; i <= limit - 4; i += 4) {
            sum += buffer[i] * buffer[i + lag] +
                   buffer[i + 1] * buffer[i + 1 + lag] +
                   buffer[i + 2] * buffer[i + 2 + lag] +
                   buffer[i + 3] * buffer[i + 3 + lag];
        }
        
        // Handle remaining samples
        for (; i < limit; ++i) {
            sum += buffer[i] * buffer[i + lag];
        }
        
        autocorrBuffer[lag] = sum;
    }
    
    // Find peak with early termination
    float maxCorr = noiseThreshold * size; // Set minimum threshold
    int bestLag = 0;
    
    for (int lag = minPeriod; lag <= maxPeriod; ++lag) {
        if (autocorrBuffer[lag] > maxCorr) {
            maxCorr = autocorrBuffer[lag];
            bestLag = lag;
        }
    }
    
    if (bestLag == 0) return 0.0f;
    
    return (float)sampleRate / bestLag;
}

float PitchDetectionEngine::detectPitchYin(const float* buffer, int size, double sampleRate)
{
    // Use advanced YIN for backward compatibility, frame 0
    YinResult result = detectPitchYinAdvanced(buffer, size, sampleRate, 0);
    return result.frequency;
}

float PitchDetectionEngine::detectPitchHPS(const float* buffer, int size, double sampleRate)
{
    return autocorrelationPitchDetection(buffer, size, sampleRate); // Simplified
}

float PitchDetectionEngine::detectPitchCepstrum(const float* buffer, int size, double sampleRate)
{
    return autocorrelationPitchDetection(buffer, size, sampleRate); // Simplified
}

NoteInfo PitchDetectionEngine::frequencyToNote(float frequency)
{
    NoteInfo info;
    if (frequency <= 0.0f) return info;
    
    const float A4 = PitchDetectorConstants::DEFAULT_REFERENCE_PITCH;
    const std::array<String, 12> noteNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    
    float logFreq = std::log2(frequency / A4);
    int totalSemitones = (int)std::round(logFreq * PitchDetectorConstants::SEMITONES_PER_OCTAVE);
    
    info.octave = 4 + totalSemitones / PitchDetectorConstants::SEMITONES_PER_OCTAVE;
    int noteIndex = (totalSemitones % PitchDetectorConstants::SEMITONES_PER_OCTAVE + PitchDetectorConstants::SEMITONES_PER_OCTAVE) % PitchDetectorConstants::SEMITONES_PER_OCTAVE;
    info.noteName = noteNames[noteIndex];
    
    float expectedFreq = A4 * std::pow(2.0f, totalSemitones / (float)PitchDetectorConstants::SEMITONES_PER_OCTAVE);
    info.centsDeviation = PitchDetectorConstants::CENTS_PER_OCTAVE * std::log2(frequency / expectedFreq);
    info.isValid = true;
    
    return info;
}

float PitchDetectionEngine::applyStabilityFilter(float newFrequency)
{
    if (newFrequency <= 0.0f) return 0.0f;
    
    // Use circular buffer for stability filter (no dynamic allocation)
    static constexpr int MAX_STABILITY_WINDOW = 10;
    static float stabilityBuffer[MAX_STABILITY_WINDOW];
    static std::atomic<int> stabilityIndex{0};
    static std::atomic<int> stabilityCount{0};
    
    stabilityBuffer[stabilityIndex] = newFrequency;
    stabilityIndex = (stabilityIndex + 1) % MAX_STABILITY_WINDOW;
    if (stabilityCount < MAX_STABILITY_WINDOW) stabilityCount++;
    
    return newFrequency; // Simplified - no filtering
}

void PitchDetectionEngine::initializeTelemetry()
{
    telemetry.reset();
    telemetry.platformInfo = SystemStats::getJUCEVersion();
}

String PitchDetectionEngine::exportTelemetryJson() const
{
    return "{}"; // Simplified
}

// Advanced YIN pitch detection with complete analysis pipeline
YinResult PitchDetectionEngine::detectPitchYinAdvanced(const float* buffer, int size, double sampleRate, int frameIndex)
{
    YinResult result;
    result.frameIndex = frameIndex;
    
    if (!buffer || size < PitchDetectorConstants::YIN_FRAME_SIZE) {
        return result;
    }
    
    // Use YIN constants for search range
    const int tauMin = static_cast<int>(sampleRate / PitchDetectorConstants::YIN_MAX_FREQ);
    const int tauMax = static_cast<int>(sampleRate / PitchDetectorConstants::YIN_MIN_FREQ);
    const int N = PitchDetectorConstants::YIN_FRAME_SIZE;
    
    // Apply windowing: x_k[n] = x[s_k + n] * w[n] using preallocated buffer
    static thread_local float windowedFrame[PitchDetectorConstants::YIN_FRAME_SIZE];
    for (int n = 0; n < N; ++n) {
        windowedFrame[n] = buffer[n] * hannWindow[n];
    }
    
    // Compute YIN difference function: d_k(τ) = Σ(x_k[n] - x_k[n-τ])²
    for (int tau = tauMin; tau <= tauMax && tau < MAX_YIN_LAG; ++tau) {
        float diff = 0.0f;
        for (int n = tau; n < N; ++n) {
            float delta = windowedFrame[n] - windowedFrame[n - tau];
            diff += delta * delta;
        }
        yinDifferenceFunction[tau] = diff;
    }
    
    // Compute cumulative mean normalized difference: C_k(τ) = d_k(τ) / ((1/τ) Σd_k(j))
    yinCumulativeMean[0] = 1.0f;  // Avoid division by zero at τ=0
    for (int tau = 1; tau <= tauMax && tau < MAX_YIN_LAG; ++tau) {
        float sum = 0.0f;
        for (int j = 1; j <= tau; ++j) {
            if (j < MAX_YIN_LAG) {
                sum += yinDifferenceFunction[j];
            }
        }
        float meanDiff = sum / tau;
        yinCumulativeMean[tau] = (meanDiff > PitchDetectorConstants::YIN_EPSILON) ? 
                                (yinDifferenceFunction[tau] / meanDiff) : 1.0f;
    }
    
    // Find minimum in search range: τ̂_k = argmin C_k(τ)
    int bestTau = tauMin;
    float minValue = yinCumulativeMean[tauMin];
    for (int tau = tauMin + 1; tau <= tauMax && tau < MAX_YIN_LAG; ++tau) {
        if (yinCumulativeMean[tau] < minValue) {
            minValue = yinCumulativeMean[tau];
            bestTau = tau;
        }
    }
    
    // Voicing strength: v[k] = 1 - C_k(τ0)
    result.voicingStrength = jlimit(0.0f, 1.0f, 1.0f - minValue);
    result.isVoiced = result.voicingStrength >= PitchDetectorConstants::YIN_VOICING_THRESHOLD;
    
    if (result.isVoiced && bestTau >= tauMin && bestTau <= tauMax) {
        // Quadratic interpolation for sub-sample precision
        float refinedTau = quadraticInterpolation(
            yinCumulativeMean[bestTau - 1],
            yinCumulativeMean[bestTau],
            yinCumulativeMean[bestTau + 1],
            bestTau
        );
        
        // Instantaneous fundamental frequency: f_in[k] = F_s / τ̃_k
        result.frequency = static_cast<float>(sampleRate / refinedTau);
        
        // Convert to cents relative to reference: c_in[k] = 1200 * log2(f_in[k] / f_ref)
        result.centsInput = 1200.0f * std::log2(result.frequency / PitchDetectorConstants::YIN_REFERENCE_FREQ);
        
        // Snap to nearest chromatic note: c_tgt[k] = 100 * round(c_in[k] / 100)
        result.centsTarget = 100.0f * std::round(result.centsInput / 100.0f);
        
        // Deviation before smoothing: Δc[k] = c_tgt[k] - c_in[k]
        result.centsDeviation = result.centsTarget - result.centsInput;
        
        // Apply causal smoothing
        applyCausalSmoothing(result);
        
        // Smoothed target frequency: f̂_tgt[k] = f_ref * 2^((c_in[k] + Δĉ[k])/1200)
        result.targetFrequency = PitchDetectorConstants::YIN_REFERENCE_FREQ * 
                               std::pow(2.0f, (result.centsInput + result.centsSmoothed) / 1200.0f);
        
        // Pitch shift ratio: r[k] = f̂_tgt[k] / f_in[k]
        result.pitchRatio = (result.frequency > PitchDetectorConstants::YIN_EPSILON) ? 
                           (result.targetFrequency / result.frequency) : 1.0f;
        
        // Update MIDI state
        updateMidiState(result);
    }
    
    return result;
}

float PitchDetectionEngine::quadraticInterpolation(float yMinus1, float y0, float yPlus1, int peakIndex)
{
    // Quadratic interpolation: τ̃_k = τ0 + (C- - C+) / (2(C- - 2C0 + C+))
    float denominator = yMinus1 - 2.0f * y0 + yPlus1;
    if (std::abs(denominator) < PitchDetectorConstants::YIN_EPSILON) {
        return static_cast<float>(peakIndex);  // No interpolation if denominator near zero
    }
    
    float offset = (yMinus1 - yPlus1) / (2.0f * denominator);
    return peakIndex + offset;
}

void PitchDetectionEngine::applyCausalSmoothing(YinResult& result)
{
    // Causal smoothing with voicing-dependent coefficient
    // λ[k] = λ_min + (λ_max - λ_min) * v[k]
    float lambda = PitchDetectorConstants::LAMBDA_MIN + 
                  (PitchDetectorConstants::LAMBDA_MAX - PitchDetectorConstants::LAMBDA_MIN) * 
                  jlimit(0.0f, 1.0f, result.voicingStrength);
    
    // One-pole recursion: Δĉ[k] = λ[k] * Δĉ[k-1] + (1 - λ[k]) * Δc[k]
    result.centsSmoothed = lambda * previousSmoothedDeviation + (1.0f - lambda) * result.centsDeviation;
    
    // Update state for next frame
    previousSmoothedDeviation = result.centsSmoothed;
}

void PitchDetectionEngine::updateMidiState(const YinResult& result)
{
    // MIDI emission with hysteresis gating
    bool shouldBeActive = result.voicingStrength >= PitchDetectorConstants::MIDI_GATE_ON_THRESHOLD;
    bool shouldBeInactive = result.voicingStrength <= PitchDetectorConstants::MIDI_GATE_OFF_THRESHOLD;
    
    // Hysteresis state machine
    if (!previousNoteState && shouldBeActive) {
        // Note on
        previousNoteState = true;
        currentMidiNote = static_cast<int>(69.0f + result.centsTarget / 100.0f);
    } else if (previousNoteState && shouldBeInactive) {
        // Note off
        previousNoteState = false;
        currentMidiNote = -1;
    }
    
    // MIDI velocity scaled from voicing strength
    if (currentMidiNote >= 0) {
        int velocity = static_cast<int>(result.voicingStrength * PitchDetectorConstants::MIDI_MAX_VELOCITY);
        velocity = jlimit(PitchDetectorConstants::MIDI_MIN_VELOCITY, PitchDetectorConstants::MIDI_MAX_VELOCITY, velocity);
    }
}

//==============================================================================
// AUTOTUNE ENGINE IMPLEMENTATION  
//==============================================================================

AutotuneEngine::AutotuneEngine()
{
    delayBuffer.assign(maxDelayInSamples, 0.0f);  // Use assign instead of resize
    windowBuffer.assign(PitchDetectorConstants::AUTOTUNE_WINDOW_SIZE, 0.0f);
    overlapBuffer.assign(PitchDetectorConstants::AUTOTUNE_OVERLAP_SIZE, 0.0f);
    
    // Create Hann window
    const int windowSize = PitchDetectorConstants::AUTOTUNE_WINDOW_SIZE;
    for (int i = 0; i < windowSize; ++i)
        windowBuffer[i] = 0.5f * (1.0f - std::cos(2.0f * MathConstants<float>::pi * i / (windowSize - 1)));
    
    // Initialize phase vocoder components
    initializePhaseVocoder();
}

void AutotuneEngine::prepareToPlay(double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate;
    blockSize = maximumExpectedSamplesPerBlock;
    reset();
}

void AutotuneEngine::processBlock(AudioBuffer<float>& buffer, const float* pitchData, int numSamples)
{
    if (!pitchData || numSamples <= 0) return;
    
    float* audioData = buffer.getWritePointer(0);
    
    // If we have enough samples for a YIN frame, use advanced processing
    if (numSamples >= PitchDetectorConstants::YIN_FRAME_SIZE) {
        // Create a temporary PitchDetectionEngine for YIN analysis
        // In production, this should be a member to maintain state continuity
        PitchDetectionEngine yinEngine;
        
        // Analyze with advanced YIN
        YinResult yinResult = yinEngine.detectPitchYinAdvanced(audioData, numSamples, sampleRate, analysisFrameCounter);
        
        if (yinResult.isVoiced && yinResult.pitchRatio != 1.0f) {
            // Apply correction strength scaling
            float effectivePitchRatio = 1.0f + (yinResult.pitchRatio - 1.0f) * settings.correctionStrength;
            
            // Use phase vocoder for high-quality pitch shifting
            if (engineMode == EngineMode::PhaseVocoder || engineMode == EngineMode::Auto) {
                processPhaseVocoder(audioData, numSamples, effectivePitchRatio);
            } else {
                // Fallback to PSOLA for vocal mode
                processPSOLA(audioData, numSamples, effectivePitchRatio);
            }
        }
    } else {
        // Fallback to simple processing for short blocks
        for (int i = 0; i < numSamples; ++i) {
            float detectedPitch = pitchData[i];
            if (detectedPitch > 0.0f) {
                float targetPitch = calculateTargetPitch(detectedPitch);
                float correctionRatio = targetPitch / detectedPitch;
                
                // Apply correction strength
                float effectiveRatio = 1.0f + (correctionRatio - 1.0f) * settings.correctionStrength;
                
                // Simple amplitude modulation as placeholder
                jassert(i < numSamples);
                audioData[i] *= effectiveRatio * 0.1f + 0.9f;
            }
        }
    }
    
    totalProcessedSamples += numSamples;
    analysisFrameCounter++;
}

void AutotuneEngine::reset()
{
    currentTargetPitch = 0.0f;
    previousTargetPitch = 0.0f;
    currentCorrectionAmount = 0.0f;
    pitchCorrectionActive = false;
    std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
}

float AutotuneEngine::calculateTargetPitch(float detectedPitch)
{
    if (detectedPitch <= 0.0f) return 0.0f;
    
    int midiNote = frequencyToMidiNote(detectedPitch);
    int targetMidiNote = findNearestScaleNote(midiNote);
    return midiNoteToFrequency(targetMidiNote);
}

int AutotuneEngine::frequencyToMidiNote(float frequency) const
{
    return (int)std::round(PitchDetectorConstants::SEMITONES_PER_OCTAVE * std::log2(frequency / settings.referencePitch)) + 69;
}

float AutotuneEngine::midiNoteToFrequency(int midiNote) const
{
    return settings.referencePitch * std::pow(2.0f, (midiNote - 69) / (float)PitchDetectorConstants::SEMITONES_PER_OCTAVE);
}

bool AutotuneEngine::isNoteInScale(int midiNote) const
{
    int noteClass = (midiNote - settings.rootNote + 12) % 12;
    return settings.customScale[noteClass];
}

int AutotuneEngine::findNearestScaleNote(int midiNote) const
{
    if (settings.scaleType == ScaleType::Chromatic) return midiNote;
    if (isNoteInScale(midiNote)) return midiNote;
    
    // Find nearest note in scale (search up to half octave)
    const int maxSearchDistance = PitchDetectorConstants::SEMITONES_PER_OCTAVE / 2;
    for (int distance = 1; distance <= maxSearchDistance; ++distance) {
        if (isNoteInScale(midiNote + distance)) return midiNote + distance;
        if (isNoteInScale(midiNote - distance)) return midiNote - distance;
    }
    
    return midiNote;
}

std::array<bool, 12> AutotuneEngine::getActiveScale() const
{
    switch (settings.scaleType) {
        case ScaleType::Major:
            return {true, false, true, false, true, true, false, true, false, true, false, true};
        case ScaleType::Minor:
            return {true, false, true, true, false, true, false, true, true, false, true, false};
        case ScaleType::Pentatonic:
            return {true, false, true, false, true, false, false, true, false, true, false, false};
        case ScaleType::Blues:
            return {true, false, false, true, false, true, true, true, false, false, true, false};
        case ScaleType::Dorian:
            return {true, false, true, true, false, true, false, true, false, true, true, false};
        case ScaleType::Custom:
            return settings.customScale;
        case ScaleType::Chromatic:
        default:
            return {true, true, true, true, true, true, true, true, true, true, true, true};
    }
}

// Phase Vocoder Implementation
void AutotuneEngine::initializePhaseVocoder()
{
    const int N = PitchDetectorConstants::PV_FFT_SIZE;
    
    // Initialize FFT objects
    int fftOrder = static_cast<int>(std::log2(N));
    analysisFFT = std::make_unique<juce::dsp::FFT>(fftOrder);
    synthesisFFT = std::make_unique<juce::dsp::FFT>(fftOrder);
    
    // Initialize buffers
    analysisFrame.assign(N, std::complex<float>(0.0f, 0.0f));
    synthesisFrame.assign(N, std::complex<float>(0.0f, 0.0f));
    previousPhase.assign(N / 2 + 1, 0.0f);  // Only positive frequencies
    synthesisPhase.assign(N / 2 + 1, 0.0f);
    instantaneousFreq.assign(N / 2 + 1, 0.0f);
    
    // Precompute analysis and synthesis windows (Hann)
    analysisWindow.assign(N, 0.0f);
    synthesisWindow.assign(N, 0.0f);
    for (int n = 0; n < N; ++n) {
        float w = 0.5f * (1.0f - std::cos(2.0f * MathConstants<float>::pi * n / (N - 1)));
        analysisWindow[n] = w;
        synthesisWindow[n] = w;  // Same window for both analysis and synthesis
    }
    
    // Initialize overlap-add buffer (larger to handle variable synthesis hops)
    overlapAddBuffer.assign(N * 4, 0.0f);
    temporaryBuffer.assign(N * 8, 0.0f);  // For resampling intermediate signal
}

void AutotuneEngine::processPhaseVocoder(float* audioData, int numSamples, float pitchRatio)
{
    const int N = PitchDetectorConstants::PV_FFT_SIZE;
    const int H = PitchDetectorConstants::PV_HOP_SIZE;
    const float epsilon = PitchDetectorConstants::YIN_EPSILON;
    
    // Synthesis hop size: H_s[k] = ⌊H * r[k]⌉
    int synthesisHop = static_cast<int>(std::round(H * pitchRatio));
    synthesisHop = jlimit(H / 4, H * 4, synthesisHop);  // Reasonable bounds
    
    // Process frame by frame
    for (int sampleIndex = 0; sampleIndex < numSamples; sampleIndex += H) {
        int frameSamples = jmin(H, numSamples - sampleIndex);
        
        // Clear analysis frame
        std::fill(analysisFrame.begin(), analysisFrame.end(), std::complex<float>(0.0f, 0.0f));
        
        // Copy and window input samples: x_k[n] = x[s_k + n] * w[n]
        for (int n = 0; n < frameSamples && n < N; ++n) {
            if (sampleIndex + n < numSamples) {
                float sample = audioData[sampleIndex + n] * analysisWindow[n];
                analysisFrame[n] = std::complex<float>(sample, 0.0f);
            }
        }
        
        // Forward FFT: Analysis STFT X_k[m]
        analysisFFT->perform(reinterpret_cast<const juce::dsp::Complex<float>*>(analysisFrame.data()), 
                           reinterpret_cast<juce::dsp::Complex<float>*>(analysisFrame.data()), false);
        
        // Compute instantaneous frequency per bin
        const int numBins = N / 2 + 1;  // Only positive frequencies
        for (int m = 0; m < numBins; ++m) {
            float magnitude = std::abs(analysisFrame[m]);
            float phase = std::arg(analysisFrame[m]);
            
            if (magnitude > epsilon) {
                // Phase unwrapping: Δφ_k[m] = princarg(∠X_k[m] - ∠X_{k-1}[m] - 2π m H / N)
                float expectedPhase = previousPhase[m] + (2.0f * MathConstants<float>::pi * m * H) / N;
                float phaseDiff = principalArgument(phase - expectedPhase);
                
                // Instantaneous frequency: ω_k[m] = 2π m / N + Δφ_k[m] / H
                instantaneousFreq[m] = (2.0f * MathConstants<float>::pi * m) / N + phaseDiff / H;
                
                // Update synthesis phase: ∠Y_k[m] = ∠Y_{k-1}[m] + ω_k[m] * H_s[k]
                synthesisPhase[m] += instantaneousFreq[m] * synthesisHop;
                
                // Synthesize with modified magnitude and accumulated phase
                synthesisFrame[m] = std::polar(magnitude, synthesisPhase[m]);
                
                // Store current phase for next frame
                previousPhase[m] = phase;
            } else {
                synthesisFrame[m] = std::complex<float>(0.0f, 0.0f);
            }
        }
        
        // Mirror negative frequencies for real IFFT
        for (int m = numBins; m < N; ++m) {
            synthesisFrame[m] = std::conj(synthesisFrame[N - m]);
        }
        
        // Inverse FFT: Y_k[m] → time domain
        synthesisFFT->perform(reinterpret_cast<const juce::dsp::Complex<float>*>(synthesisFrame.data()),
                            reinterpret_cast<juce::dsp::Complex<float>*>(synthesisFrame.data()), true);
        
        // Overlap-add synthesis with adaptive hop
        for (int n = 0; n < N && n < static_cast<int>(overlapAddBuffer.size()) - sampleIndex; ++n) {
            if (sampleIndex + n < static_cast<int>(overlapAddBuffer.size())) {
                float synthSample = synthesisFrame[n].real() * synthesisWindow[n];
                overlapAddBuffer[sampleIndex + n] += synthSample;
            }
        }
        
        analysisFrameCounter++;
    }
    
    // Copy overlap-add result back to audio data (simplified - should use resampling)
    for (int i = 0; i < numSamples && i < static_cast<int>(overlapAddBuffer.size()); ++i) {
        jassert(i < (int)overlapAddBuffer.size());
        audioData[i] = overlapAddBuffer[i];
        overlapAddBuffer[i] = 0.0f;  // Clear for next use
    }
    
    // Shift remaining overlap data (simplified approach)
    int shiftAmount = numSamples;
    for (int i = 0; i < static_cast<int>(overlapAddBuffer.size()) - shiftAmount; ++i) {
        overlapAddBuffer[i] = overlapAddBuffer[i + shiftAmount];
    }
    for (int i = static_cast<int>(overlapAddBuffer.size()) - shiftAmount; i < static_cast<int>(overlapAddBuffer.size()); ++i) {
        overlapAddBuffer[i] = 0.0f;
    }
}

float AutotuneEngine::principalArgument(float phase)
{
    // princarg() - wrap phase to [-π, π]
    while (phase > MathConstants<float>::pi) {
        phase -= MathConstants<float>::twoPi;
    }
    while (phase < -MathConstants<float>::pi) {
        phase += MathConstants<float>::twoPi;
    }
    return phase;
}

void AutotuneEngine::lagrangeInterpolate8(const float* input, float* output, int outputLength, const float* timeMap)
{
    // 8-tap Lagrange interpolation (simplified implementation)
    // This should be a proper bandlimited interpolator for production use
    for (int i = 0; i < outputLength; ++i) {
        float t = timeMap[i];
        int baseIndex = static_cast<int>(t);
        float frac = t - baseIndex;
        
        if (baseIndex >= 3 && baseIndex < outputLength - 4) {
            // Simple linear interpolation as placeholder for full Lagrange
            output[i] = input[baseIndex] * (1.0f - frac) + input[baseIndex + 1] * frac;
        } else {
            output[i] = 0.0f;  // Zero pad at boundaries
        }
    }
}

void AutotuneEngine::processPSOLA(float* audioData, int numSamples, float pitchRatio)
{
    // Simplified PSOLA implementation (placeholder)
    // In production, this would use pitch-synchronous overlap-add with epoch detection
    
    if (pitchRatio == 1.0f) return;  // No processing needed
    
    // Simple time-stretching approach as placeholder
    for (int i = 0; i < numSamples; ++i) {
        // Apply simple gain modulation based on pitch ratio
        jassert(i < numSamples);
        audioData[i] *= (1.0f + (pitchRatio - 1.0f) * 0.5f);
    }
}

//==============================================================================
// GUI COMPONENTS IMPLEMENTATION
//==============================================================================

AutoTunePitchDisplay::AutoTunePitchDisplay()
{
    setSize(400, 300);
}

void AutoTunePitchDisplay::paint(Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    
    // Professional Auto-Tune style dark background with subtle gradient
    g.setGradientFill(ColourGradient(
        Colour(0xff1a1a1a), bounds.getX(), bounds.getY(),
        Colour(0xff0f0f0f), bounds.getRight(), bounds.getBottom(), false));
    g.fillAll();
    
    // Split into three main areas: scale grid (top), pitch wheel (center), correction indicator (bottom)
    auto scaleArea = bounds.removeFromTop(bounds.getHeight() * 0.25f);
    auto wheelArea = bounds.removeFromTop(bounds.getHeight() * 0.6f);
    auto correctionArea = bounds;
    
    drawScaleGrid(g, scaleArea);
    drawPitchWheel(g, wheelArea);
    drawCorrectionIndicator(g, correctionArea);
}

void AutoTunePitchDisplay::drawScaleGrid(Graphics& g, Rectangle<float> area)
{
    // Draw scale note grid like Auto-Tune
    const String scaleNames[] = {"Chromatic", "Major", "Minor", "Dorian", "Pentatonic", "Blues"};
    const String noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    
    // Current scale indicator
    g.setColour(Colour(0xff4a90e2));
    g.fillRect(area.reduced(2));
    
    g.setColour(Colour(0xff000000));
    g.setFont(FontOptions(14.0f, Font::bold));
    g.drawText("SCALE: " + scaleNames[currentScale], area.reduced(8), Justification::centredLeft);
    
    // Note grid - show active notes in current scale
    auto noteArea = area.removeFromRight(area.getWidth() * 0.7f);
    float noteWidth = noteArea.getWidth() / 12.0f;
    
    for (int i = 0; i < 12; ++i) {
        auto noteRect = Rectangle<float>(noteArea.getX() + i * noteWidth, noteArea.getY() + 5, 
                                        noteWidth - 2, noteArea.getHeight() - 10);
        
        // Color based on whether note is in current scale
        bool inScale = (currentScale == 0) || ((i % 2 == 0) && currentScale == 1); // Simplified for demo
        Colour noteColor = inScale ? Colour(0xff00ff80) : Colour(0x40ffffff);
        
        g.setColour(noteColor);
        g.fillRect(noteRect);
        
        g.setColour(Colour(0xff000000));
        g.setFont(FontOptions(10.0f));
        g.drawText(noteNames[i], noteRect, Justification::centred);
    }
}

void AutoTunePitchDisplay::drawPitchWheel(Graphics& g, Rectangle<float> area)
{
    auto centre = area.getCentre();
    float radius = jmin(area.getWidth(), area.getHeight()) * 0.4f;
    
    // Professional pitch wheel background
    g.setColour(Colour(0xff2a2a2a));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);
    
    // Pitch correction zones
    g.setColour(Colour(0x4000ff00)); // Green zone ±10 cents
    g.fillEllipse(centre.x - radius * 0.8f, centre.y - radius * 0.8f, radius * 1.6f, radius * 1.6f);
    
    g.setColour(Colour(0x40ffff00)); // Yellow zone ±20 cents  
    g.fillEllipse(centre.x - radius * 0.6f, centre.y - radius * 0.6f, radius * 1.2f, radius * 1.2f);
    
    // Center perfect pitch zone
    g.setColour(Colour(0x8000ff00));
    g.fillEllipse(centre.x - radius * 0.2f, centre.y - radius * 0.2f, radius * 0.4f, radius * 0.4f);
    
    // Current note indicator
    if (hasValidPitch) {
        float angle = currentCents * MathConstants<float>::pi / 100.0f; // ±100 cents = ±π radians
        float indicatorX = centre.x + radius * 0.9f * std::sin(angle);
        float indicatorY = centre.y - radius * 0.9f * std::cos(angle);
        
        Colour indicatorColor = (std::abs(currentCents) < 10.0f) ? Colour(0xff00ff00) : 
                               (std::abs(currentCents) < 25.0f) ? Colour(0xffffff00) : Colour(0xffff0000);
        
        g.setColour(indicatorColor);
        g.fillEllipse(indicatorX - 8, indicatorY - 8, 16, 16);
        
        // Current note name in center
        g.setColour(Colour(0xffffffff));
        g.setFont(FontOptions(24.0f, Font::bold));
        g.drawText(currentNote, Rectangle<float>(centre.x - 30, centre.y - 12, 60, 24), Justification::centred);
    }
    
    // Cents scale markings
    g.setColour(Colour(0x80ffffff));
    for (int cents = -50; cents <= 50; cents += 10) {
        float angle = cents * MathConstants<float>::pi / 100.0f;
        float x1 = centre.x + radius * 1.05f * std::sin(angle);
        float y1 = centre.y - radius * 1.05f * std::cos(angle);
        float x2 = centre.x + radius * 1.15f * std::sin(angle);
        float y2 = centre.y - radius * 1.15f * std::cos(angle);
        
        g.drawLine(x1, y1, x2, y2, 2.0f);
        
        if (cents % 20 == 0) {
            g.setFont(FontOptions(10.0f));
            g.drawText(String(cents), Rectangle<float>(x2 - 10, y2 - 6, 20, 12), Justification::centred);
        }
    }
}

void AutoTunePitchDisplay::drawCorrectionIndicator(Graphics& g, Rectangle<float> area)
{
    // Correction strength and status indicator
    g.setColour(Colour(0xff333333));
    g.fillRect(area);
    
    g.setColour(Colour(0xff4a90e2));
    g.setFont(FontOptions(12.0f, Font::bold));
    g.drawText("CORRECTION: " + String((int)(correctionStrength * 100)) + "%", 
               area.reduced(8), Justification::centredLeft);
    
    // Correction activity LED
    if (isCorrectingPitch) {
        g.setColour(Colour(0xff00ff00));
        g.fillEllipse(area.getRight() - 30, area.getCentreY() - 6, 12, 12);
        g.setColour(Colour(0xffffffff));
        g.setFont(FontOptions(10.0f));
        g.drawText("ACTIVE", Rectangle<float>(area.getRight() - 70, area.getCentreY() - 6, 35, 12), Justification::centred);
    }
}

void AutoTunePitchDisplay::resized() {}

void AutoTunePitchDisplay::updatePitch(float frequency, const NoteInfo& noteInfo)
{
    if (noteInfo.isValid && !noteInfo.noteName.isEmpty()) {
        currentCents = noteInfo.centsDeviation;
        currentNote = noteInfo.noteName + String(noteInfo.octave);
        hasValidPitch = true;
        isCorrectingPitch = std::abs(currentCents) > 8.0f; // Correction active when off by >8 cents
    } else {
        hasValidPitch = false;
        currentNote = "--";
        currentCents = 0.0f;
        isCorrectingPitch = false;
    }
    repaint();
}

void AutoTunePitchDisplay::setTargetNote(const String& note)
{
    targetNote = note;
    repaint();
}

//==============================================================================
// PitchTrackingStrip Implementation
//==============================================================================

void PitchTrackingStrip::paint(Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    
    // Dark background
    g.setColour(Colour(0xff1a1a1a));
    g.fillRect(bounds);
    
    if (sampleCount == 0) return;
    
    // Draw pitch deviation history
    Path pitchPath;
    bool firstPoint = true;
    
    const float width = bounds.getWidth();
    const float height = bounds.getHeight();
    const float centerY = height * 0.5f;
    const int currentSampleCount = sampleCount.load();
    const int maxPoints = jmin(currentSampleCount, MAX_HISTORY_SAMPLES);
    
    if (maxPoints > 0) {
        const int currentWriteIndex = writeIndex.load();
        
        for (int i = 0; i < maxPoints; ++i)
        {
            // Read from circular buffer in correct order
            int readIndex = (currentWriteIndex - maxPoints + i + MAX_HISTORY_SAMPLES) % MAX_HISTORY_SAMPLES;
            const float cents = historyBuffer[readIndex];
            
            if (std::isnan(cents)) continue;  // Skip invalid data
            
            const float x = (maxPoints <= 1) ? width * 0.5f : (i / float(maxPoints - 1)) * width;
            const float y = centerY - (cents / range) * (height * 0.4f);
            
            // Ensure valid coordinates
            if (!std::isfinite(x) || !std::isfinite(y)) continue;
            
            if (firstPoint)
            {
                pitchPath.startNewSubPath(x, y);
                firstPoint = false;
            }
            else
            {
                pitchPath.lineTo(x, y);
            }
        }
    }
    
    // Draw the pitch curve
    g.setColour(Colours::cyan.withAlpha(0.8f));
    g.strokePath(pitchPath, PathStrokeType(1.5f));
    
    // Draw center line
    g.setColour(Colour(0xff444444));
    g.drawHorizontalLine((int)centerY, 0.0f, width);
}

void PitchTrackingStrip::addCents(float cents)
{
    // Thread-safe circular buffer write
    historyBuffer[writeIndex] = cents;
    writeIndex = (writeIndex + 1) % MAX_HISTORY_SAMPLES;
    if (sampleCount < MAX_HISTORY_SAMPLES) {
        sampleCount++;
    }
    repaint();
}

//==============================================================================



//==============================================================================
//==============================================================================

PianoRollDisplay::PianoRollDisplay()
{
    setSize(800, 300);
    startTimer(16);  // 60fps updates
    sessionStartTime = Time::getCurrentTime();
}

PianoRollDisplay::~PianoRollDisplay()
{
    stopTimer();
}

void PianoRollDisplay::paint(Graphics& g)
{
    auto bounds = getLocalBounds();
    
    // Dark background
    g.fillAll(Colour(0xff1a1a1a));
    
    // Split into piano keys (left) and roll area (right)
    auto keyArea = bounds.removeFromLeft(PIANO_KEY_WIDTH);
    auto rollArea = bounds;
    
    drawPianoKeys(g, keyArea);
    drawPianoRoll(g, rollArea);
}

void PianoRollDisplay::drawPianoKeys(Graphics& g, Rectangle<int> keyArea)
{
    const int totalNotes = maxMidiNote - minMidiNote + 1;
    const float keyHeight = static_cast<float>(keyArea.getHeight()) / totalNotes;
    
    for (int i = 0; i <= totalNotes; ++i) {
        int midiNote = maxMidiNote - i;  // Draw from top (high notes) to bottom (low notes)
        float y = i * keyHeight;
        
        bool isBlack = isBlackKey(midiNote);
        bool isC = (midiNote % 12) == 0;
        
        // Key background
        Colour keyColour = isBlack ? Colour(0xff2a2a2a) : Colour(0xfff0f0f0);
        g.setColour(keyColour);
        g.fillRect(keyArea.getX(), static_cast<int>(y), keyArea.getWidth(), static_cast<int>(keyHeight));
        
        // Key border
        g.setColour(Colour(0xff666666));
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(keyArea.getX()), static_cast<float>(keyArea.getRight()));
        
        // Note name for C notes
        if (isC && keyHeight > 15) {
            g.setColour(isBlack ? Colours::white : Colours::black);
            g.setFont(FontOptions(10.0f));
            g.drawText(getMidiNoteName(midiNote), 
                      keyArea.getX() + 2, static_cast<int>(y), keyArea.getWidth() - 4, static_cast<int>(keyHeight),
                      Justification::centredLeft);
        }
    }
    
    // Right border
    g.setColour(Colour(0xff444444));
    g.drawVerticalLine(keyArea.getRight(), static_cast<float>(keyArea.getY()), static_cast<float>(keyArea.getBottom()));
}

void PianoRollDisplay::drawPianoRoll(Graphics& g, Rectangle<int> rollArea)
{
    // Background with slight gradient
    g.setGradientFill(ColourGradient(
        Colour(0xff0f0f0f), rollArea.getX(), rollArea.getY(),
        Colour(0xff1a1a1a), rollArea.getRight(), rollArea.getBottom(), false));
    g.fillRect(rollArea);
    
    drawGridLines(g, rollArea);
    
    const int totalNotes = maxMidiNote - minMidiNote + 1;
    const float keyHeight = static_cast<float>(rollArea.getHeight()) / totalNotes;
    const float currentTimeX = rollArea.getRight() - (currentTime * PIXELS_PER_SECOND);
    
    // Draw notes
    for (const auto& note : pianoRollNotes) {
        if (note.midiNote < minMidiNote || note.midiNote > maxMidiNote) continue;
        
        float noteY = (maxMidiNote - note.midiNote) * keyHeight;
        float noteX = rollArea.getRight() - ((currentTime - note.startTime) * PIXELS_PER_SECOND);
        float noteWidth = note.duration * PIXELS_PER_SECOND;
        
        // Only draw if visible
        if (noteX + noteWidth >= rollArea.getX() && noteX < rollArea.getRight()) {
            Rectangle<float> noteBounds(noteX, noteY + 1, 
                                      jmax(2.0f, noteWidth), keyHeight - 2);
            
            // Note color based on pitch accuracy and velocity
            Colour noteColour = getNoteColour(note.pitchAccuracy, note.velocity);
            
            // Draw note with glow effect if active
            if (note.isActive) {
                g.setColour(noteColour.withAlpha(0.3f));
                g.fillRoundedRectangle(noteBounds.expanded(2), 3.0f);
            }
            
            g.setColour(noteColour);
            g.fillRoundedRectangle(noteBounds, 2.0f);
            
            // Brighter border
            g.setColour(noteColour.brighter(0.3f));
            g.drawRoundedRectangle(noteBounds, 2.0f, 1.0f);
        }
    }
    
    // Current time line
    g.setColour(Colour(0xff00ffff));
    g.drawVerticalLine(static_cast<int>(currentTimeX), static_cast<float>(rollArea.getY()), static_cast<float>(rollArea.getBottom()));
}

void PianoRollDisplay::drawGridLines(Graphics& g, Rectangle<int> area)
{
    g.setColour(Colour(0x20ffffff));
    
    // Horizontal grid lines (note boundaries)
    const int totalNotes = maxMidiNote - minMidiNote + 1;
    const float keyHeight = static_cast<float>(area.getHeight()) / totalNotes;
    
    for (int i = 0; i <= totalNotes; ++i) {
        int midiNote = maxMidiNote - i;
        float y = i * keyHeight;
        
        // Stronger lines for C notes
        bool isC = (midiNote % 12) == 0;
        g.setColour(isC ? Colour(0x40ffffff) : Colour(0x10ffffff));
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
    }
    
    // Vertical grid lines (time)
    g.setColour(Colour(0x10ffffff));
    const float timeStep = 1.0f;  // 1 second intervals
    for (float t = 0; t < timeRangeSeconds; t += timeStep) {
        float x = area.getRight() - (t * PIXELS_PER_SECOND);
        if (x >= area.getX() && x <= area.getRight()) {
            g.drawVerticalLine(static_cast<int>(x), static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
        }
    }
}

Colour PianoRollDisplay::getNoteColour(float pitchAccuracy, float velocity)
{
    // Base color depends on pitch accuracy
    Colour baseColour;
    if (std::abs(pitchAccuracy) < 8.0f) {
        baseColour = Colour(0xff00ff80);  // Green for accurate
    } else if (std::abs(pitchAccuracy) < 25.0f) {
        baseColour = Colour(0xffffff80);  // Yellow for close
    } else {
        baseColour = Colour(0xffff6080);  // Red for off-pitch
    }
    
    // Modulate alpha by velocity
    return baseColour.withAlpha(0.6f + velocity * 0.4f);
}

int PianoRollDisplay::frequencyToMidiNote(float frequency)
{
    return static_cast<int>(std::round(69.0f + 12.0f * std::log2(frequency / 440.0f)));
}

String PianoRollDisplay::getMidiNoteName(int midiNote) const
{
    const char* noteNames[] = {"C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B"};
    int noteClass = midiNote % 12;
    int octave = (midiNote / 12) - 1;
    return String(noteNames[noteClass]) + String(octave);
}

bool PianoRollDisplay::isBlackKey(int midiNote)
{
    int noteClass = midiNote % 12;
    return (noteClass == 1 || noteClass == 3 || noteClass == 6 || noteClass == 8 || noteClass == 10);
}

void PianoRollDisplay::addNote(int midiNote, float startTime, float velocity, float pitchAccuracy)
{
    pianoRollNotes.emplace_back(midiNote, startTime, velocity, pitchAccuracy);
    repaint();
}

void PianoRollDisplay::updateCurrentNote(int midiNote, float accuracy)
{
    if (!pianoRollNotes.empty() && pianoRollNotes.back().isActive) {
        auto& lastNote = pianoRollNotes.back();
        if (lastNote.midiNote == midiNote) {
            lastNote.pitchAccuracy = accuracy;
            lastNote.duration = currentTime - lastNote.startTime;
            repaint();
        }
    }
}

void PianoRollDisplay::endCurrentNote()
{
    if (!pianoRollNotes.empty() && pianoRollNotes.back().isActive) {
        pianoRollNotes.back().isActive = false;
        pianoRollNotes.back().duration = currentTime - pianoRollNotes.back().startTime;
        repaint();
    }
}

void PianoRollDisplay::clearHistory()
{
    pianoRollNotes.clear();
    sessionStartTime = Time::getCurrentTime();
    currentTime = 0.0f;
    repaint();
}

void PianoRollDisplay::timerCallback()
{
    // Update current time
    currentTime = static_cast<float>((Time::getCurrentTime() - sessionStartTime).inMilliseconds()) / 1000.0f;
    
    // Remove notes that are too old (GUI operation, not audio thread)
    auto cutoffTime = currentTime - timeRangeSeconds;
    if (!pianoRollNotes.empty()) {
        // Use iterator-based removal to avoid vector.erase() with range
        for (auto it = pianoRollNotes.begin(); it != pianoRollNotes.end();) {
            if (it->startTime + it->duration < cutoffTime) {
                it = pianoRollNotes.erase(it);  // Safe single element erase
            } else {
                ++it;
            }
        }
    }
    
    repaint();
}

void PianoRollDisplay::resized()
{
    // Handle component resizing
}

bool PianoRollDisplay::exportRecordingToCSV(const File& file) const
{
    if (recordedNotes.empty())
        return false;
        
    std::unique_ptr<FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;
    
    // Write CSV header
    String header = "Time,Note,Velocity,Duration,PitchAccuracy\n";
    stream->write(header.toUTF8(), header.getNumBytesAsUTF8());
    
    // Write note data
    for (const auto& note : recordedNotes)
    {
        String line = String(note.startTime, 3) + "," +
                     getMidiNoteName(note.midiNote) + "," +
                     String(note.velocity, 2) + "," +
                     String(note.duration, 3) + "," +
                     String(note.pitchAccuracy, 1) + "\n";
        stream->write(line.toUTF8(), line.getNumBytesAsUTF8());
    }
    
    return true;
}

//==============================================================================
// SPECTROGRAM DISPLAY IMPLEMENTATION
//==============================================================================

SpectrogramDisplay::SpectrogramDisplay()
{
    setSize(800, 200);
    startTimer(30);  // ~30fps updates for spectrogram
    
    // Initialize FFT
    int fftOrder = static_cast<int>(std::log2(FFT_SIZE));
    spectrogramFFT = std::make_unique<juce::dsp::FFT>(fftOrder);
    
    fftBuffer.assign(FFT_SIZE * 2, 0.0f);  // Real + imaginary - use assign
    windowBuffer.assign(FFT_SIZE, 0.0f);
    
    // Precompute Hann window
    for (int i = 0; i < FFT_SIZE; ++i) {
        windowBuffer[i] = 0.5f * (1.0f - std::cos(2.0f * MathConstants<float>::pi * i / (FFT_SIZE - 1)));
    }
}

SpectrogramDisplay::~SpectrogramDisplay()
{
    stopTimer();
}

void SpectrogramDisplay::paint(Graphics& g)
{
    auto bounds = getLocalBounds();
    
    // Dark background with gradient
    g.setGradientFill(ColourGradient(
        Colour(0xff0a0a0a), bounds.getX(), bounds.getY(),
        Colour(0xff1a1a1a), bounds.getRight(), bounds.getBottom(), false));
    g.fillRect(bounds);
    
    if (spectrogramHistory.empty()) {
        g.setColour(Colour(0x80ffffff));
        g.setFont(FontOptions(14.0f));
        g.drawText("🎵 Spectrogram - Play audio to see frequency content", bounds, Justification::centred);
        return;
    }
    
    // Optimized spectrogram rendering - use image instead of individual fillRect calls
    if (!spectrogramHistory.empty()) {
        // Create cached image for better performance
        static Image spectrogramImage;
        static bool needsUpdate = true;
        
        if (needsUpdate || spectrogramImage.getWidth() != bounds.getWidth() || spectrogramImage.getHeight() != bounds.getHeight()) {
            spectrogramImage = Image(Image::RGB, (int)bounds.getWidth(), (int)bounds.getHeight(), true);
            Graphics imageGraphics(spectrogramImage);
            
            const int maxFrames = jmin(static_cast<int>(spectrogramHistory.size()), bounds.getWidth() / WATERFALL_HEIGHT);
            
            for (int frameIdx = 0; frameIdx < maxFrames; ++frameIdx) {
                const auto& frame = spectrogramHistory[spectrogramHistory.size() - 1 - frameIdx];
                float x = bounds.getWidth() - (frameIdx + 1) * WATERFALL_HEIGHT;
                
                if (!frame.magnitudes.empty() && x >= 0) {
                    // Draw simplified spectrogram - sample every 4th bin for performance
                    const int binStep = 4;
                    for (int bin = 0; bin < (int)frame.magnitudes.size(); bin += binStep) {
                        float frequency = bin * static_cast<float>(frame.sampleRate) / FFT_SIZE;
                        
                        if (frequency >= minFrequency && frequency <= maxFrequency) {
                            float normalizedFreq = (frequency - minFrequency) / (maxFrequency - minFrequency);
                            float y = bounds.getHeight() - (normalizedFreq * bounds.getHeight());
                            
                            float magnitude = frame.magnitudes[bin];
                            Colour pixelColour = getSpectrogramColour(magnitude, frame.maxMagnitude);
                            
                            imageGraphics.setColour(pixelColour);
                            imageGraphics.fillRect(x, y - binStep, static_cast<float>(WATERFALL_HEIGHT), binStep * 2.0f);
                        }
                    }
                }
            }
            needsUpdate = false;
        }
        
        g.drawImageAt(spectrogramImage, (int)bounds.getX(), (int)bounds.getY());
    } else {
        // Show placeholder text when no data
        g.setColour(Colour(0x80ffffff));
        g.setFont(FontOptions(14.0f));
        g.drawText("🎵 Spectrogram - Play audio to see frequency content", bounds, Justification::centred);
    }
    
    // Draw frequency scale on the left
    g.setColour(Colour(0x80ffffff));
    g.setFont(FontOptions(10.0f));
    
    const float freqStep = 500.0f;  // 500Hz intervals
    for (float freq = minFrequency; freq <= maxFrequency; freq += freqStep) {
        float normalizedFreq = (freq - minFrequency) / (maxFrequency - minFrequency);
        float y = bounds.getBottom() - (normalizedFreq * bounds.getHeight());
        
        g.drawText(String(static_cast<int>(freq)) + "Hz", 
                  bounds.getX() + 2, static_cast<int>(y - 8), 50, 16, Justification::centredLeft);
        
        g.setColour(Colour(0x20ffffff));
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(bounds.getX() + 45), static_cast<float>(bounds.getRight()));
        g.setColour(Colour(0x80ffffff));
    }
    
    // Title and info
    g.setColour(Colour(0xffffffff));
    g.setFont(FontOptions(12.0f, Font::bold));
    g.drawText("Real-time Spectrogram", bounds.getX() + 5, bounds.getY() + 5, 200, 20, Justification::centredLeft);
}

void SpectrogramDisplay::addSpectrogramData(const float* audioBuffer, int bufferSize, double sampleRate)
{
    if (bufferSize < FFT_SIZE || !audioBuffer) return;
    
    performFFT(audioBuffer, bufferSize);
    
    // Calculate magnitudes and find peak
    std::vector<float> magnitudes(FFT_SIZE / 2);
    float maxMagnitude = 0.0f;
    
    for (int i = 0; i < FFT_SIZE / 2; ++i) {
        jassert(i * 2 + 1 < (int)fftBuffer.size());
        float real = fftBuffer[i * 2];
        float imag = fftBuffer[i * 2 + 1];
        float magnitude = std::sqrt(real * real + imag * imag);
        
        // Apply logarithmic scaling for better visualization
        magnitude = std::log10(1.0f + magnitude * 100.0f);
        
        magnitudes[i] = magnitude;
        maxMagnitude = jmax(maxMagnitude, magnitude);
    }
    
    // Add to history with actual sample rate
    spectrogramHistory.emplace_back(magnitudes, maxMagnitude, sampleRate);
    
    // Limit history size
    if (static_cast<int>(spectrogramHistory.size()) > maxHistoryFrames) {
        spectrogramHistory.erase(spectrogramHistory.begin());
    }
    
    repaint();
}

void SpectrogramDisplay::performFFT(const float* audioBuffer, int bufferSize)
{
    // Clear FFT buffer
    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
    
    // Copy and window audio data
    int samplesToCopy = jmin(bufferSize, FFT_SIZE);
    for (int i = 0; i < samplesToCopy; ++i) {
        jassert(i * 2 + 1 < (int)fftBuffer.size());
        fftBuffer[static_cast<size_t>(i * 2)] = audioBuffer[i] * windowBuffer[i];  // Real part
        fftBuffer[static_cast<size_t>(i * 2 + 1)] = 0.0f;                          // Imaginary part
    }
    
    // Perform FFT
    spectrogramFFT->perform(reinterpret_cast<juce::dsp::Complex<float>*>(fftBuffer.data()),
                           reinterpret_cast<juce::dsp::Complex<float>*>(fftBuffer.data()), false);
}

Colour SpectrogramDisplay::getSpectrogramColour(float magnitude, float maxMag)
{
    if (maxMag < 0.001f) return Colour(0x00000000);  // Transparent for silence
    
    // Normalize magnitude
    float normalized = jlimit(0.0f, 1.0f, magnitude / (maxMag + 0.001f));
    
    // Create heat map colors: black -> blue -> green -> yellow -> red
    if (normalized < 0.2f) {
        // Black to blue
        float t = normalized / 0.2f;
        return Colour::fromFloatRGBA(0.0f, 0.0f, t * 0.8f, 0.8f);
    } else if (normalized < 0.4f) {
        // Blue to cyan
        float t = (normalized - 0.2f) / 0.2f;
        return Colour::fromFloatRGBA(0.0f, t * 0.8f, 0.8f, 0.9f);
    } else if (normalized < 0.6f) {
        // Cyan to green
        float t = (normalized - 0.4f) / 0.2f;
        return Colour::fromFloatRGBA(0.0f, 0.8f, 0.8f * (1.0f - t), 0.95f);
    } else if (normalized < 0.8f) {
        // Green to yellow
        float t = (normalized - 0.6f) / 0.2f;
        return Colour::fromFloatRGBA(t * 0.9f, 0.8f, 0.0f, 0.98f);
    } else {
        // Yellow to red
        float t = (normalized - 0.8f) / 0.2f;
        return Colour::fromFloatRGBA(0.9f + t * 0.1f, 0.8f * (1.0f - t), 0.0f, 1.0f);
    }
}

float SpectrogramDisplay::frequencyToBin(float frequency, double sampleRate, int fftSize)
{
    return frequency * fftSize / sampleRate;
}

void SpectrogramDisplay::clearHistory()
{
    spectrogramHistory.clear();
    repaint();
}

void SpectrogramDisplay::timerCallback()
{
    // Auto-refresh for smooth animation
    repaint();
}

void SpectrogramDisplay::resized()
{
    // Handle component resizing
}

AutotuneControls::AutotuneControls()
{
    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Autotune Controls", dontSendNotification);
    titleLabel.setFont(FontOptions(20.0f, Font::bold));
    titleLabel.setJustificationType(Justification::centred);
    titleLabel.setColour(Label::textColourId, Colours::white);

    // Autotune ON/OFF button
    addAndMakeVisible(autotuneButton);
    autotuneButton.setButtonText("Autotune OFF");
    autotuneButton.setColour(TextButton::buttonColourId, Colour(0xff804040));

    // Sliders
    addAndMakeVisible(strengthSlider);
    strengthSlider.setRange(0.0, 1.0, 0.01);
    strengthSlider.setValue(0.8);
    strengthSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

    addAndMakeVisible(speedSlider);
    speedSlider.setRange(0.0, 1.0, 0.01);
    speedSlider.setValue(0.5);
    speedSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

    addAndMakeVisible(mixSlider);
    mixSlider.setRange(0.0, 1.0, 0.01);
    mixSlider.setValue(1.0);
    mixSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

    // Labels
    addAndMakeVisible(strengthLabel);
    strengthLabel.setText("Strength", dontSendNotification);
    strengthLabel.setJustificationType(Justification::centred);

    addAndMakeVisible(speedLabel);
    speedLabel.setText("Speed", dontSendNotification);
    speedLabel.setJustificationType(Justification::centred);

    addAndMakeVisible(mixLabel);
    mixLabel.setText("Mix", dontSendNotification);
    mixLabel.setJustificationType(Justification::centred);
}

AutotuneControls::~AutotuneControls() {}

void AutotuneControls::paint(Graphics& g)
{
    g.fillAll(Colour(0xff202020));
}

void AutotuneControls::resized()
{
    auto area = getLocalBounds().reduced(10);

    // Title and button at top
    titleLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);
    autotuneButton.setBounds(area.removeFromTop(36));
    area.removeFromTop(6);

    const int width = area.getWidth();
    if (width > 520) {
        // Wide: sliders in one row, labels in a second row using FlexBox
        auto slidersRow = area.removeFromTop(100);
        juce::FlexBox fb1;
        fb1.flexDirection = juce::FlexBox::Direction::row;
        fb1.alignItems = juce::FlexBox::AlignItems::center;
        fb1.items.add(juce::FlexItem(strengthSlider).withFlex(1.0f).withMinWidth(120.0f).withMargin({6,6,6,6}));
        fb1.items.add(juce::FlexItem(speedSlider)  .withFlex(1.0f).withMinWidth(120.0f).withMargin({6,6,6,6}));
        fb1.items.add(juce::FlexItem(mixSlider)    .withFlex(1.0f).withMinWidth(120.0f).withMargin({6,6,6,6}));
        fb1.performLayout(slidersRow.toFloat());

        auto labelsRow = area.removeFromTop(22);
        juce::FlexBox fb2;
        fb2.flexDirection = juce::FlexBox::Direction::row;
        fb2.alignItems = juce::FlexBox::AlignItems::center;
        fb2.items.add(juce::FlexItem(strengthLabel).withFlex(1.0f).withMinWidth(120.0f).withMargin({2,0,2,6}));
        fb2.items.add(juce::FlexItem(speedLabel)  .withFlex(1.0f).withMinWidth(120.0f).withMargin({2,0,2,6}));
        fb2.items.add(juce::FlexItem(mixLabel)    .withFlex(1.0f).withMinWidth(120.0f).withMargin({2,0,2,6}));
        fb2.performLayout(labelsRow.toFloat());
    } else if (width > 360) {
        // Medium: sliders row only; place labels beneath each slider cell
        auto slidersRow = area.removeFromTop(100);
        juce::FlexBox fb;
        fb.flexDirection = juce::FlexBox::Direction::row;
        fb.alignItems = juce::FlexBox::AlignItems::center;

        // Compute rects first, then place labels under them
        juce::Array<juce::Rectangle<float>> rects;
        auto temp = slidersRow.toFloat();
        // Use three equal cells
        auto cellW = temp.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i) {
            rects.add({ temp.getX() + i * cellW, temp.getY(), cellW, temp.getHeight() });
        }
        strengthSlider.setBounds(rects[0].reduced(10.0f).toNearestInt());
        speedSlider.setBounds(rects[1].reduced(10.0f).toNearestInt());
        mixSlider.setBounds(rects[2].reduced(10.0f).toNearestInt());

        auto labelsRow = area.removeFromTop(20);
        auto labelsFloat = labelsRow.toFloat();
        for (int i = 0; i < 3; ++i) {
            auto lr = juce::Rectangle<float>(labelsFloat.getX() + i * cellW, labelsFloat.getY(), cellW, labelsFloat.getHeight());
            switch (i) {
                case 0: strengthLabel.setBounds(lr.toNearestInt()); break;
                case 1: speedLabel.setBounds(lr.toNearestInt()); break;
                case 2: mixLabel.setBounds(lr.toNearestInt()); break;
            }
        }
    } else {
        // Narrow: stack each as label + slider column
        for (auto& pair : std::array<std::pair<Label*, Slider*>, 3>{
                 std::make_pair(&strengthLabel, &strengthSlider),
                 std::make_pair(&speedLabel, &speedSlider),
                 std::make_pair(&mixLabel, &mixSlider) })
        {
            auto row = area.removeFromTop(70);
            auto labelRow = row.removeFromTop(18);
            pair.first->setBounds(labelRow);
            row.removeFromTop(2);
            pair.second->setBounds(row.reduced(6));
            area.removeFromTop(6);
        }
    }
}

void AutotuneControls::setProcessor(AudioProcessor* proc)
{
    processor = proc;

    // Bind to processor parameters if available
    if (auto* pitchProc = dynamic_cast<class PitchDetectorProcessor*>(processor)) {
        auto& vts = pitchProc->getValueTreeState();

        // Ensure button toggles and reflects state
        autotuneButton.setClickingTogglesState(true);
        autotuneEnableAttachment.reset();
        strengthAttachment.reset();
        speedAttachment.reset();
        mixAttachment.reset();

        autotuneEnableAttachment = std::make_unique<ButtonAttachment>(vts, "autotuneEnabled", autotuneButton);
        strengthAttachment       = std::make_unique<SliderAttachment>(vts, "correctionStrength", strengthSlider);
        speedAttachment          = std::make_unique<SliderAttachment>(vts, "correctionSpeed",    speedSlider);
        mixAttachment            = std::make_unique<SliderAttachment>(vts, "mixAmount",          mixSlider);

        auto updateButtonVisuals = [this]
        {
            const bool on = autotuneButton.getToggleState();
            autotuneButton.setButtonText(on ? "Autotune ON" : "Autotune OFF");
            autotuneButton.setColour(TextButton::buttonColourId, on ? Colour(0xff00aa00) : Colour(0xff804040));
        };
        autotuneButton.onClick = updateButtonVisuals;
        autotuneButton.onStateChange = updateButtonVisuals;
        updateButtonVisuals();
    }
}

//==============================================================================

PitchDetectorGUI::PitchDetectorGUI() : engine()
{
    setOpaque(true);
    
    // Set minimum size first to prevent assertion failures
    setSize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    
    // Initialize components after size is set
    initializeComponents();
    
    // Setup audio processing
    setupStandaloneAudio();
    
    // Start GUI refresh timer
    startTimerHz(PitchDetectorConstants::GUI_UPDATE_RATE_HZ);
}

PitchDetectorGUI::~PitchDetectorGUI()
{
    stopTimer();
}

void PitchDetectorGUI::initializeComponents()
{
    addAndMakeVisible(autoTuneDisplay);
    addAndMakeVisible(pianoRoll);
    addAndMakeVisible(spectrogram);
    addAndMakeVisible(trackingStrip);
    
    // Enhanced note name display with professional styling
    addAndMakeVisible(noteNameLabel);
    noteNameLabel.setFont(FontOptions(42.0f, Font::bold));
    noteNameLabel.setJustificationType(Justification::centred);
    noteNameLabel.setText("--", dontSendNotification);
    noteNameLabel.setColour(Label::textColourId, Colour(0xffffffff));
    
    // Professional frequency display with neon styling
    addAndMakeVisible(frequencyLabel);
    frequencyLabel.setFont(FontOptions(16.0f, Font::bold));
    frequencyLabel.setJustificationType(Justification::centred);
    frequencyLabel.setText("-- Hz", dontSendNotification);
    frequencyLabel.setColour(Label::textColourId, Colour(0xff00ffaa));
    
    // Enhanced cents display with dynamic coloring
    addAndMakeVisible(centsLabel);
    centsLabel.setFont(FontOptions(16.0f, Font::bold));
    centsLabel.setJustificationType(Justification::centred);
    centsLabel.setText("-- cents", dontSendNotification);
    centsLabel.setColour(Label::textColourId, Colour(0xffffff80));
    
    // Professional audio level indicator
    addAndMakeVisible(audioLevelLabel);
    audioLevelLabel.setFont(FontOptions(14.0f));
    audioLevelLabel.setJustificationType(Justification::centred);
    audioLevelLabel.setText("Level: --", dontSendNotification);
    audioLevelLabel.setColour(Label::textColourId, Colour(0xff80c8ff));
}

void PitchDetectorGUI::paint(Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    [[maybe_unused]] auto centre = bounds.getCentre();
    
    // Enhanced dark background with subtle texture
    g.fillAll(Colour(0xff0f0f0f));
    
    // Create depth with multiple gradient layers
    g.setGradientFill(ColourGradient(
        Colour(0xff1a1a2e), 0, 0,
        Colour(0xff16213e), bounds.getWidth(), bounds.getHeight(), false));
    g.fillAll();
    
    // Professional glassmorphism main panel
    auto glassArea = bounds.reduced(12);
    
    // Multi-layer glass effect with depth
    for (int i = 0; i < 3; ++i) {
        float reduction = i * 2.0f;
        float alpha = 0.15f - i * 0.03f;
        g.setColour(Colour(0xffffffff).withAlpha(alpha));
        g.fillRoundedRectangle(glassArea.reduced(reduction), 18.0f - i);
    }
    
    // Frosted glass background with noise texture simulation
    g.setColour(Colour(0x25ffffff));
    g.fillRoundedRectangle(glassArea, 18.0f);
    
    // Enhanced glass borders with gradient highlights
    g.setGradientFill(ColourGradient(
        Colour(0x80ffffff), glassArea.getX(), glassArea.getY(),
        Colour(0x20ffffff), glassArea.getRight(), glassArea.getBottom(), false));
    g.drawRoundedRectangle(glassArea, 18.0f, 1.5f);
    
    // Top highlight for enhanced glass effect
    auto topHighlight = glassArea.removeFromTop(2).reduced(20, 0);
    g.setColour(Colour(0x60ffffff));
    g.fillRoundedRectangle(topHighlight, 1.0f);
    
    // Subtle inner shadow for depth
    g.setColour(Colour(0x15000000));
    g.drawRoundedRectangle(glassArea.reduced(1), 17.0f, 1.0f);
    
    // Add subtle animated glow effects around the panel edges
    static float glowPhase = 0.0f;
    glowPhase += 0.02f;
    if (glowPhase > MathConstants<float>::twoPi) glowPhase -= MathConstants<float>::twoPi;
    
    float glowIntensity = 0.3f + 0.1f * std::sin(glowPhase);
    g.setColour(NEON_ACCENT.withAlpha(glowIntensity * 0.3f));
    g.drawRoundedRectangle(glassArea.expanded(2), 20.0f, 2.0f);
    
    repaint(); // Keep the glow animation running
}

void PitchDetectorGUI::resized()
{
    auto bounds = getLocalBounds();
    if (bounds.getWidth() < 400 || bounds.getHeight() < 300) {
        // Hide all components if window is too small
        setVisible(false);
        return;
    }
    
    setVisible(true);
    auto area = bounds.reduced(10);
    
    // Clean layout: Top section for main display (40% of height)
    auto displayHeight = (int)(area.getHeight() * 0.4f);
    auto topSection = area.removeFromTop(displayHeight);
    
    // Split display: Left side for tuner (30%), right for info (70%)
    auto tunerWidth = (int)(topSection.getWidth() * 0.3f);
    auto tunerArea = topSection.removeFromLeft(tunerWidth);
    topSection.removeFromLeft(5); // Small gap
    auto infoArea = topSection;
    
    // Set Auto-Tune display bounds (full tuner area)
    if (tunerArea.getWidth() > 200 && tunerArea.getHeight() > 150) {
        autoTuneDisplay.setBounds(tunerArea.reduced(5));
        autoTuneDisplay.setVisible(true);
    } else {
        autoTuneDisplay.setVisible(false);
    }
    
    // Info labels - stack vertically with proper spacing
    auto labelHeight = infoArea.getHeight() / 4;
    
    noteNameLabel.setBounds(infoArea.removeFromTop(labelHeight).reduced(5));
    frequencyLabel.setBounds(infoArea.removeFromTop(labelHeight).reduced(5));  
    centsLabel.setBounds(infoArea.removeFromTop(labelHeight).reduced(5));
    audioLevelLabel.setBounds(infoArea.reduced(5));
    
    area.removeFromTop(10); // Spacing
    
    // Middle section: Tracking strip (15% of height)
    auto trackingHeight = (int)(bounds.getHeight() * 0.15f);
    auto trackingSection = area.removeFromTop(trackingHeight);
    trackingStrip.setBounds(trackingSection);
    trackingStrip.setVisible(trackingSection.getHeight() > 20);
    
    area.removeFromTop(5);
    
    // Bottom sections: Split remaining 45% between piano roll and spectrogram
    auto remainingHeight = area.getHeight();
    auto pianoHeight = remainingHeight / 2;
    
    auto pianoSection = area.removeFromTop(pianoHeight);
    pianoRoll.setBounds(pianoSection);
    pianoRoll.setVisible(pianoSection.getHeight() > 30);
    
    area.removeFromTop(5);
    
    // Remaining space for spectrogram
    if (area.getHeight() > 30) {
        spectrogram.setBounds(area);
        spectrogram.setVisible(true);
    } else {
        spectrogram.setVisible(false);
    }
    
    // Force repaint after layout
    repaint();
}

void PitchDetectorGUI::updatePitchDisplay(float frequency, const NoteInfo& noteInfo)
{
    autoTuneDisplay.updatePitch(frequency, noteInfo);
    
    if (noteInfo.isValid) {
        // Dynamic note name styling based on pitch accuracy
        char noteText[16]; 
    snprintf(noteText, sizeof(noteText), "%s%d", noteInfo.noteName.toRawUTF8(), noteInfo.octave);
        noteNameLabel.setText(noteText, dontSendNotification);
        
        // Color-code the note name based on accuracy
        const float perfectThreshold = 8.0f;
        const float goodThreshold = 20.0f;
        
        if (std::abs(noteInfo.centsDeviation) < perfectThreshold) {
            noteNameLabel.setColour(Label::textColourId, Colour(0xff00ff80)); // Bright green for perfect
        } else if (std::abs(noteInfo.centsDeviation) < goodThreshold) {
            noteNameLabel.setColour(Label::textColourId, Colour(0xffffff80)); // Yellow for close
        } else {
            noteNameLabel.setColour(Label::textColourId, Colour(0xffff6060)); // Red for off-pitch
        }
        
        // Enhanced frequency display with precision indicator
        char freqText[32]; 
    snprintf(freqText, sizeof(freqText), "%.1f Hz", frequency);
        frequencyLabel.setText(freqText, dontSendNotification);
        frequencyLabel.setColour(Label::textColourId, Colour(0xff00ffcc));
        
        // Dynamic cents display with directional indicators
        String centsText = String(noteInfo.centsDeviation, 0) + " cents";
        if (std::abs(noteInfo.centsDeviation) > 30.0f) {
            centsText += (noteInfo.centsDeviation > 0 ? " ↗" : " ↙"); // Directional arrows for large deviations
        }
        centsLabel.setText(centsText, dontSendNotification);
        
        // Color-code cents based on deviation
        if (std::abs(noteInfo.centsDeviation) < perfectThreshold) {
            centsLabel.setColour(Label::textColourId, Colour(0xff00ff80)); // Green
        } else if (std::abs(noteInfo.centsDeviation) < goodThreshold) {
            centsLabel.setColour(Label::textColourId, Colour(0xffffff80)); // Yellow
        } else {
            centsLabel.setColour(Label::textColourId, Colour(0xffff8060)); // Orange/red
        }
        
        // Convert frequency to MIDI note and update piano roll
        int midiNote = static_cast<int>(std::round(69.0f + 12.0f * std::log2(frequency / 440.0f)));
        // Update piano roll using its internal timebase
        float currentTime = pianoRoll.getCurrentTime();
        
        // Check if we need to start a new note (simplified logic)
        static int lastMidiNote = -1;
        if (lastMidiNote != midiNote) {
            // Start new note
            pianoRoll.addNote(midiNote, currentTime, 0.8f, noteInfo.centsDeviation);
            lastMidiNote = midiNote;
        } else {
            // Update current note
            pianoRoll.updateCurrentNote(midiNote, noteInfo.centsDeviation);
        }
        // Feed tracking strip with current cents deviation
        trackingStrip.addCents(noteInfo.centsDeviation);
        
    } else {
        // No signal state with dimmed styling
        noteNameLabel.setText("--", dontSendNotification);
        noteNameLabel.setColour(Label::textColourId, Colour(0x60ffffff));
        
        frequencyLabel.setText("-- Hz", dontSendNotification);
        frequencyLabel.setColour(Label::textColourId, Colour(0x60ffffff));
        
        centsLabel.setText("-- cents", dontSendNotification);
        centsLabel.setColour(Label::textColourId, Colour(0x60ffffff));
        
        // End current note if no signal
        pianoRoll.endCurrentNote();
        // Keep strip centered when no valid pitch
        trackingStrip.addCents(0.0f);
    }
}

void PitchDetectorGUI::updateAudioLevel(float level)
{
    // Enhanced audio level display with visual meter representation
    String levelText = "Level: " + String(level, 2);
    
    // Add visual level meter using Unicode blocks
    int meterBars = (int)(level * 10.0f);
    meterBars = jlimit(0, 10, meterBars);
    
    String meterDisplay = " [";
    for (int i = 0; i < 10; ++i) {
        if (i < meterBars) {
            if (i < 6) meterDisplay += "▓";      // Normal level
            else if (i < 8) meterDisplay += "▓"; // Getting hot
            else meterDisplay += "▓";            // Hot level
        } else {
            meterDisplay += "░";                 // Empty
        }
    }
    meterDisplay += "]";
    
    audioLevelLabel.setText(levelText + meterDisplay, dontSendNotification);
    
    // Dynamic color based on audio level
    if (level < 0.1f) {
        audioLevelLabel.setColour(Label::textColourId, Colour(0x60808080)); // Dim for very low
    } else if (level < 0.5f) {
        audioLevelLabel.setColour(Label::textColourId, Colour(0xff60c0ff)); // Blue for normal
    } else if (level < 0.8f) {
        audioLevelLabel.setColour(Label::textColourId, Colour(0xff80ff60)); // Green for good
    } else if (level < 0.95f) {
        audioLevelLabel.setColour(Label::textColourId, Colour(0xffffff80)); // Yellow for hot
    } else {
        audioLevelLabel.setColour(Label::textColourId, Colour(0xffff6060)); // Red for clipping danger
    }
}

void PitchDetectorGUI::timerCallback()
{
    // Process all available FIFO data for responsive updates
    int readIndex = fifoReadIndex.load(std::memory_order_acquire);
    int writeIndex = fifoWriteIndex.load(std::memory_order_acquire);
    
    bool hasUpdates = false;
    float latestFrequency = 0.0f;
    float latestLevel = 0.0f;
    
    // Process all pending data (up to fifoSize to prevent infinite loop)
    int processed = 0;
    while (readIndex != writeIndex && processed < fifoSize) {
        latestFrequency = pitchFifo[readIndex];
        latestLevel = levelFifo[readIndex];
        hasUpdates = true;
        
        readIndex = (readIndex + 1) % fifoSize;
        processed++;
    }
    
    // Update read index after processing
    if (hasUpdates) {
        fifoReadIndex.store(readIndex, std::memory_order_release);
        
        // Convert frequency to note and update displays
        NoteInfo noteInfo = engine.frequencyToNote(latestFrequency);
        updatePitchDisplay(latestFrequency, noteInfo);
        updateAudioLevel(latestLevel);
        
        // Add to tracking strip for history visualization
        if (noteInfo.isValid) {
            trackingStrip.addCents(noteInfo.centsDeviation);
        }
    }
    
    // Keep animation running
    repaint();
}

void PitchDetectorGUI::setupStandaloneAudio() 
{
    // This method is called from standalone app only - no-op for plugin mode
    // Audio setup is handled by StandalonePitchDetector component
}
void PitchDetectorGUI::shutdownStandaloneAudio() {}

//==============================================================================
// AUDIO PROCESSOR IMPLEMENTATION
//==============================================================================

PitchDetectorProcessor::PitchDetectorProcessor()
    : AudioProcessor(BusesProperties()
                    .withInput("Input", AudioChannelSet::mono(), true)
                    .withOutput("Output", AudioChannelSet::mono(), true)),
      parameterTreeState(*this, nullptr, Identifier("Parameters"), createParameterLayout())
{
    std::fill(fifo, fifo + fifoSize, 0.0f);
    std::fill(processingBuffer, processingBuffer + fifoSize, 0.0f);
    
    autotuneEngine = std::make_unique<AutotuneEngine>();
    pitchBuffer.assign(fifoSize, 0.0f);
    
    parameterTreeState.addParameterListener("correctionStrength", this);
    parameterTreeState.addParameterListener("correctionSpeed", this);
    parameterTreeState.addParameterListener("mixAmount", this);
    parameterTreeState.addParameterListener("rootNote", this);
}

PitchDetectorProcessor::~PitchDetectorProcessor() {}

AudioProcessorValueTreeState::ParameterLayout PitchDetectorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> parameters;

    parameters.emplace_back(std::make_unique<AudioParameterBool>(
        "autotuneEnabled", "Autotune Enabled", true));

    parameters.emplace_back(std::make_unique<AudioParameterFloat>(
        "correctionStrength", "Correction Strength", 
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), PitchDetectorConstants::DEFAULT_CORRECTION_STRENGTH));
    
    parameters.emplace_back(std::make_unique<AudioParameterFloat>(
        "correctionSpeed", "Correction Speed", 
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), PitchDetectorConstants::DEFAULT_CORRECTION_SPEED));
    
    parameters.emplace_back(std::make_unique<AudioParameterFloat>(
        "mixAmount", "Mix Amount", 
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), PitchDetectorConstants::DEFAULT_MIX_AMOUNT));
    
    parameters.emplace_back(std::make_unique<AudioParameterInt>(
        "rootNote", "Root Note", 0, PitchDetectorConstants::SEMITONES_PER_OCTAVE - 1, 0));

    return { parameters.begin(), parameters.end() };
}

void PitchDetectorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (autotuneEngine)
        autotuneEngine->prepareToPlay(sampleRate, samplesPerBlock);
}

void PitchDetectorProcessor::releaseResources() {}

void PitchDetectorProcessor::processBlock(AudioBuffer<float>& buffer, [[maybe_unused]] MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (totalNumInputChannels > 0) {
        const float* inputData = buffer.getReadPointer(0);
        int numSamples = buffer.getNumSamples();
        
        pushSamplesToFifo(inputData, numSamples);
        
        if (nextBlockReady.load(std::memory_order_acquire)) {
            processAudioBlock();
            nextBlockReady.store(false, std::memory_order_release);
        }
        
        if (autotuneEnabled && autotuneEngine) {
            autotuneEngine->processBlock(buffer, pitchBuffer.data(), numSamples);
        }
    }
}

void PitchDetectorProcessor::pushSamplesToFifo(const float* samples, int numSamples)
{
    auto currentIndex = fifoIndex.load(std::memory_order_relaxed);
    
    for (int i = 0; i < numSamples; ++i) {
        fifo[currentIndex] = samples[i];
        currentIndex = (currentIndex + 1) % fifoSize;
        
        if (currentIndex == 0)
            nextBlockReady.store(true, std::memory_order_release);
    }
    
    fifoIndex.store(currentIndex, std::memory_order_release);
}

void PitchDetectorProcessor::processAudioBlock()
{
    // Simple pitch detection on FIFO buffer using member engine
    float frequency = pitchEngine.detectPitch(fifo, fifoSize, getSampleRate());
    
    // Store for autotune
    std::fill(pitchBuffer.begin(), pitchBuffer.end(), frequency);
    
    // Store result for GUI thread to pick up (thread-safe communication)
    {
        const ScopedLock lock(resultLock);
        latestResult.frequency = frequency;
        latestResult.noteInfo = pitchEngine.frequencyToNote(frequency);
        latestResult.hasNewData = true;
    }
}

void PitchDetectorProcessor::parameterChanged(const String& parameterID, float newValue)
{
    if (parameterID == "autotuneEnabled")
        autotuneEnabled = (newValue >= 0.5f);
    else if (parameterID == "correctionStrength" && autotuneEngine)
        autotuneEngine->setCorrectionStrength(newValue);
    else if (parameterID == "correctionSpeed" && autotuneEngine)
        autotuneEngine->setCorrectionSpeed(newValue);
    else if (parameterID == "mixAmount" && autotuneEngine)
        autotuneEngine->setMixAmount(newValue);
    else if (parameterID == "rootNote" && autotuneEngine)
        autotuneEngine->setRootNote((int)newValue);
}

AudioProcessorEditor* PitchDetectorProcessor::createEditor()
{
    // Factory method - not in audio thread, but use smart pointer for safety
    return new PitchDetectorEditor(*this);
}

void PitchDetectorProcessor::getStateInformation(MemoryBlock& destData)
{
    auto state = parameterTreeState.copyState();
    std::unique_ptr<XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PitchDetectorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameterTreeState.state.getType()))
            parameterTreeState.replaceState(ValueTree::fromXml(*xmlState));
}

void PitchDetectorProcessor::startStopRecording() {}
void PitchDetectorProcessor::saveRecording() {}

//==============================================================================
// PLUGIN EDITOR IMPLEMENTATION
//==============================================================================

PitchDetectorEditor::PitchDetectorEditor(PitchDetectorProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // Main visual tuner GUI
    addAndMakeVisible(gui);

    // Autotune controls panel
    addAndMakeVisible(autotuneControls);
    autotuneControls.setProcessor(&p);

    // Recording UI
    addAndMakeVisible(recordButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(exportCsvButton);
    recordButton.setButtonText("Record");
    stopButton.setButtonText("Stop");
    exportCsvButton.setButtonText("Export CSV");

    recordButton.onClick = [this]
    {
        gui.clearPianoRollHistory();
        gui.startPianoRollRecording();
        recordButton.setEnabled(false);
        stopButton.setEnabled(true);
    };

    stopButton.onClick = [this]
    {
        gui.stopPianoRollRecording();
        recordButton.setEnabled(true);
        stopButton.setEnabled(false);
    };

    exportCsvButton.onClick = [this]
    {
        if (gui.getPianoRollRecording().empty())
            return;

        FileChooser chooser("Export pitch notes to CSV", File::getSpecialLocation(File::userDocumentsDirectory), "*.csv");
        chooser.launchAsync(FileBrowserComponent::saveMode, [this](const FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile() || f.getParentDirectory().exists())
                gui.exportPianoRollToCSV(f);
        });
    };

    stopButton.setEnabled(false);

    // Default size closer to plugin doc
    setSize(800, 550);

    // Connect GUI to processor for realtime display
    audioProcessor.gui = &gui;
}

PitchDetectorEditor::~PitchDetectorEditor()
{
    audioProcessor.gui = nullptr;
}

void PitchDetectorEditor::paint([[maybe_unused]] Graphics& g) {}

void PitchDetectorEditor::resized()
{
    auto area = getLocalBounds().reduced(10);

    // Flex layout: side by side on wide, stacked on narrow
    const int w = area.getWidth();
    // Top row: recording controls
    auto controlsBar = area.removeFromTop(34);
    auto b = controlsBar.toFloat();
    // Simple left-to-right placement
    auto placeBtn = [&b](TextButton& btn, float width)
    {
        auto r = Rectangle<float>(b.getX(), b.getY(), width, b.getHeight());
        btn.setBounds(r.toNearestInt());
        b.setX(b.getX() + width + 6.0f);
    };
    placeBtn(recordButton, 90.0f);
    placeBtn(stopButton, 80.0f);
    placeBtn(exportCsvButton, 110.0f);
    area.removeFromTop(8);

    if (w >= 900) {
        // Wide: GUI left, controls right
        auto left = area.removeFromLeft(jmax(520, w - 320));
        gui.setBounds(left);
        area.removeFromLeft(10);
        autotuneControls.setBounds(area);
    } else if (w >= 640) {
        // Medium: 60/40 split
        auto left = area.removeFromLeft((int)(area.getWidth() * 0.6f));
        gui.setBounds(left);
        area.removeFromLeft(8);
        autotuneControls.setBounds(area);
    } else {
        // Narrow: stack
        auto top = area.removeFromTop(jmax(280, area.getHeight() / 2));
        gui.setBounds(top);
        area.removeFromTop(8);
        autotuneControls.setBounds(area);
    }
}

//==============================================================================
// STANDALONE APP IMPLEMENTATION
//==============================================================================

StandalonePitchDetector::StandalonePitchDetector()
{
    setOpaque(true);
    
    auto envFlag = SystemStats::getEnvironmentVariable("PD_DISABLE_AUDIO", {});
    disableAudio = envFlag.equalsIgnoreCase("1") || envFlag.equalsIgnoreCase("true");
    
    std::fill(fifo, fifo + fifoSize, 0.0f);
    std::fill(processingBuffer, processingBuffer + fifoSize, 0.0f);
    
    addAndMakeVisible(gui);
    
    gui.onExportTelemetry = [this] { exportTelemetryData(); };
    gui.onResetTelemetry = [this] { resetTelemetryData(); };
    
    addAndMakeVisible(permissionStatusLabel);
    permissionStatusLabel.setFont(FontOptions(14.0f, Font::bold));
    permissionStatusLabel.setJustificationType(Justification::centred);
    
    addAndMakeVisible(permissionButton);
    addAndMakeVisible(audioSettingsButton);
    
    if (!disableAudio) {
        setAudioChannels(1, 0);
        startTimerHz(PitchDetectorConstants::STANDALONE_TIMER_HZ);
    }
    
    setSize(PitchDetectorConstants::DEFAULT_WINDOW_WIDTH + 100, PitchDetectorConstants::DEFAULT_WINDOW_HEIGHT + 100);
}

StandalonePitchDetector::~StandalonePitchDetector()
{
    stopTimer();
    shutdownAudio();
}

void StandalonePitchDetector::prepareToPlay([[maybe_unused]] int samplesPerBlockExpected, [[maybe_unused]] double sampleRate)
{
    engine.initializeTelemetry();
    audioSetupFailed = false;
}

void StandalonePitchDetector::getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer->getNumChannels() > 0) {
        const float* inputData = bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample);
        pushSamplesToFifo(inputData, bufferToFill.numSamples);
        
        if (nextBlockReady) {
            processAudioBlock();
            nextBlockReady = false;
        }
    }
    
    bufferToFill.clearActiveBufferRegion();
}

void StandalonePitchDetector::releaseResources() {}

void StandalonePitchDetector::pushSamplesToFifo(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i) {
        fifo[fifoIndex] = samples[i];
        fifoIndex = (fifoIndex + 1) % fifoSize;
        
        if (fifoIndex == 0)
            nextBlockReady = true;
    }
}

void StandalonePitchDetector::processAudioBlock()
{
    double actualSampleRate = deviceManager.getCurrentAudioDevice() ? 
                              deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 
                              PitchDetectorConstants::DEFAULT_SAMPLE_RATE;
    float frequency = engine.detectPitch(fifo, fifoSize, actualSampleRate);
    
    NoteInfo noteInfo = engine.frequencyToNote(frequency);
    
    // Update GUI via FIFO
    int writeIndex = gui.fifoWriteIndex.load(std::memory_order_acquire);
    gui.pitchFifo[writeIndex] = frequency;
    gui.levelFifo[writeIndex] = 0.5f; // Simplified
    gui.fifoWriteIndex = (writeIndex + 1) % gui.fifoSize;
    
    // Update spectrogram with audio data
    gui.spectrogram.addSpectrogramData(fifo, fifoSize, actualSampleRate);
}

void StandalonePitchDetector::timerCallback() {}

void StandalonePitchDetector::paint(Graphics& g)
{
    g.fillAll(Colour(0xff202020));
}

void StandalonePitchDetector::resized()
{
    auto area = getLocalBounds();
    
    if (!disableAudio && audioSetupFailed) {
        permissionStatusLabel.setBounds(area.removeFromTop(40).reduced(10));
        permissionButton.setBounds(area.removeFromTop(40).reduced(10));
        audioSettingsButton.setBounds(area.removeFromTop(40).reduced(10));
        area.removeFromTop(10);
    }
    
    gui.setBounds(area);
}

void StandalonePitchDetector::initializePermissions() {}
void StandalonePitchDetector::checkPermissionStatus() {}
void StandalonePitchDetector::updatePermissionUI() {}
void StandalonePitchDetector::requestMicrophonePermission() {}
void StandalonePitchDetector::setupAudioWithPermission() {}
void StandalonePitchDetector::exportTelemetryData() {}
void StandalonePitchDetector::resetTelemetryData() {}
void StandalonePitchDetector::showAudioSettings() {}

// Create plugin instance - JUCE framework factory method (not audio thread)
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchDetectorProcessor();
}
