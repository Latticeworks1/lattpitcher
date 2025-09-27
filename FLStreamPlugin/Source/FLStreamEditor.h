#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "FLStreamProcessor.h"

using namespace juce;

//==============================================================================
/** WebBrowserComponent for Colyseus voice interface rendering */
struct FLStreamWebView : WebBrowserComponent
{
    FLStreamWebView() 
        : WebBrowserComponent(WebBrowserComponent::Options{}
            .withBackend(WebBrowserComponent::Options::Backend::defaultBackend)
            .withWinWebView2Options(WebBrowserComponent::Options::WinWebView2{})) {}

    // Override pageAboutToLoad for voice.latticeworks-ai.com URL validation
    bool pageAboutToLoad(const String& newURL) override
    {
        juce::ignoreUnused(newURL);
        return true; // Allow all navigation for now
    }

    // Redirect new window requests to current WebView instance
    void newWindowAttemptingToLoad(const String& newURL) override
    {
        MessageManager::callAsync([this, newURL]() {
            goToURL(newURL);
        });
    }

    // Override pageFinishedLoading to inject FL Stream CSS theme
    void pageFinishedLoading(const String& url) override
    {
        std::cout << "FL Stream: Page loaded successfully: " << url << std::endl;
        
        // Apply FL Stream branded CSS via evaluateJavascript
        String customCSS = R"(
            /* FL Stream branded interface theme for Colyseus WebView */
            body { 
                background: linear-gradient(135deg, #1e1e2e, #2d3748) !important;
                font-family: 'SF Pro Display', 'Segoe UI', system-ui !important;
            }
            .container {
                background: rgba(45, 55, 72, 0.9) !important;
                border-radius: 12px !important;
                box-shadow: 0 8px 32px rgba(0,0,0,0.3) !important;
                backdrop-filter: blur(10px) !important;
            }
            #btn-talk {
                background: linear-gradient(45deg, #667eea, #764ba2) !important;
                border: none !important;
                border-radius: 25px !important;
                box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4) !important;
                transition: all 0.3s ease !important;
            }
            #btn-talk:hover {
                transform: translateY(-2px) !important;
                box-shadow: 0 6px 20px rgba(102, 126, 234, 0.6) !important;
            }
            #btn-talk.talking {
                background: linear-gradient(45deg, #ff6b6b, #ee5a24) !important;
                animation: pulse 1.5s infinite !important;
            }
            @keyframes pulse {
                0% { transform: scale(1); }
                50% { transform: scale(1.05); }
                100% { transform: scale(1); }
            }
        )";
        
        // Use JUCE's evaluateJavascript for custom styling
        evaluateJavascript("(function() { "
                          "var style = document.createElement('style'); "
                          "style.textContent = `" + customCSS + "`; "
                          "document.head.appendChild(style); "
                          "})();");
    }
};

//==============================================================================
/** AudioProcessorEditor with WebView and native UI for Colyseus voice chat */
class FLStreamEditor : public AudioProcessorEditor, public Timer
{
public:
    explicit FLStreamEditor(FLStreamProcessor& processor);

    //==============================================================================
    void paint(Graphics&) override;
    void resized() override;
    void timerCallback() override;

    int getControlParameterIndex(Component&) override
    {
        return -1; // WebBrowserComponent requires no parameter automation
    }

    void loadFLStreamHome();

private:
    FLStreamProcessor& processorRef;

    // WebView configured for voice.latticeworks-ai.com without navigation UI

    // UI Mode Selection
    enum class UIMode { WebView, Native };
    UIMode currentUIMode = UIMode::Native;

    // WebView components  
    std::unique_ptr<FLStreamWebView> webComponent;
    
    // JUCE components for standalone application voice controls
    std::unique_ptr<TextButton> talkButton;
    std::unique_ptr<Label> statusLabel;
    std::unique_ptr<Label> connectionStatusLabel;
    std::unique_ptr<Label> roomDisplayLabel;
    std::unique_ptr<Label> playersLabel;
    std::unique_ptr<Component> playerListContainer;
    std::unique_ptr<Slider> volumeSlider;
    std::unique_ptr<ToggleButton> muteButton;
    
    // Player list management
    std::vector<std::unique_ptr<Label>> playerLabels;
    void updatePlayerList();
    bool isSelfTalking = false;
    
    // Interactive methods for native UI
    void startTalking();
    void stopTalking();
    void updateConnectionStatus();
    void updateRoomDisplay();
    
    // Embedded HTML for Colyseus voice interface fallback
    String flStreamHtmlContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamEditor)
};


extern const String localColouseusServerAddress;