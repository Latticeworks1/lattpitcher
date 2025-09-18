#include "AudioMonitor.h"

AudioMonitor::AudioMonitor() {
    startTime_ = Time::getMillisecondCounter();
    reset();
}

void AudioMonitor::recordPacketSent() {
    stats_.packetsSent++;
}

void AudioMonitor::recordPacketReceived() {
    stats_.packetsReceived++;
}

void AudioMonitor::recordPacketDropped() {
    stats_.packetsDropped++;
    recordGlitch("PacketDrop", "Network packet was dropped", 0.7);
}

void AudioMonitor::recordBufferUnderrun() {
    stats_.bufferUnderruns++;
    recordGlitch("BufferUnderrun", "Audio buffer underrun detected", 0.9);
}

void AudioMonitor::recordBufferOverrun() {
    stats_.bufferOverruns++;
    recordGlitch("BufferOverrun", "Audio buffer overrun detected", 0.8);
}

void AudioMonitor::recordGlitch(const String& type, const String& details, double severity) {
    stats_.glitchCount++;
    addGlitchEvent(GlitchEvent(type, details, severity));
    
    DBG("GLITCH DETECTED: " << type << " - " << details << " (severity: " << severity << ")");
    
    // Log to GUI if callback is set - defer to message thread for safety
    if (logCallback_) {
        String level = severity > 0.8 ? "ERROR" : (severity > 0.5 ? "WARNING" : "INFO");
        String message = type + ": " + details + " (severity: " + String(severity, 2) + ")";
        
        // Always defer GUI updates to message thread
        if (MessageManager::getInstance()) {
            MessageManager::callAsync([this, level, message] {
                if (logCallback_) {
                    logCallback_(level, message);
                }
            });
        }
    }
}

void AudioMonitor::recordLatency(double latencyMs) {
    // Simple moving average
    double current = stats_.avgLatency.load();
    stats_.avgLatency = current * 0.9 + latencyMs * 0.1;
}

void AudioMonitor::recordCpuUsage(double usage) {
    stats_.cpuUsage = usage;
}

void AudioMonitor::recordAudioLevels(double input, double output) {
    stats_.inputLevel = input;
    stats_.outputLevel = output;
}

void AudioMonitor::recordProcessTime(double timeMs) {
    stats_.processTimeMs = timeMs;
    stats_.lastProcessTime = Time::getMillisecondCounter();
}

void AudioMonitor::analyzeAudioBuffer(const AudioBuffer<float>& buffer, bool isInput) {
    if (buffer.getNumSamples() == 0) {
        recordGlitch("EmptyBuffer", isInput ? "Input buffer is empty" : "Output buffer is empty", 0.8);
        return;
    }
    
    // Calculate RMS level
    double rms = calculateRMS(buffer);
    
    // Debug: Log suspicious RMS values
    static int logCounter = 0;
    if (++logCounter % 500 == 0) { // Log every ~10 seconds
        if (logCallback_) {
            MessageManager::callAsync([this, rms, isInput] {
                if (logCallback_) {
                    String prefix = isInput ? "Input" : "Output";
                    logCallback_("DEBUG", prefix + " RMS: " + String(rms, 6));
                }
            });
        }
    }
    
    if (isInput) {
        recordAudioLevels(rms, stats_.outputLevel.load());
        
        // Check for input silence (possible disconnection)
        if (rms < 0.001 && stats_.inputLevel.load() > 0.1) {
            recordGlitch("InputSilence", "Sudden drop in input level", 0.6);
        }
    } else {
        recordAudioLevels(stats_.inputLevel.load(), rms);
    }
    
    // Detect audio glitches - but don't flood the log
    static int glitchCounter = 0;
    if (detectGlitch(buffer)) {
        glitchCounter++;
        // Only log every 100th glitch to avoid spam
        if (glitchCounter % 100 == 1) {
            recordGlitch("AudioDistortion", 
                        "Audio distortion detected (x" + String(glitchCounter) + ") - RMS: " + String(rms, 6), 0.9);
        }
    }
    
    // Update RMS history for trend analysis
    int index = rmsIndex_.load();
    rmsHistory_[index] = rms;
    rmsIndex_ = (index + 1) % 4;
}

bool AudioMonitor::detectGlitch(const AudioBuffer<float>& buffer) {
    if (buffer.getNumSamples() == 0) return false;
    
    const float* channelData = buffer.getReadPointer(0);
    double rms = calculateRMS(buffer);
    bool glitchDetected = false;
    
    // Check for sudden spikes
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        float sample = channelData[i];
        
        // Detect clipping
        if (std::abs(sample) > 0.98f) {
            glitchDetected = true;
            break;
        }
        
        // Detect sudden jumps
        double lastSample = lastSampleValue_.load();
        if (std::abs(sample - lastSample) > 0.5) {
            glitchDetected = true;
            break;
        }
        
        lastSampleValue_ = sample;
        
        // Count consecutive zeros (dropout detection) - but only if there was previous audio
        if (std::abs(sample) < 0.0001) {
            consecutiveZeros_++;
            // Only trigger if we had audio before and now have extended silence (1 second)
            if (consecutiveZeros_ > 48000 && std::abs(lastSampleValue_.load()) > 0.01) {
                glitchDetected = true;
                break;
            }
        } else {
            consecutiveZeros_ = 0;
        }
    }
    
    // Check RMS trend for sudden changes - but be less sensitive
    double avgRms = 0.0;
    for (int i = 0; i < 4; ++i) {
        avgRms += rmsHistory_[i].load();
    }
    avgRms /= 4.0;
    
    // Only detect significant changes when there's actual audio content
    if (avgRms > 0.01 && (rms > avgRms * 10.0 || (rms < avgRms * 0.1 && avgRms > 0.1))) {
        glitchDetected = true;
    }
    
    return glitchDetected;
}

std::vector<GlitchEvent> AudioMonitor::getRecentGlitches(int maxCount) const {
    std::lock_guard<std::mutex> lock(glitchMutex_);
    
    std::vector<GlitchEvent> result;
    int count = jmin(maxCount, static_cast<int>(recentGlitches_.size()));
    
    auto it = recentGlitches_.rbegin();
    for (int i = 0; i < count && it != recentGlitches_.rend(); ++i, ++it) {
        result.push_back(*it);
    }
    
    return result;
}

String AudioMonitor::getStatusReport() const {
    String report;
    uint64_t runtime = Time::getMillisecondCounter() - startTime_;
    
    report += "=== NETWORK AUDIO STREAMER STATUS ===\n";
    report += "Runtime: " + String(runtime / 1000.0, 1) + "s\n";
    report += "Packets Sent: " + String(stats_.packetsSent.load()) + "\n";
    report += "Packets Received: " + String(stats_.packetsReceived.load()) + "\n";
    report += "Packets Dropped: " + String(stats_.packetsDropped.load()) + "\n";
    report += "Buffer Underruns: " + String(stats_.bufferUnderruns.load()) + "\n";
    report += "Buffer Overruns: " + String(stats_.bufferOverruns.load()) + "\n";
    report += "Glitches Detected: " + String(stats_.glitchCount.load()) + "\n";
    report += "Average Latency: " + String(stats_.avgLatency.load(), 2) + "ms\n";
    report += "CPU Usage: " + String(stats_.cpuUsage.load() * 100, 1) + "%\n";
    report += "Input Level: " + String(Decibels::gainToDecibels(stats_.inputLevel.load()), 1) + "dB\n";
    report += "Output Level: " + String(Decibels::gainToDecibels(stats_.outputLevel.load()), 1) + "dB\n";
    report += "Last Process Time: " + String(stats_.processTimeMs.load(), 3) + "ms\n";
    
    return report;
}

String AudioMonitor::getDetailedReport() const {
    String report = getStatusReport();
    
    report += "\n=== RECENT GLITCHES ===\n";
    auto glitches = getRecentGlitches(10);
    
    if (glitches.empty()) {
        report += "No recent glitches detected.\n";
    } else {
        for (const auto& glitch : glitches) {
            uint64_t age = Time::getMillisecondCounter() - glitch.timestamp;
            report += String(age / 1000.0, 1) + "s ago: " + glitch.type + " - " + glitch.details + " (severity: " + String(glitch.severity, 2) + ")\n";
        }
    }
    
    return report;
}

void AudioMonitor::reset() {
    stats_.packetsReceived = 0;
    stats_.packetsSent = 0;
    stats_.packetsDropped = 0;
    stats_.bufferUnderruns = 0;
    stats_.bufferOverruns = 0;
    stats_.glitchCount = 0;
    stats_.avgLatency = 0.0;
    stats_.cpuUsage = 0.0;
    stats_.inputLevel = 0.0;
    stats_.outputLevel = 0.0;
    stats_.processTimeMs = 0.0;
    
    lastSampleValue_ = 0.0;
    consecutiveZeros_ = 0;
    for (int i = 0; i < 4; ++i) {
        rmsHistory_[i] = 0.0;
    }
    rmsIndex_ = 0;
    
    std::lock_guard<std::mutex> lock(glitchMutex_);
    recentGlitches_.clear();
    
    startTime_ = Time::getMillisecondCounter();
}

void AudioMonitor::addGlitchEvent(const GlitchEvent& event) {
    std::lock_guard<std::mutex> lock(glitchMutex_);
    
    recentGlitches_.push_back(event);
    
    // Keep only last 100 glitch events
    if (recentGlitches_.size() > 100) {
        recentGlitches_.pop_front();
    }
}

double AudioMonitor::calculateRMS(const AudioBuffer<float>& buffer) const {
    double sum = 0.0;
    int totalSamples = 0;
    
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        const float* channelData = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            sum += channelData[sample] * channelData[sample];
            totalSamples++;
        }
    }
    
    return totalSamples > 0 ? std::sqrt(sum / totalSamples) : 0.0;
}