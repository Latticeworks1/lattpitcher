#include "Source/StandaloneApp.h"

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
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (PitchDetectorApplication)