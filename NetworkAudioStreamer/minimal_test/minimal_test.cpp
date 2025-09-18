#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

using namespace juce;

//==============================================================================
class MinimalNetworkApp : public Component
{
public:
    MinimalNetworkApp()
    {
        setSize(400, 300);
        
        serverButton.setButtonText("Start Server");
        clientButton.setButtonText("Connect Client");
        statusLabel.setText("Ready", dontSendNotification);
        
        addAndMakeVisible(serverButton);
        addAndMakeVisible(clientButton);
        addAndMakeVisible(statusLabel);
        
        serverButton.onClick = [this] { 
            statusLabel.setText("Server Mode: Listening on port 9001", dontSendNotification);
        };
        
        clientButton.onClick = [this] { 
            statusLabel.setText("Client Mode: Connecting to server...", dontSendNotification);
        };
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(20);
        serverButton.setBounds(bounds.removeFromTop(40));
        bounds.removeFromTop(10);
        clientButton.setBounds(bounds.removeFromTop(40));
        bounds.removeFromTop(10);
        statusLabel.setBounds(bounds.removeFromTop(40));
    }

private:
    TextButton serverButton, clientButton;
    Label statusLabel;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MinimalNetworkApp)
};

//==============================================================================
class MinimalNetworkApplication : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Minimal Network Audio Streamer"; }
    const String getApplicationVersion() override { return "1.0.0"; }

    void initialise(const String&) override
    {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override { mainWindow = nullptr; }

    class MainWindow : public DocumentWindow
    {
    public:
        MainWindow(String name) : DocumentWindow(name, Desktop::getInstance().getDefaultLookAndFeel()
                                                        .findColour(ResizableWindow::backgroundColourId),
                                                DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MinimalNetworkApp(), true);
            centreWithSize(getWidth(), getHeight());
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

START_JUCE_APPLICATION(MinimalNetworkApplication)