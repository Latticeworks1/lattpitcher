#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "NetworkAudioProcessor.h"

using namespace juce;

class SpectrumAnalyzer : public Component, public Timer {
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer() override;
    
    void paint(Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void processAudioData(const AudioBuffer<float>& buffer);
    
private:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    
    dsp::FFT forwardFFT;
    dsp::WindowingFunction<float> window;
    
    float fifo[fftSize];
    float fftData[2 * fftSize];
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;
    float scopeData[fftSize / 2];
    
    void pushNextSampleIntoFifo(float sample) noexcept;
    void drawNextFrameOfSpectrum();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};

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
    
    // Enhanced visual theme
    struct ModernTheme {
        static const Colour backgroundDark;
        static const Colour cardBackground;
        static const Colour accentBlue;
        static const Colour accentGreen;
        static const Colour accentRed;
        static const Colour accentOrange;
        static const Colour textPrimary;
        static const Colour textSecondary;
        static const Colour borderColor;
    };
    
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

class NetworkAudioEditor : public AudioProcessorEditor, public Timer {
public:
    explicit NetworkAudioEditor(NetworkAudioProcessor& p);
    ~NetworkAudioEditor() override;

    void paint(Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    NetworkAudioProcessor& processor_;
    
    // Tabbed interface
    TabbedComponent tabbedComponent_;
    
    // Settings tab components
    Component settingsTab_;
    ComboBox modeBox_;
    Slider portSlider_;
    TextEditor addressEditor_;
    Slider sessionSlider_;
    Slider userSlider_;
    Label statusLabel_;
    TextButton connectButton_;
    TextButton resetStatsButton_;
    
    // Debug tab
    std::unique_ptr<DebugConsole> debugConsole_;
    
    // Statistics display
    Label networkStatsLabel_;
    Label audioStatsLabel_;
    Label performanceLabel_;
    
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments_;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments_;
    
    void setupSettingsTab();
    void setupDebugTab(); 
    void updateStatusDisplay();
    void logMessage(const String& level, const String& message);
    
    std::atomic<bool> editorBeingDeleted{false};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkAudioEditor)
};