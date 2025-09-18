#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_cryptography/juce_cryptography.h>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>

//==============================================================================
namespace FLStreamConstants 
{
    // UI Dimensions
    static constexpr int EDITOR_WIDTH = 400;
    static constexpr int EDITOR_HEIGHT = 600;
    
    // Audio Settings
    static constexpr double DEFAULT_SAMPLE_RATE = 48000.0;
    static constexpr int DEFAULT_BUFFER_SIZE = 512;
    static constexpr int STREAM_FIFO_SIZE = 8192;
    static constexpr int GUI_REFRESH_RATE = 30; // 30 FPS
    
    // Streaming Settings
    static constexpr int DEFAULT_HTTP_PORT = 8080;
    static constexpr int MAX_LISTENERS = 100;
    static constexpr int ROOM_ID_LENGTH = 6;
    
    // Audio Processing
    static constexpr float PEAK_DECAY_RATE = 0.99f;
    static constexpr float RMS_SMOOTHING = 0.95f;
    static constexpr int METER_UPDATE_RATE = 60; // Hz
    
    // Colors (FL Studio style)
    static const juce::Colour FL_BACKGROUND = juce::Colour(0xff2c3e50);
    static const juce::Colour FL_ACCENT = juce::Colour(0xff3498db);  
    static const juce::Colour FL_SUCCESS = juce::Colour(0xff27ae60);
    static const juce::Colour FL_DANGER = juce::Colour(0xffe74c3c);
    static const juce::Colour FL_WARNING = juce::Colour(0xfff1c40f);
    static const juce::Colour FL_TEXT_PRIMARY = juce::Colour(0xffffffff);
    static const juce::Colour FL_TEXT_SECONDARY = juce::Colour(0xffbdc3c7);
}

//==============================================================================
/** Stream metadata and configuration */
struct StreamSettings
{
    juce::String roomId;
    double sampleRate = FLStreamConstants::DEFAULT_SAMPLE_RATE;
    int bitDepth = 24;
    int channels = 2;
    int bufferSize = FLStreamConstants::DEFAULT_BUFFER_SIZE;
    juce::String codec = "PCM";
    bool isActive = false;
    int listenerCount = 0;
    double bandwidth = 0.0; // Mbps
};

//==============================================================================
/** Audio packet for streaming */
struct AudioPacket
{
    int64_t timestamp;
    juce::AudioBuffer<float> audioData;
    int sequenceId;
    double sampleRate;
    int channels;
    
    AudioPacket() : AudioPacket(2, 512, 48000.0) {}
    
    AudioPacket(int numChannels, int numSamples, double sr)
        : timestamp(juce::Time::getCurrentTime().toMilliseconds()),
          audioData(numChannels, numSamples),
          sequenceId(0),
          sampleRate(sr),
          channels(numChannels)
    {
    }
    
    AudioPacket(const AudioPacket& other)
        : timestamp(other.timestamp),
          audioData(other.audioData),
          sequenceId(other.sequenceId),
          sampleRate(other.sampleRate),
          channels(other.channels)
    {
    }
    
    AudioPacket& operator=(const AudioPacket& other)
    {
        if (this != &other)
        {
            timestamp = other.timestamp;
            audioData.makeCopyOf(other.audioData);
            sequenceId = other.sequenceId;
            sampleRate = other.sampleRate;
            channels = other.channels;
        }
        return *this;
    }
};

//==============================================================================
/** Real-time audio meter levels */
struct MeterLevels
{
    float leftRMS = 0.0f;
    float rightRMS = 0.0f;
    float leftPeak = 0.0f;
    float rightPeak = 0.0f;
    float overallPeak = 0.0f;
    
    void reset()
    {
        leftRMS = rightRMS = leftPeak = rightPeak = overallPeak = 0.0f;
    }
    
    void updateFromBuffer(const juce::AudioBuffer<float>& buffer)
    {
        if (buffer.getNumChannels() >= 1)
        {
            leftRMS = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
            leftPeak = juce::jmax(leftPeak * FLStreamConstants::PEAK_DECAY_RATE, 
                                 buffer.getMagnitude(0, 0, buffer.getNumSamples()));
        }
        
        if (buffer.getNumChannels() >= 2)
        {
            rightRMS = buffer.getRMSLevel(1, 0, buffer.getNumSamples());
            rightPeak = juce::jmax(rightPeak * FLStreamConstants::PEAK_DECAY_RATE,
                                  buffer.getMagnitude(1, 0, buffer.getNumSamples()));
        }
        else
        {
            rightRMS = leftRMS;
            rightPeak = leftPeak;
        }
        
        overallPeak = juce::jmax(leftPeak, rightPeak);
    }
};

//==============================================================================
/** Thread-safe circular buffer for audio streaming */
class StreamingFifo
{
public:
    StreamingFifo() : abstractFifo(FLStreamConstants::STREAM_FIFO_SIZE)
    {
        buffer.setSize(2, FLStreamConstants::STREAM_FIFO_SIZE, false, true);
    }
    
    void pushSamples(const float* const* sourceChannels, int numChannels, int numSamples)
    {
        jassert(numChannels <= buffer.getNumChannels());
        
        int start1, size1, start2, size2;
        abstractFifo.prepareToWrite(numSamples, start1, size1, start2, size2);
        
        if (size1 > 0)
        {
            for (int ch = 0; ch < juce::jmin(numChannels, buffer.getNumChannels()); ++ch)
                buffer.copyFrom(ch, start1, sourceChannels[ch], size1);
        }
        
        if (size2 > 0)
        {
            for (int ch = 0; ch < juce::jmin(numChannels, buffer.getNumChannels()); ++ch)
                buffer.copyFrom(ch, start2, sourceChannels[ch] + size1, size2);
        }
        
        abstractFifo.finishedWrite(size1 + size2);
    }
    
    bool pullSamples(juce::AudioBuffer<float>& destBuffer, int numSamples)
    {
        int start1, size1, start2, size2;
        abstractFifo.prepareToRead(numSamples, start1, size1, start2, size2);
        
        if (size1 + size2 < numSamples)
            return false; // Not enough data available
        
        int destPos = 0;
        
        if (size1 > 0)
        {
            for (int ch = 0; ch < juce::jmin(destBuffer.getNumChannels(), buffer.getNumChannels()); ++ch)
                destBuffer.copyFrom(ch, destPos, buffer, ch, start1, size1);
            destPos += size1;
        }
        
        if (size2 > 0)
        {
            for (int ch = 0; ch < juce::jmin(destBuffer.getNumChannels(), buffer.getNumChannels()); ++ch)
                destBuffer.copyFrom(ch, destPos, buffer, ch, start2, size2);
        }
        
        abstractFifo.finishedRead(size1 + size2);
        return true;
    }
    
    int getNumSamplesAvailable() const { return abstractFifo.getNumReady(); }
    
private:
    juce::AbstractFifo abstractFifo;
    juce::AudioBuffer<float> buffer;
};

//==============================================================================
/** WebSocket connection for real-time audio streaming */
class WebSocketConnection
{
public:
    WebSocketConnection(int clientId);
    ~WebSocketConnection();
    
    bool isConnected() const { return connected.load(); }
    void sendAudioData(const AudioPacket& packet);
    void sendMetadata(const StreamSettings& settings);
    void disconnect();
    
    int getClientId() const { return clientId; }
    int64_t getLastActivity() const { return lastActivity.load(); }
    
private:
    void encodeAudioPacket(const AudioPacket& packet, juce::MemoryBlock& encoded);
    void sendWebSocketFrame(const void* data, size_t size, uint8_t opcode);
    
    int clientId;
    std::atomic<bool> connected{true};
    std::atomic<int64_t> lastActivity{0};
    
    juce::CriticalSection sendLock;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebSocketConnection)
};

//==============================================================================
/** Production HTTP/WebSocket server for streaming audio data */
class StreamingServer
{
public:
    StreamingServer();
    ~StreamingServer();
    
    bool startServer(int port = FLStreamConstants::DEFAULT_HTTP_PORT);
    void stopServer();
    bool isServerRunning() const { return serverRunning.load(); }
    
    void setStreamSettings(const StreamSettings& settings);
    void pushAudioData(const AudioPacket& packet);
    
    int getListenerCount() const { return static_cast<int>(connections.size()); }
    double getBandwidthUsage() const { return bandwidthUsage.load(); }
    
private:
    void serverThreadFunction();
    void acceptConnections();
    void handleHttpRequest(juce::StreamingSocket& socket, const juce::String& request);
    void handleWebSocketUpgrade(juce::StreamingSocket& socket, const juce::String& request);
    void processWebSocketFrame(WebSocketConnection& connection, const uint8_t* data, size_t length);
    void cleanupInactiveConnections();
    
    juce::String generateWebSocketKey(const juce::String& clientKey);
    juce::String generateRoomId();
    bool parseHttpRequest(const juce::String& request, juce::StringPairArray& headers, juce::String& path);
    
    std::atomic<bool> serverRunning{false};
    std::atomic<double> bandwidthUsage{0.0};
    std::atomic<int> nextClientId{1};
    
    std::unique_ptr<std::thread> serverThread;
    std::unique_ptr<std::thread> acceptThread;
    std::unique_ptr<juce::StreamingSocket> serverSocket;
    
    StreamSettings currentSettings;
    juce::CriticalSection settingsLock;
    
    // Active WebSocket connections
    std::vector<std::unique_ptr<WebSocketConnection>> connections;
    juce::CriticalSection connectionsLock;
    
    // Audio packet queue for real-time streaming
    juce::AbstractFifo audioFifo{1000}; // 1000 packet buffer
    std::array<AudioPacket, 1000> audioPackets;
    
    // Bandwidth monitoring
    std::atomic<int64_t> bytesTransferred{0};
    std::chrono::steady_clock::time_point lastBandwidthUpdate;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StreamingServer)
};

//==============================================================================
/** Audio level meters component */
class AudioMeter : public juce::Component, private juce::Timer
{
public:
    AudioMeter();
    
    void setLevels(const MeterLevels& levels);
    void reset();
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void startMetering();
    void stopMetering();
    
private:
    void timerCallback() override;
    void paintMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float rms, float peak, const juce::String& label);
    
    MeterLevels currentLevels;
    juce::CriticalSection levelsLock;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioMeter)
};

//==============================================================================
/** Stream control panel component */
class StreamControlPanel : public juce::Component, 
                          public juce::Button::Listener
{
public:
    StreamControlPanel();
    
    void setStreamSettings(const StreamSettings& settings);
    void setConnectionStatus(bool connected);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void buttonClicked(juce::Button* button) override;
    
    std::function<void()> onStartStream;
    std::function<void()> onStopStream;
    std::function<void()> onCopyUrl;
    std::function<void()> onShowQR;
    
private:
    void updateUI();
    
    StreamSettings currentSettings;
    bool isConnected = false;
    
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Label statusLabel;
    juce::Label roomIdLabel;
    juce::Label roomIdDisplay;
    juce::Label listenerCountLabel;
    
    juce::TextButton startButton;
    juce::TextButton stopButton;
    juce::TextButton copyUrlButton;
    juce::TextButton qrButton;
    
    juce::CriticalSection uiLock;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StreamControlPanel)
};

//==============================================================================
/** Stream settings panel component */
class StreamSettingsPanel : public juce::Component
{
public:
    StreamSettingsPanel();
    
    void setStreamSettings(const StreamSettings& settings);
    StreamSettings getStreamSettings() const;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
private:
    void updateLabels();
    
    StreamSettings currentSettings;
    
    juce::Label sampleRateLabel{"", "Sample Rate:"};
    juce::Label sampleRateValue;
    juce::Label bitDepthLabel{"", "Bit Depth:"};
    juce::Label bitDepthValue;
    juce::Label latencyLabel{"", "Latency:"};
    juce::Label latencyValue;
    juce::Label codecLabel{"", "Codec:"};
    juce::Label codecValue;
    juce::Label bandwidthLabel{"", "Bandwidth:"};
    juce::Label bandwidthValue;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StreamSettingsPanel)
};

//==============================================================================
/** Main FL Studio stream interface component */
class FLStreamInterface : public juce::Component, private juce::Timer
{
public:
    FLStreamInterface();
    ~FLStreamInterface() override;
    
    void setProcessor(class FLStreamProcessor* proc);
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void startStreaming();
    void stopStreaming();
    bool isStreaming() const { return streaming.load(); }
    
    void updateMeterLevels(const MeterLevels& levels);
    void updateStreamSettings(const StreamSettings& settings);
    
private:
    void timerCallback() override;
    void setupCallbacks();
    void copyStreamUrl();
    void showQRCode();
    
    FLStreamProcessor* processor = nullptr;
    std::atomic<bool> streaming{false};
    
    StreamControlPanel controlPanel;
    AudioMeter audioMeter;
    StreamSettingsPanel settingsPanel;
    
    std::unique_ptr<StreamingServer> server;
    StreamSettings currentSettings;
    
    juce::CriticalSection dataLock;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamInterface)
};

//==============================================================================
/** The audio processor that captures FL Studio output */
class FLStreamProcessor : public juce::AudioProcessor
{
public:
    FLStreamProcessor();
    ~FLStreamProcessor() override;
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    
    const juce::String getName() const override { return "FL Stream Pro"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    
    // Stream interface
    const MeterLevels& getCurrentMeterLevels() const { return currentLevels; }
    const StreamSettings& getStreamSettings() const { return streamSettings; }
    void setStreamSettings(const StreamSettings& settings);
    
    StreamingFifo& getStreamingFifo() { return streamingFifo; }
    
private:
    void updateMeterLevels(const juce::AudioBuffer<float>& buffer);
    
    StreamingFifo streamingFifo;
    MeterLevels currentLevels;
    StreamSettings streamSettings;
    
    juce::CriticalSection levelsLock;
    juce::CriticalSection settingsLock;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamProcessor)
};

//==============================================================================
/** The audio processor editor (plugin UI) */
class FLStreamEditor : public juce::AudioProcessorEditor
{
public:
    explicit FLStreamEditor(FLStreamProcessor& p);
    ~FLStreamEditor() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
private:
    FLStreamProcessor& audioProcessor;
    FLStreamInterface streamInterface;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamEditor)
};

//==============================================================================
/** Standalone application component */
class StandaloneFLStream : public juce::AudioAppComponent
{
public:
    StandaloneFLStream();
    ~StandaloneFLStream() override;
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
private:
    void processAudioInput(const juce::AudioSourceChannelInfo& bufferToFill);
    
    FLStreamInterface streamInterface;
    MeterLevels meterLevels;
    StreamSettings streamSettings;
    StreamingFifo streamingFifo;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneFLStream)
};

//==============================================================================
// Global functions for plugin instantiation
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();