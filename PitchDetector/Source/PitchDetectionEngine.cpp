#include "PitchDetectionEngine.h"
#include <algorithm>
#include <cmath>

//==============================================================================
PitchDetectionEngine::PitchDetectionEngine()
{
    initializeTelemetry();
}

//==============================================================================
float PitchDetectionEngine::detectPitch(const float* buffer, int size, double sampleRate)
{
    // Preprocessing
    std::vector<float> processedBuffer(size);
    preprocess(buffer, processedBuffer.data(), size);
    
    float frequency = 0.0f;
    
    // Use selected detection method
    switch (currentMethod) {
        case Autocorrelation:
            frequency = autocorrelationPitchDetection(processedBuffer.data(), size, sampleRate);
            break;
        case YIN:
            frequency = detectPitchYin(processedBuffer.data(), size, sampleRate);
            break;
        case HPS:
            frequency = detectPitchHPS(processedBuffer.data(), size, sampleRate);
            break;
        case Cepstrum:
            frequency = detectPitchCepstrum(processedBuffer.data(), size, sampleRate);
            break;
        case Hybrid:
        default:
            // Use multiple algorithms and choose most confident result
            float autocorrFreq = autocorrelationPitchDetection(processedBuffer.data(), size, sampleRate);
            float yinFreq = detectPitchYin(processedBuffer.data(), size, sampleRate);
            
            // Simple voting - prefer YIN for low frequencies, autocorrelation for higher
            if (autocorrFreq > 0 && yinFreq > 0) {
                // If both agree within 5%, use their average
                if (std::abs(autocorrFreq - yinFreq) / std::max(autocorrFreq, yinFreq) < 0.05f) {
                    frequency = (autocorrFreq + yinFreq) * 0.5f;
                } else if (autocorrFreq < 200.0f) {
                    frequency = yinFreq; // YIN better for low frequencies
                } else {
                    frequency = autocorrFreq; // Autocorrelation better for higher frequencies
                }
            } else {
                frequency = std::max(autocorrFreq, yinFreq); // Use whichever found something
            }
            break;
    }
    
    // Apply stability filtering
    return applyStabilityFilter(frequency);
}

//==============================================================================
float PitchDetectionEngine::autocorrelationPitchDetection(const float* buffer, int size, double sampleRate)
{
    auto startTime = Time::getHighResolutionTicks();
    
    std::vector<float> autocorr(static_cast<size_t>(size / 2));
    
    // Calculate RMS for noise gate
    float rms = 0.0f;
    for (int i = 0; i < size; ++i) {
        rms += buffer[i] * buffer[i];
    }
    rms = std::sqrt(rms / size);
    
    telemetry.totalDetectionAttempts++;
    
    // Dynamic noise gate threshold
    if (rms < noiseThreshold) {
        failedDetections++;
        return 0.0f;
    }
    
    // Calculate autocorrelation
    for (int lag = 1; lag < size / 2; ++lag) {
        float sum = 0.0f;
        for (int i = 0; i < size - lag; ++i) {
            sum += buffer[i] * buffer[i + lag];
        }
        autocorr[static_cast<size_t>(lag)] = sum;
    }
    
    // Find peak using dynamic frequency range
    int maxLag = 0;
    float maxVal = 0.0f;
    int minPitch = (int)(sampleRate / maxFrequency);
    int maxPitch = (int)(sampleRate / minFrequency);
    
    maxPitch = std::min(maxPitch, (int)autocorr.size() - 1);
    minPitch = std::max(minPitch, 1);
    
    for (int lag = minPitch; lag < maxPitch; ++lag) {
        if (autocorr[static_cast<size_t>(lag)] > maxVal) {
            maxVal = autocorr[static_cast<size_t>(lag)];
            maxLag = lag;
        }
    }
    
    // Dynamic correlation threshold
    float correlationThreshold = rms * rms * size * correlationThresholdFactor;
    if (maxVal < correlationThreshold) {
        failedDetections++;
        return 0.0f;
    }
    
    float detectedFrequency = maxLag > 0 ? (float)sampleRate / maxLag : 0.0f;
    
    // Calculate performance metrics
    auto endTime = Time::getHighResolutionTicks();
    double processingTimeMs = Time::highResolutionTicksToSeconds(endTime - startTime) * 1000.0;
    
    // Update telemetry
    telemetry.totalProcessingCycles++;
    if (processingTimeMs > telemetry.maxProcessingTimeMs) {
        telemetry.maxProcessingTimeMs = processingTimeMs;
    }
    telemetry.avgProcessingTimeMs = (telemetry.avgProcessingTimeMs * (telemetry.totalProcessingCycles - 1) + processingTimeMs) / telemetry.totalProcessingCycles;
    
    telemetry.recentProcessingTimes.push_back(processingTimeMs);
    if (telemetry.recentProcessingTimes.size() > 100) {
        telemetry.recentProcessingTimes.erase(telemetry.recentProcessingTimes.begin());
    }
    
    if (detectedFrequency > 0) {
        detectionCount++;
        telemetry.successfulDetections++;
        
        float confidence = maxVal / (rms * rms * size);
        telemetry.avgDetectionConfidence = (telemetry.avgDetectionConfidence * (telemetry.successfulDetections - 1) + confidence) / telemetry.successfulDetections;
        
        int freqBin = (int)(detectedFrequency / 10.0f) * 10;
        telemetry.frequencyBins[freqBin]++;
        
        if (telemetry.minDetectedFreq == 0.0f || detectedFrequency < telemetry.minDetectedFreq) {
            telemetry.minDetectedFreq = detectedFrequency;
        }
        if (detectedFrequency > telemetry.maxDetectedFreq) {
            telemetry.maxDetectedFreq = detectedFrequency;
        }
        
        telemetry.recentFrequencies.push_back(detectedFrequency);
        if (telemetry.recentFrequencies.size() > 100) {
            telemetry.recentFrequencies.erase(telemetry.recentFrequencies.begin());
        }
        
        telemetry.recentConfidences.push_back(confidence);
        if (telemetry.recentConfidences.size() > 100) {
            telemetry.recentConfidences.erase(telemetry.recentConfidences.begin());
        }
    } else {
        failedDetections++;
    }
    
    return detectedFrequency;
}

//==============================================================================
float PitchDetectionEngine::detectPitchYin(const float* buffer, int size, double sampleRate)
{
    auto startTime = Time::getHighResolutionTicks();
    
    // YIN algorithm implementation
    std::vector<float> yin(size / 2);
    
    // Calculate difference function
    for (int lag = 1; lag < size / 2; ++lag) {
        float sum = 0.0f;
        for (int i = 0; i < size - lag; ++i) {
            float diff = buffer[i] - buffer[i + lag];
            sum += diff * diff;
        }
        yin[lag] = sum;
    }
    
    // Cumulative mean normalized difference
    yin[0] = 1.0f;
    float cumulativeSum = yin[1];
    for (int lag = 1; lag < size / 2; ++lag) {
        if (lag > 1) cumulativeSum += yin[lag];
        yin[lag] = yin[lag] * lag / cumulativeSum;
    }
    
    // Find first minimum below threshold
    float threshold = 0.1f; // YIN threshold
    int minLag = (int)(sampleRate / maxFrequency);
    int maxLag = (int)(sampleRate / minFrequency);
    maxLag = std::min(maxLag, (int)yin.size() - 1);
    
    for (int lag = minLag; lag < maxLag; ++lag) {
        if (yin[lag] < threshold) {
            // Parabolic interpolation for better accuracy
            float betterLag = interpolatePeak(yin, lag);
            float frequency = (float)sampleRate / betterLag;
            
            // Update telemetry
            auto endTime = Time::getHighResolutionTicks();
            double processingTimeMs = Time::highResolutionTicksToSeconds(endTime - startTime) * 1000.0;
            telemetry.totalProcessingCycles++;
            
            return frequency;
        }
    }
    
    return 0.0f;
}

float PitchDetectionEngine::detectPitchHPS(const float* buffer, int size, double sampleRate)
{
    // Harmonic Product Spectrum - multiply downsampled versions
    std::vector<float> spectrum(size / 2);
    std::vector<float> hps(size / 8); // Smaller for efficiency
    
    // Simple FFT approximation using autocorrelation
    // (Real FFT would be better but this works for demonstration)
    for (int i = 0; i < size / 2; ++i) {
        float real = 0.0f, imag = 0.0f;
        for (int j = 0; j < size; ++j) {
            float angle = 2.0f * M_PI * i * j / size;
            real += buffer[j] * cos(angle);
            imag += buffer[j] * sin(angle);
        }
        spectrum[i] = real * real + imag * imag; // Power spectrum
    }
    
    // Harmonic product
    for (int i = 1; i < hps.size(); ++i) {
        hps[i] = spectrum[i];
        
        // Multiply harmonics (2nd, 3rd, 4th)
        if (i * 2 < spectrum.size()) hps[i] *= spectrum[i * 2];
        if (i * 3 < spectrum.size()) hps[i] *= spectrum[i * 3];
        if (i * 4 < spectrum.size()) hps[i] *= spectrum[i * 4];
    }
    
    // Find peak
    int maxIdx = 1;
    for (int i = 2; i < hps.size(); ++i) {
        if (hps[i] > hps[maxIdx]) {
            maxIdx = i;
        }
    }
    
    float frequency = maxIdx * (float)sampleRate / size;
    return (frequency >= minFrequency && frequency <= maxFrequency) ? frequency : 0.0f;
}

float PitchDetectionEngine::detectPitchCepstrum(const float* buffer, int size, double sampleRate)
{
    // Cepstral analysis - simplified version
    std::vector<float> logSpectrum(size / 2);
    
    // Calculate log power spectrum (simplified)
    for (int i = 1; i < size / 2; ++i) {
        float real = 0.0f, imag = 0.0f;
        for (int j = 0; j < size; ++j) {
            float angle = 2.0f * M_PI * i * j / size;
            real += buffer[j] * cos(angle);
            imag += buffer[j] * sin(angle);
        }
        float power = real * real + imag * imag;
        logSpectrum[i] = log(power + 1e-10f); // Avoid log(0)
    }
    
    // Inverse FFT of log spectrum (simplified autocorrelation)
    std::vector<float> cepstrum(size / 4);
    for (int i = 1; i < cepstrum.size(); ++i) {
        float sum = 0.0f;
        for (int j = 1; j < logSpectrum.size(); ++j) {
            sum += logSpectrum[j] * cos(2.0f * M_PI * i * j / logSpectrum.size());
        }
        cepstrum[i] = sum;
    }
    
    // Find peak in quefrency domain
    int minQuefrency = (int)(sampleRate / maxFrequency);
    int maxQuefrency = (int)(sampleRate / minFrequency);
    maxQuefrency = std::min(maxQuefrency, (int)cepstrum.size() - 1);
    
    int maxIdx = minQuefrency;
    for (int i = minQuefrency; i < maxQuefrency; ++i) {
        if (cepstrum[i] > cepstrum[maxIdx]) {
            maxIdx = i;
        }
    }
    
    float frequency = (float)sampleRate / maxIdx;
    return frequency;
}

//==============================================================================
void PitchDetectionEngine::preprocess(const float* input, float* output, int size)
{
    // Apply Hann window to reduce spectral leakage
    for (int i = 0; i < size; ++i) {
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (size - 1)));
        output[i] = input[i] * window;
    }
    
    // Simple high-pass filter to remove DC and low-frequency noise
    float alpha = 0.95f; // High-pass filter coefficient
    float prev = 0.0f;
    for (int i = 0; i < size; ++i) {
        float filtered = alpha * (prev + output[i] - (i > 0 ? output[i-1] : 0.0f));
        prev = filtered;
        output[i] = filtered;
    }
}

float PitchDetectionEngine::interpolatePeak(const std::vector<float>& data, int peakIndex)
{
    if (peakIndex <= 0 || peakIndex >= data.size() - 1) {
        return (float)peakIndex;
    }
    
    // Parabolic interpolation
    float y1 = data[peakIndex - 1];
    float y2 = data[peakIndex];
    float y3 = data[peakIndex + 1];
    
    float a = (y1 - 2*y2 + y3) / 2.0f;
    float b = (y3 - y1) / 2.0f;
    
    if (std::abs(a) < 1e-10f) return (float)peakIndex;
    
    float offset = -b / (2.0f * a);
    return peakIndex + offset;
}

float PitchDetectionEngine::applyStabilityFilter(float newFrequency)
{
    if (newFrequency <= 0.0f) {
        return 0.0f;
    }
    
    recentDetections.push_back(newFrequency);
    if (recentDetections.size() > stabilityWindow) {
        recentDetections.erase(recentDetections.begin());
    }
    
    if (recentDetections.size() < 2) {
        return newFrequency;
    }
    
    // Calculate median for stability
    std::vector<float> sorted = recentDetections;
    std::sort(sorted.begin(), sorted.end());
    
    float median = sorted[sorted.size() / 2];
    
    // Check if new frequency is within stability threshold of median
    if (std::abs(newFrequency - median) / median > stabilityThreshold) {
        // If unstable, return median of recent detections
        return median;
    }
    
    return newFrequency;
}

//==============================================================================
NoteInfo PitchDetectionEngine::frequencyToNote(float frequency)
{
    NoteInfo info;
    
    if (frequency <= 0.0f) {
        return info;
    }
    
    static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    
    // A4 = 440 Hz is our reference
    float A4 = 440.0f;
    float C0 = A4 * std::pow(2.0f, -4.75f); // C0 frequency
    
    if (frequency < C0) {
        return info; // Too low to be meaningful
    }
    
    // Calculate semitones from C0
    float semitonesFromC0 = 12.0f * std::log2(frequency / C0);
    int noteIndex = (int)std::round(semitonesFromC0) % 12;
    if (noteIndex < 0) noteIndex += 12;
    
    info.octave = (int)std::round(semitonesFromC0) / 12;
    info.noteName = String(noteNames[noteIndex]);
    
    // Calculate cents deviation
    float exactSemitones = semitonesFromC0;
    float nearestSemitone = std::round(exactSemitones);
    info.centsDeviation = (exactSemitones - nearestSemitone) * 100.0f;
    
    info.isValid = true;
    
    // Track note in telemetry
    String fullNoteName = info.noteName + String(info.octave);
    telemetry.noteDetections[fullNoteName]++;
    
    return info;
}

//==============================================================================
void PitchDetectionEngine::initializeTelemetry()
{
    telemetry.reset();
    telemetry.platformInfo = SystemStats::getOperatingSystemName();
}

//==============================================================================
String PitchDetectionEngine::exportTelemetryJson() const
{
    String jsonData = "{\n";
    jsonData += "  \"sessionInfo\": {\n";
    jsonData += "    \"startTime\": \"" + telemetry.sessionStart.toString(true, true, true, true) + "\",\n";
    jsonData += "    \"durationSeconds\": " + String((double)telemetry.sessionDurationSeconds, 3) + ",\n";
    jsonData += "    \"platform\": \"" + telemetry.platformInfo + "\",\n";
    jsonData += "    \"audioDevice\": \"" + telemetry.audioDeviceInfo + "\",\n";
    jsonData += "    \"sampleRate\": " + String(telemetry.sampleRateInfo) + ",\n";
    jsonData += "    \"bufferSize\": " + String(telemetry.bufferSizeInfo) + "\n";
    jsonData += "  },\n";
    
    jsonData += "  \"performance\": {\n";
    jsonData += "    \"totalAudioSamples\": " + String(telemetry.totalAudioSamples) + ",\n";
    jsonData += "    \"totalProcessingCycles\": " + String(telemetry.totalProcessingCycles) + ",\n";
    jsonData += "    \"avgProcessingTimeMs\": " + String(telemetry.avgProcessingTimeMs, 6) + ",\n";
    jsonData += "    \"maxProcessingTimeMs\": " + String(telemetry.maxProcessingTimeMs, 6) + "\n";
    jsonData += "  },\n";
    
    jsonData += "  \"detection\": {\n";
    jsonData += "    \"totalAttempts\": " + String(telemetry.totalDetectionAttempts) + ",\n";
    jsonData += "    \"successfulDetections\": " + String(telemetry.successfulDetections) + ",\n";
    jsonData += "    \"successRate\": " + String(telemetry.totalDetectionAttempts > 0 ? 
                                              (float)telemetry.successfulDetections / telemetry.totalDetectionAttempts : 0.0f, 6) + ",\n";
    jsonData += "    \"avgConfidence\": " + String(telemetry.avgDetectionConfidence, 6) + ",\n";
    jsonData += "    \"minFreq\": " + String(telemetry.minDetectedFreq, 3) + ",\n";
    jsonData += "    \"maxFreq\": " + String(telemetry.maxDetectedFreq, 3) + "\n";
    jsonData += "  },\n";
    
    jsonData += "  \"noteDetections\": {\n";
    bool first = true;
    for (const auto& note : telemetry.noteDetections) {
        if (!first) jsonData += ",\n";
        jsonData += "    \"" + note.first + "\": " + String(note.second);
        first = false;
    }
    jsonData += "\n  },\n";
    
    jsonData += "  \"frequencyBins\": {\n";
    first = true;
    for (const auto& bin : telemetry.frequencyBins) {
        if (!first) jsonData += ",\n";
        jsonData += "    \"" + String(bin.first) + "\": " + String(bin.second);
        first = false;
    }
    jsonData += "\n  }\n";
    jsonData += "}\n";
    
    return jsonData;
}

//==============================================================================
// Vocal-specific detection methods

float PitchDetectionEngine::detectPitchWithConfidence(const float* buffer, int size, double sampleRate, float& confidence)
{
    confidence = 0.0f;
    
    if (!vocalOptimization) {
        float pitch = detectPitch(buffer, size, sampleRate);
        confidence = (pitch > 0.0f) ? 0.8f : 0.0f; // Basic confidence
        return pitch;
    }
    
    float vocalPitch = detectVocalPitch(buffer, size, sampleRate);
    
    if (vocalPitch > 0.0f) {
        // Calculate confidence based on harmonic content and stability
        float harmonicWeight = weighByHarmonics(vocalPitch, buffer, size, sampleRate);
        float formantWeight = analyzeFormantContent(buffer, size, sampleRate);
        
        confidence = 0.3f + (harmonicWeight * 0.4f) + (formantWeight * 0.3f);
        confidence = jlimit(0.0f, 1.0f, confidence);
    }
    
    return vocalPitch;
}

std::vector<float> PitchDetectionEngine::getHarmonicContent(const float* buffer, int size, double sampleRate, float fundamental)
{
    std::vector<float> harmonics;
    if (fundamental <= 0.0f || size <= 0) return harmonics;
    
    // Analyze first 8 harmonics
    harmonics.reserve(8);
    
    // Simple FFT-based harmonic analysis
    std::vector<float> spectrum(size / 2);
    
    // Calculate power spectrum
    for (int i = 1; i < size / 2; ++i) {
        float real = 0.0f, imag = 0.0f;
        for (int j = 0; j < size; ++j) {
            float angle = 2.0f * MathConstants<float>::pi * i * j / size;
            real += buffer[j] * std::cos(angle);
            imag += buffer[j] * std::sin(angle);
        }
        spectrum[i] = real * real + imag * imag;
    }
    
    // Find harmonic peaks
    float binFreq = static_cast<float>(sampleRate) / size;
    for (int harmonic = 1; harmonic <= 8; ++harmonic) {
        float targetFreq = fundamental * harmonic;
        int targetBin = static_cast<int>(targetFreq / binFreq);
        
        if (targetBin < spectrum.size()) {
            harmonics.push_back(spectrum[targetBin]);
        } else {
            harmonics.push_back(0.0f);
        }
    }
    
    return harmonics;
}

float PitchDetectionEngine::detectVocalPitch(const float* buffer, int size, double sampleRate)
{
    // Enhanced YIN algorithm optimized for vocals
    float pitch = detectPitchYin(buffer, size, sampleRate);
    
    if (pitch > 0.0f && vocalOptimization) {
        // Apply vocal-specific refinements
        float harmonicWeight = weighByHarmonics(pitch, buffer, size, sampleRate);
        float formantInfluence = analyzeFormantContent(buffer, size, sampleRate);
        
        // Adjust pitch based on harmonic strength
        if (harmonicWeight > 0.5f) {
            // Pitch is well-supported by harmonics, trust it more
            pitch = applyStabilityFilter(pitch);
        } else if (harmonicWeight < 0.3f) {
            // Weak harmonic support, might be noise
            return 0.0f;
        }
        
        // Use formant information to validate vocal content
        if (formantInfluence < 0.2f) {
            // Doesn't look like vocal content
            pitch *= 0.5f; // Reduce confidence
        }
    }
    
    return pitch;
}

float PitchDetectionEngine::analyzeFormantContent(const float* buffer, int size, double sampleRate)
{
    // Analyze formant regions typical for human vocals
    // F1: 300-900Hz, F2: 900-2800Hz, F3: 1900-3800Hz
    
    std::vector<float> spectrum(size / 2);
    
    // Simple power spectrum calculation
    for (int i = 1; i < size / 2; ++i) {
        float real = 0.0f, imag = 0.0f;
        for (int j = 0; j < size; ++j) {
            float angle = 2.0f * MathConstants<float>::pi * i * j / size;
            real += buffer[j] * std::cos(angle);
            imag += buffer[j] * std::sin(angle);
        }
        spectrum[i] = std::sqrt(real * real + imag * imag);
    }
    
    float binFreq = static_cast<float>(sampleRate) / size;
    
    // Analyze formant regions
    float f1Energy = 0.0f, f2Energy = 0.0f, f3Energy = 0.0f;
    int f1Count = 0, f2Count = 0, f3Count = 0;
    
    for (int i = 1; i < spectrum.size(); ++i) {
        float freq = i * binFreq;
        
        if (freq >= 300.0f && freq <= 900.0f) {
            f1Energy += spectrum[i];
            f1Count++;
        } else if (freq >= 900.0f && freq <= 2800.0f) {
            f2Energy += spectrum[i];
            f2Count++;
        } else if (freq >= 1900.0f && freq <= 3800.0f) {
            f3Energy += spectrum[i];
            f3Count++;
        }
    }
    
    // Normalize by bin count
    if (f1Count > 0) f1Energy /= f1Count;
    if (f2Count > 0) f2Energy /= f2Count;
    if (f3Count > 0) f3Energy /= f3Count;
    
    // Calculate formant presence indicator
    float totalFormantEnergy = f1Energy + f2Energy + f3Energy;
    float maxEnergy = *std::max_element(spectrum.begin(), spectrum.end());
    
    return jlimit(0.0f, 1.0f, totalFormantEnergy / (maxEnergy + 1e-10f));
}

float PitchDetectionEngine::weighByHarmonics(float frequency, const float* buffer, int size, double sampleRate)
{
    if (frequency <= 0.0f) return 0.0f;
    
    std::vector<float> harmonics = getHarmonicContent(buffer, size, sampleRate, frequency);
    if (harmonics.empty()) return 0.0f;
    
    // Calculate harmonic-to-noise ratio
    float harmonicSum = 0.0f;
    float fundamentalPower = harmonics[0];
    
    for (size_t i = 0; i < harmonics.size(); ++i) {
        // Weight harmonics by their expected strength (decreasing with order)
        float weight = 1.0f / (i + 1.0f);
        harmonicSum += harmonics[i] * weight;
    }
    
    // Return ratio indicating how well the frequency is supported by harmonics
    return jlimit(0.0f, 1.0f, harmonicSum / (fundamentalPower + harmonicSum + 1e-10f));
}

std::vector<float> PitchDetectionEngine::findFormantPeaks(const float* buffer, int size, double sampleRate)
{
    std::vector<float> formantFreqs;
    
    // Calculate power spectrum
    std::vector<float> spectrum(size / 2);
    for (int i = 1; i < size / 2; ++i) {
        float real = 0.0f, imag = 0.0f;
        for (int j = 0; j < size; ++j) {
            float angle = 2.0f * MathConstants<float>::pi * i * j / size;
            real += buffer[j] * std::cos(angle);
            imag += buffer[j] * std::sin(angle);
        }
        spectrum[i] = real * real + imag * imag;
    }
    
    float binFreq = static_cast<float>(sampleRate) / size;
    
    // Find peaks in formant regions
    for (int i = 2; i < spectrum.size() - 2; ++i) {
        float freq = i * binFreq;
        if (freq > 200.0f && freq < 4000.0f) { // Vocal formant range
            if (spectrum[i] > spectrum[i-1] && spectrum[i] > spectrum[i+1] &&
                spectrum[i] > spectrum[i-2] && spectrum[i] > spectrum[i+2]) {
                // Local peak found
                formantFreqs.push_back(freq);
            }
        }
    }
    
    return formantFreqs;
}