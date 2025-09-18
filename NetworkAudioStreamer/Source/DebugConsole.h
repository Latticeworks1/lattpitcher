#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

using namespace juce;

#include "NetworkAudioProcessor.h"
#include "SpectrumAnalyzer.h"
#include "ModernTheme.h"
#include "AudioMonitor.h"

class DebugConsole : public Component, public Timer {
public:
    DebugConsole(NetworkAudioProcessor& proc);
    ~DebugConsole() override;
    
    bool isValidComponent() const { return !componentBeingDeleted; }
    
    void paint(Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void addLogEntry(const String& level, const String& message);
    void clearLog();
    void processAudioForAnalysis(const AudioBuffer<float>& buffer);
    
private:
    NetworkAudioProcessor& processor_;
    TextEditor logDisplay_;
    TextButton clearButton_;
    TextButton exportButton_;
    TextButton pauseButton_;
    ComboBox logLevelFilter_;
    TextEditor searchBox_;
    Label searchLabel_;
    Label statsLabel_;
    
    // Advanced visualization components
    Component performanceGraph_;
    std::unique_ptr<SpectrumAnalyzer> spectrumAnalyzer_;
    Component networkTopology_;
    Component advancedMetrics_;
    
    std::vector<float> cpuHistory_;
    std::vector<float> latencyHistory_;
    std::vector<int> glitchHistory_;
    
    // Theme is now external
    
    CriticalSection logLock_;
    std::deque<String> logEntries_;
    std::deque<String> filteredEntries_;
    static constexpr int maxLogEntries_ = 500;
    
    bool isPaused_{false};
    String currentSearchTerm_;
    int selectedLogLevel_{1}; // All
    
    std::atomic<bool> componentBeingDeleted{false};
    
    // Smart alert system
    struct AlertState {
        int consecutiveHighCpu{0};
        int consecutiveHighLatency{0};
        int recentGlitchCount{0};
        uint64_t lastGlitchAlertTime{0};
        uint64_t lastCpuAlertTime{0};
        uint64_t lastLatencyAlertTime{0};
    } alertState_;
    
    void updateStats();
    void updateLogFilter();
    void exportLogs();
    void drawPerformanceGraph(Graphics& g, Rectangle<int> area);
    void drawNetworkTopology(Graphics& g, Rectangle<int> area);
    void drawAdvancedMetrics(Graphics& g, Rectangle<int> area);
    bool matchesFilter(const String& logEntry, const String& searchTerm, int logLevel);
    void checkForCriticalIssues(const AudioStats& stats);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugConsole)
};