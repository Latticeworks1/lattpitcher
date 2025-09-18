#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Source/FLStreamPlugin.h"

//==============================================================================
class FLStreamApplication : public juce::JUCEApplication
{
public:
    //==============================================================================
    FLStreamApplication() = default;

    const juce::String getApplicationName() override       { return "FL Stream"; }
    const juce::String getApplicationVersion() override    { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    //==============================================================================
    void initialise (const juce::String&) override
    {
        // Create main window with FL Stream interface
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset(); // Clean shutdown
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        // Handle multiple instances if needed
    }

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                            juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour (juce::ResizableWindow::backgroundColourId),
                            DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new StandaloneFLStream(), true);
            
            #if JUCE_IOS || JUCE_ANDROID
                setFullScreen (true);
            #else
                setResizable (false, false);
                centreWithSize (getWidth(), getHeight());
            #endif
            
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            // Clean shutdown when window is closed
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (FLStreamApplication)