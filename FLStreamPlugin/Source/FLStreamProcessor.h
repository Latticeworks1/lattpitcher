#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include "ColyseusRoomClient.h"

using namespace juce;

// Parameter IDs
static constexpr const char* PARAM_IS_CONNECTED = "isConnected";
static constexpr const char* PARAM_IS_TALKING = "isTalking";
static constexpr const char* PARAM_ROOM_VOLUME = "roomVolume";
static constexpr const char* PARAM_MUTE = "mute";

//==============================================================================
/** FL Stream Processor - Colyseus Room Management */
class FLStreamProcessor : public AudioProcessor
{
public:
    FLStreamProcessor();
    
    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override;
    
    //==============================================================================
    const String getName() const override { return "FL Stream Plugin"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    
    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}
    
    //==============================================================================
    void getStateInformation(MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    
    //==============================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    
    //==============================================================================
    // Room management interface
    bool joinRoom(const String& roomName, const String& serverUrl = "https://voice.latticeworks-ai.com");
    void leaveRoom();
    bool isRoomConnected() const;
    String getCurrentRoomName() const { return currentRoomName; }
    String getServerAddress() const { return serverAddress; }
    
    // Enhanced status methods for detailed GUI feedback
    String getConnectionStatusText() const;
    String getLastErrorMessage() const;
    String getLastLogMessage() const;
    bool isConnecting() const;
    
    AudioProcessorValueTreeState parameters;
    
    // Room status for WebView
    std::atomic<int> connectedUsers{0};
    std::atomic<double> audioLevel{0.0};
    SpinLock statusLock;
    
    // Enhanced connection status tracking
    enum class ConnectionState {
        Disconnected,
        Connecting,
        Matchmaking,
        EstablishingWebSocket,
        Connected,
        Error
    };
    std::atomic<ConnectionState> connectionState{ConnectionState::Disconnected};
    String lastErrorMessage;
    String lastLogMessage;
    String connectionStatusText = "Disconnected";
    mutable CriticalSection statusMutex;
    
    // Colyseus room management
    std::unique_ptr<ColyseusRoomClient> roomClient;
    
    // Room state
    String currentRoomName;
    String serverAddress = "https://voice.latticeworks-ai.com";
    mutable CriticalSection roomStateMutex;
    
    // Push-to-talk state
    std::atomic<bool> isTalking{false};
    std::atomic<bool> isPushing{false};
    
private:
    //==============================================================================
    // Private methods
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void processRoomCollaboration(AudioBuffer<float>& buffer);
    void updateAudioLevel(const AudioBuffer<float>& buffer);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamProcessor)
};