#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <atomic>
#include "AudioPacket.h"

// Forward declarations
class NetworkAudioProcessor;

using namespace juce;

class NetworkManager {
public:
    enum class Mode { Inactive, Server, Client };
    enum class Status { Disconnected, Connecting, Connected, Error };
    
    NetworkManager();
    ~NetworkManager();
    
    bool startServer(int port);
    bool startClient(const String& address, int port);
    void stop();
    
    bool sendPacket(std::shared_ptr<AudioPacket> packet);
    std::shared_ptr<AudioPacket> receivePacket();
    
    Mode getMode() const { return mode_.load(); }
    Status getStatus() const { return status_.load(); }
    int getConnectedClients() const { return clientCount_.load(); }
    
    String getLastError() const;
    
    // Web interface
    bool startWebInterface(int webPort);
    void stopWebInterface();
    void setAudioProcessor(NetworkAudioProcessor* processor) { audioProcessor_ = processor; }

    struct Impl;
    std::unique_ptr<Impl> pImpl_;

private:
    std::atomic<Mode> mode_{Mode::Inactive};
    std::atomic<Status> status_{Status::Disconnected};
    std::atomic<int> clientCount_{0};
    
public:
    NetworkAudioProcessor* audioProcessor_{nullptr};
private:
    
    CriticalSection errorLock_;
    String lastError_;
    
    void setError(const String& error);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkManager)
};