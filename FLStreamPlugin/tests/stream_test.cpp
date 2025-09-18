#include <juce_core/juce_core.h>
#include <iostream>
#include "../Source/FLStreamPlugin.h"

//==============================================================================
/** Simple test for FL Stream functionality */
int main(int argc, char* argv[])
{
    juce::ignoreUnused(argc, argv);
    
    std::cout << "FL Stream Plugin Test" << std::endl;
    std::cout << "=====================" << std::endl;
    
    // Test stream settings
    StreamSettings settings;
    settings.roomId = "FLTEST";
    settings.sampleRate = 48000.0;
    settings.bitDepth = 24;
    settings.channels = 2;
    
    std::cout << "✓ StreamSettings created successfully" << std::endl;
    
    // Test meter levels
    MeterLevels levels;
    levels.leftRMS = 0.5f;
    levels.rightRMS = 0.4f;
    levels.leftPeak = 0.8f;
    levels.rightPeak = 0.7f;
    levels.overallPeak = 0.8f;
    
    std::cout << "✓ MeterLevels created successfully" << std::endl;
    
    // Test streaming FIFO
    StreamingFifo fifo;
    std::cout << "✓ StreamingFifo created successfully" << std::endl;
    std::cout << "  Available samples: " << fifo.getNumSamplesAvailable() << std::endl;
    
    // Test audio processor creation
    try 
    {
        auto processor = std::make_unique<FLStreamProcessor>();
        std::cout << "✓ FLStreamProcessor created successfully" << std::endl;
        std::cout << "  Plugin name: " << processor->getName() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cout << "✗ FLStreamProcessor failed: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << std::endl << "All tests passed! ✓" << std::endl;
    return 0;
}