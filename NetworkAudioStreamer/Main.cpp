#include <juce_gui_basics/juce_gui_basics.h>
#include "Source/StandaloneApp.h"

using namespace juce;

//==============================================================================
class NetworkAudioStreamerApplication : public JUCEApplication
{
public:
    NetworkAudioStreamerApplication() {}

    const String getApplicationName() override       { return "Network Audio Streamer"; }
    const String getApplicationVersion() override    { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override       { return true; }

    void initialise(const String& commandLine) override
    {
        // Create main window
        mainWindow.reset(new MainWindow(getApplicationName()));
        
        // Parse command line arguments for automated testing
        if (commandLine.contains("--server"))
        {
            // Start in server mode
            if (auto* app = dynamic_cast<StandaloneNetworkApp*>(mainWindow->getContentComponent()))
                app->setModeFromCommandLine("server");
        }
        else if (commandLine.contains("--client"))
        {
            // Start in client mode with optional server address
            auto serverAddr = commandLine.fromFirstOccurrenceOf("--server-addr=", false, false);
            if (serverAddr.isEmpty()) serverAddr = "127.0.0.1";
            
            if (auto* app = dynamic_cast<StandaloneNetworkApp*>(mainWindow->getContentComponent()))
                app->setModeFromCommandLine("client", serverAddr);
        }
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const String& commandLine) override
    {
        // Allow multiple instances for testing multiple FL Studio connections
    }

    //==============================================================================
    class MainWindow : public DocumentWindow
    {
    public:
        MainWindow(String name)
            : DocumentWindow(name,
                           Desktop::getInstance().getDefaultLookAndFeel()
                                                 .findColour(ResizableWindow::backgroundColourId),
                           DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new StandaloneNetworkApp(), true);
            
#if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
#else
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
#endif

            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION(NetworkAudioStreamerApplication)