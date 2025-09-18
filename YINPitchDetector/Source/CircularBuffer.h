#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>

//==============================================================================
// CircularBuffer implementation based on JUCE example
class CircularBuffer
{
public:
    CircularBuffer (int numChannels, int numSamples)
        : data(numChannels * numSamples, 0.0f)
    {
        // Create channel pointers for AudioBlock
        channelPointers.resize(numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
            channelPointers[ch] = data.data() + ch * numSamples;
        
        buffer = juce::dsp::AudioBlock<float>(channelPointers.data(), (size_t)numChannels, (size_t)numSamples);
    }

    template <typename T>
    void push (juce::dsp::AudioBlock<T> b)
    {
        jassert (b.getNumChannels() == buffer.getNumChannels());

        const auto trimmed = b.getSubBlock (  b.getNumSamples()
                                            - std::min (b.getNumSamples(), buffer.getNumSamples()));

        const auto bufferLength = (juce::int64) buffer.getNumSamples();

        for (auto samplesRemaining = (juce::int64) trimmed.getNumSamples(); samplesRemaining > 0;)
        {
            const auto writeOffset = writeIx % bufferLength;
            const auto numSamplesToWrite = std::min (samplesRemaining, bufferLength - writeOffset);

            auto srcBlock = trimmed.getSubBlock ((juce::int64) trimmed.getNumSamples() - samplesRemaining, (size_t) numSamplesToWrite);
            auto dstBlock = buffer.getSubBlock ((size_t) writeOffset, (size_t) numSamplesToWrite);
            
            // Copy sample by sample for compatibility
            for (size_t ch = 0; ch < srcBlock.getNumChannels(); ++ch)
            {
                for (size_t i = 0; i < numSamplesToWrite; ++i)
                {
                    dstBlock.getChannelPointer(ch)[i] = srcBlock.getChannelPointer(ch)[i];
                }
            }

            samplesRemaining -= numSamplesToWrite;
            writeIx += numSamplesToWrite;
        }
    }

    template <typename T>
    juce::dsp::AudioBlock<T> getLatest (juce::dsp::AudioBlock<T> output)
    {
        jassert (output.getNumChannels() == buffer.getNumChannels());

        const auto bufferLength = (juce::int64) buffer.getNumSamples();
        const auto outputLength = (juce::int64) output.getNumSamples();

        for (auto samplesRemaining = outputLength; samplesRemaining > 0;)
        {
            const auto readOffset = (writeIx - outputLength + samplesRemaining) % bufferLength;
            const auto numSamplesToRead = std::min (samplesRemaining, bufferLength - readOffset);

            auto srcBlock = buffer.getSubBlock ((size_t) readOffset, (size_t) numSamplesToRead);
            auto dstBlock = output.getSubBlock ((size_t) (outputLength - samplesRemaining), (size_t) numSamplesToRead);
            
            // Copy sample by sample for compatibility
            for (size_t ch = 0; ch < srcBlock.getNumChannels(); ++ch)
            {
                for (size_t i = 0; i < numSamplesToRead; ++i)
                {
                    dstBlock.getChannelPointer(ch)[i] = srcBlock.getChannelPointer(ch)[i];
                }
            }

            samplesRemaining -= numSamplesToRead;
        }

        return output;
    }

    size_t getNumSamples() const { return buffer.getNumSamples(); }
    size_t getNumChannels() const { return buffer.getNumChannels(); }

private:
    std::vector<float> data;
    std::vector<float*> channelPointers;
    juce::dsp::AudioBlock<float> buffer;
    juce::int64 writeIx = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CircularBuffer)
};