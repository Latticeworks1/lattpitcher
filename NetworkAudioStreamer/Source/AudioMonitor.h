#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <deque>
#include <mutex>

using namespace juce;

struct AudioStats {
    std::atomic<int> packetsReceived{0};
    std::atomic<int> packetsSent{0};
    std::atomic<int> packetsDropped{0};
    std::atomic<int> bufferUnderruns{0};
    std::atomic<int> bufferOverruns{0};
    std::atomic<double> avgLatency{0.0};
    std::atomic<double> cpuUsage{0.0};
    std::atomic<double> inputLevel{0.0};
    std::atomic<double> outputLevel{0.0};
    std::atomic<int> glitchCount{0};
    std::atomic<uint64_t> lastProcessTime{0};
    std::atomic<double> processTimeMs{0.0};
};

struct GlitchEvent {
    uint64_t timestamp;
    String type;
    String details;
    double severity;
    
    GlitchEvent(const String& t, const String& d, double s = 1.0)
        : timestamp(Time::getMillisecondCounter()), type(t), details(d), severity(s) {}
};

class AudioMonitor {
public:
    AudioMonitor();
    ~AudioMonitor() = default;
    
    // Set GUI callback for logging - thread safe
    void setLogCallback(std::function<void(const String&, const String&)> callback) {
        logCallback_ = std::move(callback);
    }
    
    void clearLogCallback() {
        logCallback_ = nullptr;
    }
    
    // Core monitoring methods
    void recordPacketSent();
    void recordPacketReceived();
    void recordPacketDropped();
    void recordBufferUnderrun();
    void recordBufferOverrun();
    void recordGlitch(const String& type, const String& details, double severity = 1.0);
    void recordLatency(double latencyMs);
    void recordCpuUsage(double usage);
    void recordAudioLevels(double input, double output);
    void recordProcessTime(double timeMs);
    
    // Audio analysis
    void analyzeAudioBuffer(const AudioBuffer<float>& buffer, bool isInput);
    bool detectGlitch(const AudioBuffer<float>& buffer);
    
    // Statistics access
    const AudioStats& getStats() const { return stats_; }
    std::vector<GlitchEvent> getRecentGlitches(int maxCount = 50) const;
    String getStatusReport() const;
    String getDetailedReport() const;
    
    // Reset counters
    void reset();
    
private:
    AudioStats stats_;
    mutable std::mutex glitchMutex_;
    std::deque<GlitchEvent> recentGlitches_;
    
    // Glitch detection state
    std::atomic<double> lastSampleValue_{0.0};
    std::atomic<int> consecutiveZeros_{0};
    std::atomic<double> rmsHistory_[4] = {0.0, 0.0, 0.0, 0.0};
    std::atomic<int> rmsIndex_{0};
    
    // Performance monitoring
    std::atomic<uint64_t> startTime_;
    
    // GUI logging callback
    std::function<void(const String&, const String&)> logCallback_;
    
    void addGlitchEvent(const GlitchEvent& event);
    double calculateRMS(const AudioBuffer<float>& buffer) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioMonitor)
};