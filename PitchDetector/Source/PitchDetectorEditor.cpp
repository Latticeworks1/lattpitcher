#include "PitchDetectorEditor.h"

//==============================================================================
PitchDetectorEditor::PitchDetectorEditor(PitchDetectorProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    addAndMakeVisible(gui);
    
    // Connect GUI callbacks
    gui.onExportTelemetry = [this] { exportTelemetryData(); };
    gui.onResetTelemetry = [this] { resetTelemetryData(); };
    
    // Sync GUI controls with processor engine
    auto& engine = audioProcessor.getEngine();
    gui.getEngine().setNoiseThreshold(engine.getNoiseThreshold());
    gui.getEngine().setFrequencyRange(engine.getMinFrequency(), engine.getMaxFrequency());
    gui.getEngine().setCorrelationThreshold(engine.getCorrelationThreshold());
    
    setSize(800, 600);
    startTimerHz(60);
}

PitchDetectorEditor::~PitchDetectorEditor()
{
    stopTimer();
}

//==============================================================================
void PitchDetectorEditor::paint(Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void PitchDetectorEditor::resized()
{
    gui.setBounds(getLocalBounds());
}

//==============================================================================
void PitchDetectorEditor::timerCallback()
{
    // Get latest detection results from processor
    auto result = audioProcessor.getLatestResult();
    
    if (result.hasNewData) {
        gui.updatePitchDisplay(result.frequency, result.noteInfo);
        
        // Sync engine parameters from GUI to processor
        auto& processorEngine = audioProcessor.getEngine();
        auto& guiEngine = gui.getEngine();
        
        processorEngine.setNoiseThreshold(guiEngine.getNoiseThreshold());
        processorEngine.setFrequencyRange(guiEngine.getMinFrequency(), guiEngine.getMaxFrequency());
        processorEngine.setCorrelationThreshold(guiEngine.getCorrelationThreshold());
    }
    
    // Always update audio level
    gui.updateAudioLevel(result.audioLevel);
    
    // Update debug info
    String debugInfo = "=== PLUGIN DEBUG INFO ===\n";
    debugInfo += "Audio Processor: " + audioProcessor.getName() + "\n";
    debugInfo += "Sample Rate: " + String(audioProcessor.getSampleRate()) + " Hz\n";
    debugInfo += "Block Size: " + String(audioProcessor.getBlockSize()) + " samples\n";
    debugInfo += "Input Channels: " + String(audioProcessor.getTotalNumInputChannels()) + "\n";
    debugInfo += "Output Channels: " + String(audioProcessor.getTotalNumOutputChannels()) + "\n";
    
    const auto& telemetry = audioProcessor.getEngine().getTelemetry();
    debugInfo += "Processing Cycles: " + String(telemetry.totalProcessingCycles) + "\n";
    debugInfo += "Detection Rate: " + String(telemetry.totalDetectionAttempts > 0 ? 
                                            (float)telemetry.successfulDetections / telemetry.totalDetectionAttempts * 100.0f : 0.0f, 1) + "%\n";
    
    gui.updateDebugInfo(debugInfo);
}

//==============================================================================
void PitchDetectorEditor::exportTelemetryData()
{
    FileChooser chooser("Export Telemetry Data", 
                       File::getSpecialLocation(File::userDesktopDirectory).getChildFile("pitch_detector_telemetry.json"), 
                       "*.json");
    
    chooser.launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
                       [this](const FileChooser& fc) {
        if (fc.getURLResults().size() > 0) {
            File outputFile = fc.getURLResults()[0].getLocalFile();
            String jsonData = audioProcessor.getEngine().exportTelemetryJson();
            
            if (outputFile.replaceWithText(jsonData)) {
                AlertWindow::showMessageBoxAsync(AlertWindow::InfoIcon, "Export Complete", 
                                               "Telemetry data exported to:\n" + outputFile.getFullPathName());
            } else {
                AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Export Failed", 
                                               "Failed to export telemetry data to file.");
            }
        }
    });
}

void PitchDetectorEditor::resetTelemetryData()
{
    audioProcessor.getEngine().initializeTelemetry();
    AlertWindow::showMessageBoxAsync(AlertWindow::InfoIcon, "Telemetry Reset", 
                                   "Telemetry data has been reset.");
}