#include "AutotuneEngine.h"

//==============================================================================
AutotuneEngine::AutotuneEngine()
{
    // Initialize buffers
    delayBuffer.resize(maxDelayInSamples, 0.0f);
    windowBuffer.resize(2048, 0.0f);
    overlapBuffer.resize(1024, 0.0f);
    
    // Initialize PSOLA
    psolaState.grainBuffer.resize(2048, 0.0f);
    psolaState.pitchMarks.reserve(100);
    
    // Create Hann window for pitch shifting
    for (int i = 0; i < 2048; ++i)
    {
        windowBuffer[i] = 0.5f * (1.0f - std::cos(2.0f * MathConstants<float>::pi * i / 2047.0f));
    }
}

//==============================================================================
void AutotuneEngine::prepareToPlay(double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate;
    blockSize = maximumExpectedSamplesPerBlock;
    
    // Reset processing state
    reset();
    
    // Initialize pitch shifting based on sample rate
    initializePitchShifter();

    // Prepare phase vocoder state
    pv.prepare();
    
    // Prepare LPC formant state
    lpcState.prepare(newSampleRate);

    // Reset PSOLA streaming state
    psolaState.inBuffer.clear();
    psolaState.marks.clear();
    psolaState.inReadPos = 0;
    psolaState.initialized = false;
    psolaState.baseInMark = 0;
    psolaState.baseOutPos = 0.0;
    psolaState.outWritePos = 0.0;
    psolaState.olaBuffer.assign(8192, 0.0f);
    psolaState.olaReadPos = 0;
}

void AutotuneEngine::reset()
{
    currentTargetPitch = 0.0f;
    previousTargetPitch = 0.0f;
    currentCorrectionAmount = 0.0f;
    pitchCorrectionActive = false;
    
    targetPitchSmoothingState = 0.0f;
    correctionSmoothingState = 0.0f;
    
    std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
    std::fill(overlapBuffer.begin(), overlapBuffer.end(), 0.0f);
    delayWriteIndex = 0;
    
    psolaState.lastPitchMark = 0;
    psolaState.grainPhase = 0.0f;
    psolaState.pitchMarks.clear();
    
    // Reset vibrato detection state
    std::fill(vibratoState.centsBuffer.begin(), vibratoState.centsBuffer.end(), 0.0f);
    std::fill(vibratoState.filteredBuffer.begin(), vibratoState.filteredBuffer.end(), 0.0f);
    vibratoState.writeIndex = 0;
    vibratoState.highPassState = 0.0f;
    vibratoState.bandpassLowState = 0.0f;
    vibratoState.bandpassHighState = 0.0f;
    vibratoState.sampleCounter = 0;
    vibratoState.rmsAmplitude = 0.0f;
    vibratoState.autocorrPeriodicity = 0.0f;
    vibratoState.currentState = VibratoDetectionState::HMMState::GenericDeviation;
    vibratoState.previousState = VibratoDetectionState::HMMState::GenericDeviation;
    vibratoState.stateFrameCount = 0;
    vibratoState.isVibratoActive = false;
    vibratoState.currentStrength = 0.0f;
    
    totalProcessedSamples = 0;
    correctionEventsCount = 0;
    
    // Reset LPC state
    lpcState.zfrState1 = 0.0f;
    lpcState.zfrState2 = 0.0f;
    std::fill(lpcState.lpcCoeffs.begin(), lpcState.lpcCoeffs.end(), 0.0f);
    std::fill(lpcState.residualBuffer.begin(), lpcState.residualBuffer.end(), 0.0f);
    std::fill(lpcState.vocalTractBuffer.begin(), lpcState.vocalTractBuffer.end(), 0.0f);
    lpcState.epochPositions.clear();
    std::fill(lpcState.currentFormants.begin(), lpcState.currentFormants.end(), 0.0f);
    std::fill(lpcState.targetFormants.begin(), lpcState.targetFormants.end(), 0.0f);
}

//==============================================================================
void AutotuneEngine::processBlock(AudioBuffer<float>& buffer, const float* pitchData, int numSamples)
{
    if (numSamples <= 0 || buffer.getNumChannels() == 0) return;

    float* audioData = buffer.getWritePointer(0); // mono processing

    // Estimate representative detected pitch for this block (median of valid)
    float detectedPitch = 0.0f;
    if (pitchData != nullptr)
    {
        std::vector<float> vals; vals.reserve(numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            float p = pitchData[i];
            if (p > 50.0f && p < 2000.0f && std::isfinite(p)) vals.push_back(p);
        }
        if (!vals.empty())
        {
            std::nth_element(vals.begin(), vals.begin() + (int)vals.size()/2, vals.end());
            detectedPitch = vals[(int)vals.size()/2];
        }
    }

    if (!(detectedPitch > 50.0f && detectedPitch < 2000.0f))
    {
        pitchCorrectionActive = false;
        currentCorrectionAmount = 0.0f;
        totalProcessedSamples += numSamples;
        return;
    }

    float targetPitch = calculateTargetPitch(detectedPitch);
    targetPitch = applyCorrectionSmoothing(targetPitch, detectedPitch);
    currentTargetPitch = targetPitch;
    currentCorrectionAmount = std::abs(targetPitch - detectedPitch) / detectedPitch;
    pitchCorrectionActive = (currentCorrectionAmount > 0.01f && settings.correctionStrength > 0.01f);

    // Process vibrato detection
    processVibratoDetection(detectedPitch);
    
    if (!pitchCorrectionActive)
    {
        totalProcessedSamples += numSamples;
        return;
    }

    float ratio = targetPitch / detectedPitch;
    float adaptiveStrength = applyVibratoAdaptiveCorrection(settings.correctionStrength);
    ratio = 1.0f + (ratio - 1.0f) * adaptiveStrength;

    // Keep a copy for wet/dry
    juce::HeapBlock<float> dry;
    dry.allocate((size_t)numSamples, true);
    std::memcpy(dry.getData(), audioData, sizeof(float) * (size_t)numSamples);

    // Choose engine
    auto mode = engineMode;
    if (mode == EngineMode::Auto)
    {
        const bool voiced = std::abs(ratio - 1.0f) > 0.01f; // proxy
        mode = voiced ? EngineMode::PSOLA : EngineMode::PhaseVocoder;
    }

    if (mode == EngineMode::PSOLA)
        processPSOLA(audioData, numSamples, ratio);
    else
        processPhaseVocoder(audioData, numSamples, ratio);

    // Wet/dry mix
    if (settings.mixAmount < 1.0f)
    {
        const float wet = settings.mixAmount;
        const float dryAmt = 1.0f - wet;
        for (int i = 0; i < numSamples; ++i)
            audioData[i] = wet * audioData[i] + dryAmt * dry[i];
    }

    totalProcessedSamples += numSamples;
}

//==============================================================================
float AutotuneEngine::calculateTargetPitch(float detectedPitch)
{
    if (detectedPitch <= 0.0f) return detectedPitch;
    
    // Convert to MIDI note number
    int midiNote = frequencyToMidiNote(detectedPitch);
    
    // Find nearest note in active scale
    int targetMidiNote = findNearestScaleNote(midiNote);
    
    // Convert back to frequency
    float targetPitch = midiNoteToFrequency(targetMidiNote);
    
    // Add humanization if enabled
    if (settings.humanization > 0.0f)
    {
        Random random;
        float humanizationCents = (random.nextFloat() - 0.5f) * settings.humanization * 20.0f; // ±10 cents max
        float humanizationRatio = std::pow(2.0f, humanizationCents / 1200.0f);
        targetPitch *= humanizationRatio;
    }
    
    return targetPitch;
}

float AutotuneEngine::applyCorrectionSmoothing(float targetPitch, float currentPitch)
{
    // Smooth target pitch changes for natural transitions
    float smoothingTime = jmap(settings.correctionSpeed, 0.2f, 0.01f); // Faster speed = less smoothing
    currentTargetPitch = applySmoothingFilter(targetPitch, targetPitchSmoothingState, smoothingTime);
    
    return currentTargetPitch;
}

void AutotuneEngine::processPitchCorrection(float* audioData, int numSamples, float pitchRatio)
{
    // Simple linear interpolation pitch shifting (placeholder)
    // TODO: Replace with PSOLA or Phase Vocoder for production quality
    
    // SECURITY: Input validation for autotune processing
    if (audioData == nullptr || numSamples <= 0 || numSamples > 16384) {
        jassertfalse; // Debug alert for invalid parameters
        return;
    }
    
    for (int i = 0; i < numSamples; ++i)
    {
        // SECURITY: Validate input sample
        float sample = audioData[i];
        if (!std::isfinite(sample)) {
            sample = 0.0f; // Safe fallback for invalid samples
        } else {
            sample = jlimit(-10.0f, 10.0f, sample); // Clamp to safe range
        }
        
        // SECURITY: Bounds check delay buffer write index
        if (delayWriteIndex >= 0 && delayWriteIndex < maxDelayInSamples) {
            delayBuffer[delayWriteIndex] = sample;
        }
        
        // Calculate read position for pitch shifting with safety checks
        float readPos = delayWriteIndex - (512.0f * (pitchRatio - 1.0f));
        
        // SECURITY: Validate read position calculation
        if (!std::isfinite(readPos)) {
            readPos = delayWriteIndex; // Safe fallback
        }
        
        if (readPos < 0.0f)
            readPos += maxDelayInSamples;
        else if (readPos >= maxDelayInSamples)
            readPos -= maxDelayInSamples;
        
        // Linear interpolation with bounds checking
        int readIndex1 = static_cast<int>(readPos);
        int readIndex2 = (readIndex1 + 1) % maxDelayInSamples;
        
        // SECURITY: Bounds check read indices
        if (readIndex1 >= 0 && readIndex1 < maxDelayInSamples &&
            readIndex2 >= 0 && readIndex2 < maxDelayInSamples) {
            
            float fraction = readPos - readIndex1;
            fraction = jlimit(0.0f, 1.0f, fraction); // Clamp interpolation fraction
            
            float interpolatedSample = delayBuffer[readIndex1] * (1.0f - fraction) + 
                                     delayBuffer[readIndex2] * fraction;
            
            // SECURITY: Validate output sample
            if (std::isfinite(interpolatedSample)) {
                audioData[i] = jlimit(-10.0f, 10.0f, interpolatedSample);
            } else {
                audioData[i] = sample; // Keep original if interpolation failed
            }
        } else {
            audioData[i] = sample; // Keep original if indices are invalid
        }
        
        delayWriteIndex = (delayWriteIndex + 1) % maxDelayInSamples;
    }
}

//==============================================================================
std::array<bool, 12> AutotuneEngine::getActiveScale() const
{
    switch (settings.scaleType)
    {
        case ScaleType::Chromatic:
            return {true, true, true, true, true, true, true, true, true, true, true, true};
        
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
        
        default:
            return {true, true, true, true, true, true, true, true, true, true, true, true};
    }
}

float AutotuneEngine::snapToScale(float frequency) const
{
    int midiNote = frequencyToMidiNote(frequency);
    int targetNote = findNearestScaleNote(midiNote);
    return midiNoteToFrequency(targetNote);
}

int AutotuneEngine::frequencyToMidiNote(float frequency) const
{
    if (frequency <= 0.0f) return 0;
    
    // Convert frequency to MIDI note: MIDI_note = 69 + 12 * log2(freq/440)
    float midiNoteFloat = 69.0f + 12.0f * std::log2(frequency / settings.referencePitch);
    return static_cast<int>(std::round(midiNoteFloat));
}

float AutotuneEngine::midiNoteToFrequency(int midiNote) const
{
    // Convert MIDI note to frequency: freq = 440 * 2^((MIDI_note - 69)/12)
    return settings.referencePitch * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
}

bool AutotuneEngine::isNoteInScale(int midiNote) const
{
    std::array<bool, 12> scale = getActiveScale();
    int noteClass = ((midiNote % 12) + settings.rootNote) % 12;
    return scale[noteClass];
}

int AutotuneEngine::findNearestScaleNote(int midiNote) const
{
    if (isNoteInScale(midiNote))
        return midiNote;
    
    // Search up and down for nearest scale note
    for (int offset = 1; offset <= 6; ++offset)
    {
        if (isNoteInScale(midiNote + offset))
            return midiNote + offset;
        if (isNoteInScale(midiNote - offset))
            return midiNote - offset;
    }
    
    return midiNote; // Fallback
}

//==============================================================================
void AutotuneEngine::initializePitchShifter()
{
    // Initialize pitch shifting parameters based on sample rate
    psolaState.grainSize = static_cast<int>(sampleRate * 0.02); // 20ms grains
    psolaState.grainSize = jmin(psolaState.grainSize, 2048);
    psolaState.grainBuffer.resize(psolaState.grainSize);
}

void AutotuneEngine::processPSOLA(float* audioData, int numSamples, float pitchRatio)
{
    // Streaming PSOLA: cross-block pitch-mark detection and persistent OLA
    if (numSamples <= 0 || pitchRatio <= 0.0f || !std::isfinite(pitchRatio)) return;
    
    // Enhanced PSOLA with optional LPC formant preservation
    if (lpcFormantMode && settings.formantCorrection && std::abs(pitchRatio - 1.0f) > 0.01f)
    {
        // Create working copy for LPC processing
        std::vector<float> workingBuffer(audioData, audioData + numSamples);
        
        // Apply hybrid PSOLA + LPC formant preservation
        processHybridPSOLAWithLPC(workingBuffer.data(), numSamples, pitchRatio);
        
        // Copy back to output
        std::copy(workingBuffer.begin(), workingBuffer.end(), audioData);
        return;
    }

    // Append current block to input buffer
    const int oldSize = (int)psolaState.inBuffer.size();
    psolaState.inBuffer.insert(psolaState.inBuffer.end(), audioData, audioData + numSamples);

    // Estimate period from target pitch (samples)
    double approxPeriod = psolaState.lastApproxPeriod;
    if (currentTargetPitch > 50.0f)
        approxPeriod = juce::jlimit(50.0, 2000.0, sampleRate / juce::jlimit(50.0f, 2000.0f, currentTargetPitch));
    psolaState.lastApproxPeriod = approxPeriod;

    // Detect new pitch marks in [oldSize .. inBuffer.size()-1]
    const int searchStart = juce::jmax(psolaState.inReadPos, oldSize - (int)approxPeriod); // small overlap
    const int searchEnd = (int)psolaState.inBuffer.size() - 1;
    if (searchEnd - searchStart > (int)approxPeriod)
    {
        // Predict marks every approxPeriod and refine to positive zero-crossing with max slope
        int cur = (psolaState.marks.empty() ? searchStart + (int)approxPeriod : psolaState.marks.back() + (int)approxPeriod);
        while (cur + 2 < searchEnd)
        {
            const int w = (int)(approxPeriod * 0.5);
            int s0 = juce::jlimit(searchStart, searchEnd - 1, cur - w);
            int s1 = juce::jlimit(searchStart + 2, searchEnd, cur + w);

            // Find positive-going zero-cross with maximum slope
            int bestIdx = s0;
            float bestSlope = 0.0f;
            for (int i = s0 + 1; i < s1; ++i)
            {
                float y0 = psolaState.inBuffer[i - 1];
                float y1 = psolaState.inBuffer[i];
                if (y0 <= 0.0f && y1 > 0.0f)
                {
                    float slope = y1 - y0;
                    if (slope > bestSlope) { bestSlope = slope; bestIdx = i; }
                }
            }
            psolaState.marks.push_back(bestIdx);
            cur = bestIdx + (int)approxPeriod;
        }
    }

    // Prepare Hann window sized to 2*period (clamped)
    const int grain = juce::jlimit(256, 2048, (int)(2.0 * approxPeriod));
    if ((int)windowBuffer.size() < grain) windowBuffer.resize(grain);
    for (int i = 0; i < grain; ++i)
        windowBuffer[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (grain - 1)));

    // Ensure OLA buffer large enough
    if ((int)psolaState.olaBuffer.size() < psolaState.olaReadPos + numSamples + grain)
        psolaState.olaBuffer.resize(psolaState.olaReadPos + numSamples + grain, 0.0f);

    // Initialize base mapping the first time
    if (!psolaState.initialized && !psolaState.marks.empty())
    {
        psolaState.baseInMark = psolaState.marks.front();
        psolaState.baseOutPos = (double)psolaState.olaReadPos;
        psolaState.outWritePos = psolaState.baseOutPos;
        psolaState.initialized = true;
    }

    // Synthesize grains mapped to scaled timeline
    const double scale = 1.0 / juce::jmax(0.0001f, pitchRatio); // r>1 raises pitch => compress time
    for (int mi = 0; mi < (int)psolaState.marks.size(); ++mi)
    {
        int mark = psolaState.marks[mi];
        // Only synthesize marks for which the whole grain is within available input
        if (mark - grain/2 < 0 || mark + grain/2 >= (int)psolaState.inBuffer.size())
            continue;

        double outPos = psolaState.baseOutPos + (double)(mark - psolaState.baseInMark) * scale;

        // OLA grain into PSOLA OLA buffer
        for (int i = 0; i < grain; ++i)
        {
            int src = mark - grain/2 + i;
            int dst = (int)std::floor(outPos) - grain/2 + i;
            if (dst >= 0 && dst < (int)psolaState.olaBuffer.size())
                psolaState.olaBuffer[dst] += psolaState.inBuffer[src] * windowBuffer[i];
        }
    }

    // Emit exactly numSamples from OLA buffer
    for (int n = 0; n < numSamples; ++n)
    {
        int idx = psolaState.olaReadPos + n;
        audioData[n] = (idx < (int)psolaState.olaBuffer.size()) ? psolaState.olaBuffer[idx] : 0.0f;
    }
    psolaState.olaReadPos += numSamples;

    // Compact buffers occasionally to avoid unbounded growth
    const int maxBuf = 48000 * 2; // ~2 seconds safety
    if ((int)psolaState.inBuffer.size() > maxBuf)
    {
        int shift = juce::jmin(psolaState.inReadPos, (int)psolaState.inBuffer.size() - maxBuf);
        if (shift > 0)
        {
            psolaState.inBuffer.erase(psolaState.inBuffer.begin(), psolaState.inBuffer.begin() + shift);
            for (int& m : psolaState.marks) m -= shift;
            psolaState.inReadPos -= shift;
            psolaState.baseInMark -= shift;
        }
    }

    if (psolaState.olaReadPos > maxBuf)
    {
        int shift = psolaState.olaReadPos - maxBuf/2;
        if (shift > 0 && shift < (int)psolaState.olaBuffer.size())
        {
            std::memmove(psolaState.olaBuffer.data(), psolaState.olaBuffer.data() + shift, (psolaState.olaBuffer.size() - shift) * sizeof(float));
            std::fill(psolaState.olaBuffer.begin() + (psolaState.olaBuffer.size() - shift), psolaState.olaBuffer.end(), 0.0f);
            psolaState.olaReadPos -= shift;
            psolaState.baseOutPos -= shift;
        }
    }
}

void AutotuneEngine::processPhaseVocoder(float* audioData, int numSamples, float pitchRatio)
{
    if (numSamples <= 0 || pitchRatio <= 0.0f || ! std::isfinite(pitchRatio))
        return;

    const int N = pv.fftSize;
    const int H = pv.hopSize;
    const float twoPi = 2.0f * juce::MathConstants<float>::pi;
    // Ensure OLA buffer enough capacity for new frames
    if ((int)pv.olaBuffer.size() < pv.olaWritePos + numSamples + N)
        pv.olaBuffer.resize(pv.olaWritePos + numSamples + N, 0.0f);

    // Process streaming frames across calls for continuity
    int pos = 0;
    while (pos + N <= numSamples)
    {
        // Windowed frame
        for (int n = 0; n < N; ++n)
            pv.analysisFrame[n] = audioData[pos + n] * pv.window[n];

        // Interleaved FFT buffer
        std::vector<float> fftBuf(2 * N, 0.0f);
        for (int n = 0; n < N; ++n) { fftBuf[2*n] = pv.analysisFrame[n]; }
        pv.fft.performRealOnlyForwardTransform(fftBuf.data());

        // Load spectrum
        for (int k = 0; k < N; ++k)
            pv.analysisSpec[k] = { fftBuf[2*k], fftBuf[2*k+1] };

        // Compute smoothed log-magnitude spectral envelope on analysis bins (0..N/2-1)
        {
            const int K = N/2;
            static constexpr int W = 9; // moving-average half-width (~19 bins window)
            // temp buffer for log-magnitude
            if ((int)pv.envMag.size() < K) pv.envMag.resize(K);
            std::vector<float> logMag(K);
            for (int k = 0; k < K; ++k)
            {
                float m = std::abs(pv.analysisSpec[k]) + 1e-8f;
                logMag[k] = std::log(m);
            }
            for (int k = 0; k < K; ++k)
            {
                int a = juce::jmax(0, k - W);
                int b = juce::jmin(K - 1, k + W);
                double sum = 0.0;
                int cnt = 0;
                for (int i = a; i <= b; ++i) { sum += logMag[i]; ++cnt; }
                pv.envMag[k] = (float)std::exp(sum / juce::jmax(1, cnt));
            }
        }

        // Detect simple spectral peaks and build region phase-lock map via midpoints
        std::vector<int> peaks;
        peaks.reserve(N/16);
        for (int k = 2; k < N/2 - 2; ++k)
        {
            float m1 = std::abs(pv.analysisSpec[k-1]);
            float m0 = std::abs(pv.analysisSpec[k]);
            float p1 = std::abs(pv.analysisSpec[k+1]);
            if (m0 > m1 && m0 > p1 && m0 > 1e-4f)
                peaks.push_back(k);
        }
        if (peaks.empty()) peaks.push_back(N/8); // fallback
        std::sort(peaks.begin(), peaks.end());
        peaks.erase(std::unique(peaks.begin(), peaks.end()), peaks.end());
        // Region map: for each k select nearest peak by midpoint boundaries
        const int Kbins = N/2;
        std::vector<int> regionLock(Kbins);
        size_t pi = 0;
        for (int k = 0; k < Kbins; ++k)
        {
            while (pi + 1 < peaks.size())
            {
                int left = peaks[pi];
                int right = peaks[pi+1];
                int mid = (left + right) / 2;
                if (k > mid) ++pi; else break;
            }
            regionLock[k] = peaks[pi];
        }

        // Phase vocoder with identity phase-locking
        std::fill(pv.synthSpec.begin(), pv.synthSpec.end(), std::complex<float>(0.0f, 0.0f));
        std::vector<char> seeded; seeded.resize(N/2, 0);

        // First update phase accumulators for peaks
        for (int pi : peaks)
        {
            float mag = std::abs(pv.analysisSpec[pi]);
            float ph  = std::arg(pv.analysisSpec[pi]);
            float omega = twoPi * (float)pi / (float)N;
            float delta = ph - pv.prevPhase[pi] - omega * (float)H;
            delta -= twoPi * std::floor((delta + juce::MathConstants<float>::pi) / (twoPi));
            float inst = omega + delta / (float)H; // rad/sample

            // advance phase accumulator of the peak's mapped bin
            float kOutF = pitchRatio * (float)pi;
            int kOut = juce::jlimit(0, N/2 - 1, (int)std::round(kOutF));
            if (!pv.initialized)
            {
                pv.phaseAcc[kOut] = ph; // seed with analysis phase on first frame at peak map
                seeded[kOut] = 1;
            }
            else
                pv.phaseAcc[kOut] += inst * (float)H;
        }

        // Then map all bins, locking phases to nearest peak accumulator
        for (int k = 0; k < N/2; ++k)
        {
            float mag = std::abs(pv.analysisSpec[k]);
            if (mag <= 0.0f) { pv.prevPhase[k] = std::arg(pv.analysisSpec[k]); continue; }

            // region phase-lock peak by midpoint regions
            int nearest = regionLock[k];

            // output bin mapping
            float kOutF = pitchRatio * (float)k;
            int k0 = (int)std::floor(kOutF);
            float frac = kOutF - (float)k0;
            if (k0 >= 0 && k0 + 1 < N/2)
            {
                int kLock = juce::jlimit(0, N/2 - 1, (int)std::round(pitchRatio * (float)nearest));
                float phase = pv.phaseAcc[kLock];
                if (!pv.initialized)
                {
                    if (!seeded[k0])  { pv.phaseAcc[k0]   = phase; seeded[k0]   = 1; }
                    if (!seeded[k0+1]){ pv.phaseAcc[k0+1] = phase; seeded[k0+1] = 1; }
                }

                // Spectral envelope normalization: scale by envelope ratio Ein[kOut]/Ein[k]
                float Ein_k     = pv.envMag[juce::jlimit(0, N/2 - 1, k)];
                float Ein_kOut0 = pv.envMag[juce::jlimit(0, N/2 - 1, k0)];
                float Ein_kOut1 = pv.envMag[juce::jlimit(0, N/2 - 1, k0 + 1)];
                float safe = 1e-6f;
                float scale0 = Ein_kOut0 / juce::jmax(Ein_k, safe);
                float scale1 = Ein_kOut1 / juce::jmax(Ein_k, safe);

                std::complex<float> Y0 = std::polar(mag * (1.0f - frac) * scale0, phase);
                std::complex<float> Y1 = std::polar(mag * frac * scale1,            phase);
                pv.synthSpec[k0]   += Y0;
                pv.synthSpec[k0+1] += Y1;
            }

            pv.prevPhase[k] = std::arg(pv.analysisSpec[k]);
        }

        // IFFT
        std::vector<float> ifftBuf(2 * N, 0.0f);
        for (int k = 0; k < N; ++k) { ifftBuf[2*k] = pv.synthSpec[k].real(); ifftBuf[2*k+1] = pv.synthSpec[k].imag(); }
        pv.fft.performRealOnlyInverseTransform(ifftBuf.data());

        // OLA into persistent buffer at current write position and accumulate window sum
        for (int n = 0; n < N; ++n)
        {
            float w = pv.window[n];
            float s = (ifftBuf[2*n] / (float)N) * w;
            int dst = pv.olaWritePos + n;
            if (dst >= (int)pv.olaBuffer.size()) {
                int newSize = dst + N + N; // grow with margin
                pv.olaBuffer.resize(newSize, 0.0f);
                pv.olaWindowSum.resize(newSize, 0.0f);
            }
            pv.olaBuffer[dst] += s;
            pv.olaWindowSum[dst] += w;
        }
        pv.olaWritePos += H;

        pos += H;
    }

    pv.initialized = true;

    // Produce exactly numSamples from the OLA buffer with normalization
    // Warm-up: only emit samples covered by at least three frames (strong COLA region)
    const int minReady = pv.olaWritePos - 2 * (N - H);
    const int canEmit = juce::jmax(0, minReady - pv.olaProduced);
    const int toEmit = juce::jmin(numSamples, canEmit);
    for (int i = 0; i < toEmit; ++i)
    {
        int idx = pv.olaProduced + i;
        float denom = pv.olaWindowSum[idx];
        // window-sum floor to reduce head/tail artifacts post warm-up
        if (denom < 1e-6f) { audioData[i] = 0.0f; continue; }
        float norm = pv.olaBuffer[idx] / denom;
        audioData[i] = norm;
    }
    for (int i = toEmit; i < numSamples; ++i)
        audioData[i] = 0.0f; // tail padding if not enough produced yet
    pv.olaProduced += toEmit;

    // Compact buffers if head grows too large
    const int compactThreshold = pv.fftSize * 8;
    if (pv.olaProduced > compactThreshold)
    {
        const int remaining = pv.olaWritePos - pv.olaProduced;
        if (remaining > 0)
        {
            std::memmove(pv.olaBuffer.data(), pv.olaBuffer.data() + pv.olaProduced, remaining * sizeof(float));
            std::memmove(pv.olaWindowSum.data(), pv.olaWindowSum.data() + pv.olaProduced, remaining * sizeof(float));
        }
        pv.olaWritePos = remaining;
        pv.olaProduced = 0;
        // zero the tail slack
        std::fill(pv.olaBuffer.begin() + pv.olaWritePos, pv.olaBuffer.end(), 0.0f);
        std::fill(pv.olaWindowSum.begin() + pv.olaWritePos, pv.olaWindowSum.end(), 0.0f);
    }
}

//==============================================================================
void AutotuneEngine::updateSmoothingFilters()
{
    // Recalculate filter coefficients if needed
}

float AutotuneEngine::applySmoothingFilter(float input, float& smoothingState, float smoothingTime)
{
    float alpha = 1.0f - std::exp(-1.0f / (smoothingTime * sampleRate));
    smoothingState += alpha * (input - smoothingState);
    return smoothingState;
}

//==============================================================================
// Vibrato Detection Implementation
void AutotuneEngine::processVibratoDetection(float detectedPitch)
{
    if (!(detectedPitch > 50.0f && detectedPitch < 2000.0f)) return;
    
    // Convert pitch to cents deviation from A4 (440Hz)
    float centsValue = 1200.0f * std::log2(detectedPitch / settings.referencePitch);
    
    // Update circular buffer
    vibratoState.centsBuffer[vibratoState.writeIndex] = centsValue;
    
    // Apply high-pass filter to remove slow drift (1-2 second time constant)
    const float highPassTC = 1.5f; // time constant in seconds
    const float highPassAlpha = 1.0f - std::exp(-1.0f / (highPassTC * sampleRate / 64.0f)); // per-block update
    const float highPassedCents = centsValue - vibratoState.highPassState;
    vibratoState.highPassState += highPassAlpha * (centsValue - vibratoState.highPassState);
    
    // Apply bandpass filter (3-9 Hz) - implemented as cascade of high-pass (3Hz) and low-pass (9Hz)
    const float bpLowCutoff = 3.0f; // Hz
    const float bpHighCutoff = 9.0f; // Hz
    const float sampleRateBlock = sampleRate / 64.0f; // effective sample rate per block
    
    // High-pass 3Hz
    const float bpHighAlpha = 1.0f - std::exp(-2.0f * MathConstants<float>::pi * bpLowCutoff / sampleRateBlock);
    const float bpHighFiltered = highPassedCents - vibratoState.bandpassHighState;
    vibratoState.bandpassHighState += bpHighAlpha * (highPassedCents - vibratoState.bandpassHighState);
    
    // Low-pass 9Hz  
    const float bpLowAlpha = 1.0f - std::exp(-2.0f * MathConstants<float>::pi * bpHighCutoff / sampleRateBlock);
    vibratoState.bandpassLowState += bpLowAlpha * (bpHighFiltered - vibratoState.bandpassLowState);
    
    // Store filtered result
    vibratoState.filteredBuffer[vibratoState.writeIndex] = vibratoState.bandpassLowState;
    vibratoState.writeIndex = (vibratoState.writeIndex + 1) % VibratoDetectionState::bufferSize;
    
    // Update features every 10ms (feature computation)
    vibratoState.sampleCounter += 64; // assuming 64-sample blocks
    if (vibratoState.sampleCounter >= VibratoDetectionState::featureWindowSamples)
    {
        vibratoState.sampleCounter = 0;
        updateVibratoFeatures(vibratoState.bandpassLowState);
    }
}

void AutotuneEngine::updateVibratoFeatures(float centsValue)
{
    // Compute RMS amplitude A over analysis window
    float sumSquares = 0.0f;
    const int windowSize = jmin(VibratoDetectionState::bufferSize, 441); // 10ms window
    for (int i = 0; i < windowSize; ++i)
    {
        int idx = (vibratoState.writeIndex - i + VibratoDetectionState::bufferSize) % VibratoDetectionState::bufferSize;
        float sample = vibratoState.filteredBuffer[idx];
        sumSquares += sample * sample;
    }
    vibratoState.rmsAmplitude = std::sqrt(sumSquares / windowSize);
    
    // Compute autocorrelation periodicity P
    vibratoState.autocorrPeriodicity = computeAutocorrelationPeriodicity();
    
    // Two-state HMM: Natural vibrato vs Generic deviation
    const bool vibratoCondition = (vibratoState.rmsAmplitude >= VibratoDetectionState::Amin) && 
                                  (vibratoState.autocorrPeriodicity >= VibratoDetectionState::Pmin);
    
    VibratoDetectionState::HMMState newState = vibratoCondition ? 
        VibratoDetectionState::HMMState::NaturalVibrato : 
        VibratoDetectionState::HMMState::GenericDeviation;
    
    // Apply hysteresis (Tvib = 6 frames = 60ms)
    if (newState != vibratoState.currentState)
    {
        vibratoState.stateFrameCount++;
        if (vibratoState.stateFrameCount >= VibratoDetectionState::Tvib)
        {
            vibratoState.previousState = vibratoState.currentState;
            vibratoState.currentState = newState;
            vibratoState.stateFrameCount = 0;
        }
    }
    else
    {
        vibratoState.stateFrameCount = 0;
    }
    
    // Update output state
    vibratoState.isVibratoActive = (vibratoState.currentState == VibratoDetectionState::HMMState::NaturalVibrato);
    vibratoState.currentStrength = vibratoState.isVibratoActive ? vibratoState.rmsAmplitude / 20.0f : 0.0f; // normalize
    vibratoState.currentStrength = jlimit(0.0f, 1.0f, vibratoState.currentStrength);
}

float AutotuneEngine::computeAutocorrelationPeriodicity()
{
    const int windowSize = jmin(VibratoDetectionState::bufferSize / 2, 220); // 5ms correlation window
    const int maxLag = jmin(windowSize / 2, 88); // max lag ~2ms at 44.1kHz
    
    if (windowSize < 32) return 0.0f; // insufficient data
    
    // Compute autocorrelation
    float maxCorr = 0.0f;
    float zeroLagCorr = 0.0f;
    
    // Zero lag (energy)
    for (int i = 0; i < windowSize; ++i)
    {
        int idx = (vibratoState.writeIndex - i + VibratoDetectionState::bufferSize) % VibratoDetectionState::bufferSize;
        float sample = vibratoState.filteredBuffer[idx];
        zeroLagCorr += sample * sample;
    }
    
    if (zeroLagCorr < 1e-6f) return 0.0f; // silence
    
    // Find maximum correlation at non-zero lags (vibrato period detection)
    for (int lag = 3; lag < maxLag; ++lag) // start at lag 3 to skip near-zero artifacts
    {
        float corr = 0.0f;
        for (int i = 0; i < windowSize - lag; ++i)
        {
            int idx1 = (vibratoState.writeIndex - i + VibratoDetectionState::bufferSize) % VibratoDetectionState::bufferSize;
            int idx2 = (vibratoState.writeIndex - i - lag + VibratoDetectionState::bufferSize) % VibratoDetectionState::bufferSize;
            corr += vibratoState.filteredBuffer[idx1] * vibratoState.filteredBuffer[idx2];
        }
        
        if (corr > maxCorr)
            maxCorr = corr;
    }
    
    // Normalize by zero-lag correlation (energy)
    return maxCorr / jmax(zeroLagCorr, 1e-6f);
}

float AutotuneEngine::applyVibratoAdaptiveCorrection(float originalStrength) const
{
    if (!vibratoState.isVibratoActive)
        return originalStrength;
    
    // Adaptive correction: g0 * (1 - kv * Avib)
    const float kv = VibratoDetectionState::kv;
    const float Avib = vibratoState.currentStrength;
    const float scaleFactor = 1.0f - kv * Avib;
    
    // Ensure reasonable bounds
    const float adaptiveStrength = originalStrength * jlimit(0.1f, 1.0f, scaleFactor);
    
    return adaptiveStrength;
}

//==============================================================================
// LPC Formant Preservation Implementation
void AutotuneEngine::processHybridPSOLAWithLPC(float* audioData, int numSamples, float pitchRatio)
{
    if (numSamples <= 0 || pitchRatio <= 0.0f || !std::isfinite(pitchRatio)) return;
    
    // Store original for wet/dry mixing
    std::vector<float> originalBuffer(audioData, audioData + numSamples);
    
    // 1. Epoch detection using zero-frequency resonator
    lpcState.epochPositions.clear();
    detectEpochsZFR(audioData, numSamples, lpcState.epochPositions);
    
    if (lpcState.epochPositions.empty())
    {
        // Fallback to standard PSOLA if no epochs detected
        processPSOLAFallback(audioData, numSamples, pitchRatio);
        return;
    }
    
    const int windowSize = static_cast<int>(sampleRate * LPCFormantState::windowSizeMs / 1000.0);
    const int hopSize = static_cast<int>(sampleRate * LPCFormantState::hopSizeMs / 1000.0);
    const float alpha = pitchRatio;  // pitch scaling factor
    
    // Clear output buffer
    std::fill(audioData, audioData + numSamples, 0.0f);
    
    // 2. Process each epoch with LPC analysis
    for (size_t ei = 0; ei < lpcState.epochPositions.size(); ++ei)
    {
        int epochPos = lpcState.epochPositions[ei];
        if (epochPos - windowSize/2 < 0 || epochPos + windowSize/2 >= numSamples)
            continue;
            
        int windowStart = epochPos - windowSize/2;
        
        // 3. LPC analysis on 20ms Hamming window centered at epoch
        std::vector<float> coeffs;
        float gain = 0.0f;
        performLPCAnalysis(originalBuffer.data(), windowStart, windowSize, coeffs, gain);
        
        // 4. Bandwidth expansion for stability
        applyBandwidthExpansion(coeffs, LPCFormantState::stabilityRho);
        
        // 5. Inverse filtering to get residual
        std::vector<float> residual(windowSize);
        inverseFilter(originalBuffer.data() + windowStart, windowSize, coeffs, residual.data());
        
        // 6. Psychoacoustic formant mapping (simplified)
        std::vector<float> originalFormants, mappedFormants;
        extractFormantsFromLPC(coeffs, originalFormants);
        mapFormants(originalFormants, mappedFormants, alpha, formantScalingBeta);
        
        // 7. Epoch-synchronous PSOLA on residual only
        std::vector<float> psolaResidual(residual);
        applyPSOLAToResidual(psolaResidual.data(), windowSize, pitchRatio, epochPos);
        
        // 8. Reapply modified vocal tract
        std::vector<float> modifiedCoeffs;
        reconstructLPCFromFormants(mappedFormants, modifiedCoeffs, gain);
        
        std::vector<float> output(windowSize);
        reapplyVocalTract(psolaResidual.data(), windowSize, modifiedCoeffs, output.data());
        
        // 9. Overlap-add with crossfading to avoid zippering
        int outputStart = static_cast<int>(epochPos * (1.0f / pitchRatio)) - windowSize/2;
        outputStart = juce::jlimit(0, numSamples - windowSize, outputStart);
        
        for (int i = 0; i < windowSize && outputStart + i < numSamples; ++i)
        {
            float sample = output[i] * lpcState.hammingWindow[i];  // Apply window
            audioData[outputStart + i] += sample;
        }
    }
    
    // Apply safety limiting and smoothing
    for (int i = 0; i < numSamples; ++i)
    {
        audioData[i] = juce::jlimit(-2.0f, 2.0f, audioData[i]);
    }
}

void AutotuneEngine::detectEpochsZFR(const float* signal, int length, std::vector<int>& epochs)
{
    // Zero-frequency resonator for glottal closure detection
    epochs.clear();
    if (length < 100) return;
    
    std::vector<float> zfr(length);
    
    // Zero-frequency resonator: y[n] = 2*y[n-1] - y[n-2] + x[n] - x[n-1]
    for (int n = 2; n < length; ++n)
    {
        float x_n = signal[n];
        float x_n1 = signal[n-1];
        
        zfr[n] = 2.0f * lpcState.zfrState1 - lpcState.zfrState2 + x_n - x_n1;
        
        lpcState.zfrState2 = lpcState.zfrState1;
        lpcState.zfrState1 = zfr[n];
    }
    
    // Find negative-to-positive zero crossings in ZFR output (glottal closures)
    const float threshold = 0.01f * *std::max_element(zfr.begin(), zfr.end());
    
    for (int i = 1; i < length - 1; ++i)
    {
        if (zfr[i-1] < -threshold && zfr[i] > threshold)
        {
            epochs.push_back(i);
        }
    }
    
    // Remove epochs too close together (minimum 50 samples apart)
    if (epochs.size() > 1)
    {
        auto newEnd = std::unique(epochs.begin(), epochs.end(), 
            [](int a, int b) { return std::abs(a - b) < 50; });
        epochs.erase(newEnd, epochs.end());
    }
}

void AutotuneEngine::performLPCAnalysis(const float* signal, int windowStart, int windowSize, 
                                       std::vector<float>& coeffs, float& gain)
{
    coeffs.resize(LPCFormantState::lpcOrder + 1);
    std::fill(coeffs.begin(), coeffs.end(), 0.0f);
    
    if (windowSize < LPCFormantState::lpcOrder * 2) return;
    
    // Apply Hamming window
    std::vector<float> windowed(windowSize);
    for (int i = 0; i < windowSize; ++i)
    {
        int idx = juce::jlimit(0, static_cast<int>(lpcState.hammingWindow.size()) - 1, i);
        windowed[i] = signal[windowStart + i] * lpcState.hammingWindow[idx];
    }
    
    // Autocorrelation method for LPC analysis
    std::vector<float> autocorr(LPCFormantState::lpcOrder + 1);
    
    // Compute autocorrelation
    for (int k = 0; k <= LPCFormantState::lpcOrder; ++k)
    {
        float sum = 0.0f;
        for (int n = 0; n < windowSize - k; ++n)
        {
            sum += windowed[n] * windowed[n + k];
        }
        autocorr[k] = sum;
    }
    
    if (autocorr[0] < 1e-10f) return;  // Avoid division by zero
    
    // Levinson-Durbin algorithm
    std::vector<float> a(LPCFormantState::lpcOrder + 1);
    std::vector<float> k(LPCFormantState::lpcOrder + 1);
    
    float error = autocorr[0];
    
    for (int i = 1; i <= LPCFormantState::lpcOrder; ++i)
    {
        float sum = 0.0f;
        for (int j = 1; j < i; ++j)
        {
            sum += a[j] * autocorr[i - j];
        }
        
        k[i] = -(autocorr[i] + sum) / error;
        a[i] = k[i];
        
        // Update previous coefficients
        for (int j = 1; j < i; ++j)
        {
            float temp = a[j];
            a[j] = temp + k[i] * a[i - j];
        }
        
        error *= (1.0f - k[i] * k[i]);
        if (error <= 0.0f) break;
    }
    
    // Copy coefficients (a[0] = 1.0 by definition)
    coeffs[0] = 1.0f;
    for (int i = 1; i <= LPCFormantState::lpcOrder; ++i)
    {
        coeffs[i] = a[i];
    }
    
    gain = std::sqrt(juce::jmax(0.0f, error));
}

void AutotuneEngine::applyBandwidthExpansion(std::vector<float>& coeffs, float rho)
{
    // Bandwidth expansion: replace a[k] with a[k] * rho^k
    float rhoPower = 1.0f;
    for (size_t k = 0; k < coeffs.size(); ++k)
    {
        coeffs[k] *= rhoPower;
        rhoPower *= rho;
    }
}

void AutotuneEngine::inverseFilter(const float* signal, int length, 
                                  const std::vector<float>& coeffs, float* residual)
{
    // Inverse filtering: residual[n] = signal[n] + sum(a[k] * signal[n-k])
    for (int n = 0; n < length; ++n)
    {
        float sum = signal[n];
        
        for (size_t k = 1; k < coeffs.size() && k <= static_cast<size_t>(n); ++k)
        {
            sum += coeffs[k] * signal[n - k];
        }
        
        residual[n] = sum;
    }
}

void AutotuneEngine::mapFormants(const std::vector<float>& originalFormants, 
                                std::vector<float>& mappedFormants, float alpha, float beta)
{
    // Psychoacoustic formant mapping: F'k = Fk × α^β
    mappedFormants.resize(originalFormants.size());
    
    const float alphaPowerBeta = std::pow(juce::jlimit(0.5f, 2.0f, alpha), 
                                         juce::jlimit(0.2f, 0.5f, beta));
    
    for (size_t i = 0; i < originalFormants.size(); ++i)
    {
        float originalF = originalFormants[i];
        float mappedF = originalF * alphaPowerBeta;
        
        // Clamp formant movement to 25% per 200 cents (spec requirement)
        const float maxChange = originalF * maxFormantMovement;
        mappedF = juce::jlimit(originalF - maxChange, originalF + maxChange, mappedF);
        
        mappedFormants[i] = mappedF;
    }
}

void AutotuneEngine::reapplyVocalTract(float* residual, int length, 
                                      const std::vector<float>& modifiedCoeffs, float* output)
{
    // Reapply vocal tract filter: output[n] = residual[n] - sum(a[k] * output[n-k])
    std::fill(output, output + length, 0.0f);
    
    for (int n = 0; n < length; ++n)
    {
        float sum = residual[n];
        
        for (size_t k = 1; k < modifiedCoeffs.size() && k <= static_cast<size_t>(n); ++k)
        {
            sum -= modifiedCoeffs[k] * output[n - k];
        }
        
        output[n] = sum;
    }
}

void AutotuneEngine::crossfadeFrames(float* buffer1, const float* buffer2, int length, int crossfadeLength)
{
    // 10ms crossfade to avoid zippering artifacts
    const int fadeLen = juce::jmin(crossfadeLength, length / 2);
    
    for (int i = 0; i < fadeLen; ++i)
    {
        const float fade = static_cast<float>(i) / fadeLen;
        buffer1[i] = buffer1[i] * (1.0f - fade) + buffer2[i] * fade;
        
        const int endIdx = length - 1 - i;
        if (endIdx >= fadeLen)
        {
            buffer1[endIdx] = buffer1[endIdx] * fade + buffer2[endIdx] * (1.0f - fade);
        }
    }
}

// Simplified helper methods for formant extraction/reconstruction
void AutotuneEngine::extractFormantsFromLPC(const std::vector<float>& coeffs, std::vector<float>& formants)
{
    formants.clear();
    // Simplified formant extraction - find roots of LPC polynomial
    // In production, this would use proper root finding algorithms
    
    // For now, estimate formants at typical vocal frequencies
    formants = {700.0f, 1220.0f, 2600.0f, 3010.0f, 4000.0f};  // Typical formants
}

void AutotuneEngine::reconstructLPCFromFormants(const std::vector<float>& formants, 
                                               std::vector<float>& coeffs, float gain)
{
    // Simplified LPC reconstruction from formants
    // In production, this would reconstruct the polynomial from formant frequencies and bandwidths
    coeffs.resize(LPCFormantState::lpcOrder + 1);
    std::fill(coeffs.begin(), coeffs.end(), 0.0f);
    coeffs[0] = 1.0f;  // a[0] = 1
    
    // Simplified reconstruction - use original coeffs as approximation
    // Production version would properly reconstruct from formant data
    for (size_t i = 1; i < coeffs.size() && i < lpcState.lpcCoeffs.size(); ++i)
    {
        coeffs[i] = lpcState.lpcCoeffs[i];
    }
}

void AutotuneEngine::applyPSOLAToResidual(float* residual, int length, float pitchRatio, int epochPos)
{
    // Apply PSOLA time-scaling to residual signal only
    // This is a simplified version - production would use full PSOLA on the residual
    
    if (pitchRatio == 1.0f) return;
    
    std::vector<float> temp(residual, residual + length);
    
    // Simple time-domain resampling for the residual
    for (int i = 0; i < length; ++i)
    {
        float srcPos = i * pitchRatio;
        int idx1 = static_cast<int>(srcPos);
        int idx2 = idx1 + 1;
        
        if (idx2 < length)
        {
            float frac = srcPos - idx1;
            residual[i] = temp[idx1] * (1.0f - frac) + temp[idx2] * frac;
        }
        else if (idx1 < length)
        {
            residual[i] = temp[idx1];
        }
        else
        {
            residual[i] = 0.0f;
        }
    }
}

void AutotuneEngine::processPSOLAFallback(float* audioData, int numSamples, float pitchRatio)
{
    // Fallback to original PSOLA implementation when LPC fails
    // This preserves the existing behavior
    
    // Temporarily disable LPC mode for this call
    bool wasEnabled = lpcFormantMode;
    lpcFormantMode = false;
    
    // Call original PSOLA (this will now skip the LPC check at the top)
    // Note: We need to implement the original PSOLA logic here to avoid recursion
    // For now, apply simple pitch shifting
    
    if (pitchRatio == 1.0f) return;
    
    std::vector<float> temp(audioData, audioData + numSamples);
    
    for (int i = 0; i < numSamples; ++i)
    {
        float srcPos = i * pitchRatio;
        int idx1 = static_cast<int>(srcPos);
        int idx2 = idx1 + 1;
        
        if (idx2 < numSamples)
        {
            float frac = srcPos - idx1;
            audioData[i] = temp[idx1] * (1.0f - frac) + temp[idx2] * frac;
        }
        else if (idx1 < numSamples)
        {
            audioData[i] = temp[idx1];
        }
        else
        {
            audioData[i] = 0.0f;
        }
    }
    
    lpcFormantMode = wasEnabled;
}
