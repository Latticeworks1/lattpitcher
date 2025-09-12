#include "Source/PitchDetector.h"

//==============================================================================
class PitchDetectorApplication : public JUCEApplication
{
public:
    //==============================================================================
    PitchDetectorApplication() = default;

    const String getApplicationName() override       { return "Pitch Detector"; }
    const String getApplicationVersion() override    { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override       { return true; }

    //==============================================================================
    void initialise (const String& commandLine) override
    {
        // Set up a rolling file logger early to capture crashes in the field
        auto logsDir = File::getSpecialLocation(File::userApplicationDataDirectory)
                            .getChildFile("PitchDetector/Logs");
        logsDir.createDirectory();
        auto logFile = logsDir.getChildFile("PitchDetector.log");
        fileLogger.reset(FileLogger::createDefaultAppLogger(logsDir.getFullPathName(),
                                                            logFile.getFileName(),
                                                            "[PitchDetector] Starting app"));
        Logger::writeToLog("JUCE version: " + String(JUCE_MAJOR_VERSION) + "." + String(JUCE_MINOR_VERSION) + "." + String(JUCE_BUILDNUMBER));
        Logger::writeToLog("Command line: " + commandLine);
        
        ignoreUnused (commandLine);
        
        mainWindow.reset (new MainWindow (getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const String& commandLine) override
    {
        ignoreUnused (commandLine);
    }

    //==============================================================================
    class MainWindow : public DocumentWindow
    {
    public:
        MainWindow (String name)
          : DocumentWindow (name,
                            Desktop::getInstance().getDefaultLookAndFeel()
                                                   .findColour (ResizableWindow::backgroundColourId),
                            DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new StandalonePitchDetector(), true);

            #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
            #else
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<FileLogger> fileLogger;
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (PitchDetectorApplication)
