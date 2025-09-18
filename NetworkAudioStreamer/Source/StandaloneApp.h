#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "NetworkAudioProcessor.h"

using namespace juce;

class StandaloneNetworkApp : public AudioAppComponent
{
public:
    StandaloneNetworkApp()
    {
        processor = std::make_unique<NetworkAudioProcessor>();
        setSize(400, 300);
        
        // Set up audio
        setAudioChannels(2, 2);
    }
    
    ~StandaloneNetworkApp() override
    {
        shutdownAudio();
    }
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        processor->prepareToPlay(sampleRate, samplesPerBlockExpected);
    }
    
    void getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill) override
    {
        MidiBuffer midiBuffer;
        processor->processBlock(*bufferToFill.buffer, midiBuffer);
    }
    
    void releaseResources() override
    {
        processor->releaseResources();
    }
    
    void paint(Graphics& g) override
    {
        g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
        g.setColour(Colours::white);
        g.drawText("NetworkAudioStreamer Standalone", 
                  getLocalBounds(), Justification::centred, true);
    }
    
    void setModeFromCommandLine(const String& mode, const String& serverAddr = "127.0.0.1")
    {
        if (processor)
        {
            auto& params = processor->getParameters();
            if (mode == "server")
            {
                if (auto* modeParam = dynamic_cast<AudioParameterChoice*>(params.getParameter("mode")))
                    *modeParam = 1; // Server mode
            }
            else if (mode == "client")
            {
                if (auto* modeParam = dynamic_cast<AudioParameterChoice*>(params.getParameter("mode")))
                    *modeParam = 2; // Client mode
            }
        }
    }
    
private:
    std::unique_ptr<NetworkAudioProcessor> processor;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneNetworkApp)
};