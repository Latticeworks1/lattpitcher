#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "FLStreamProcessor.h"

using namespace juce;

//==============================================================================
/** FL Stream Voice Chat WebView */
struct FLStreamWebView : WebBrowserComponent
{
    FLStreamWebView() 
        : WebBrowserComponent(WebBrowserComponent::Options{}
            .withBackend(WebBrowserComponent::Options::Backend::defaultBackend)
            .withWinWebView2Options(WebBrowserComponent::Options::WinWebView2{})) {}

    // Allow navigation to voice.latticeworks-ai.com only
    bool pageAboutToLoad(const String& newURL) override
    {
        juce::ignoreUnused(newURL);
        return true; // Allow all navigation for now
    }

    // Handle new window requests
    void newWindowAttemptingToLoad(const String& newURL) override
    {
        MessageManager::callAsync([this, newURL]() {
            goToURL(newURL);
        });
    }

    // Handle page load completion and inject custom styling
    void pageFinishedLoading(const String& url) override
    {
        std::cout << "FL Stream: Page loaded successfully: " << url << std::endl;
        
        // Inject custom CSS for reskinning
        String customCSS = R"(
            /* Custom FL Stream Plugin Skin */
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
/** FL Stream Plugin WebView Editor - Colyseus Room Management */
class FLStreamEditor : public AudioProcessorEditor
{
public:
    explicit FLStreamEditor(FLStreamProcessor& processor);

    //==============================================================================
    void paint(Graphics&) override;
    void resized() override;

    int getControlParameterIndex(Component&) override
    {
        return -1; // No parameter control mapping for web browser
    }

    std::optional<WebBrowserComponent::Resource> getResource(const String& url);
    void loadFLStreamHome();

private:
    FLStreamProcessor& processorRef;

    // Navigation controls hidden - direct access to voice.latticeworks-ai.com only

    // UI Mode Selection
    enum class UIMode { WebView, Native };
    UIMode currentUIMode = UIMode::Native;

    // WebView components  
    std::unique_ptr<FLStreamWebView> webComponent;
    
    // Native UI components (alternative to WebView)
    std::unique_ptr<TextButton> talkButton;
    std::unique_ptr<Label> statusLabel;
    std::unique_ptr<Slider> volumeSlider;
    std::unique_ptr<ToggleButton> muteButton;
    
    // Default FL Stream Voice Chat HTML content
    String flStreamHtmlContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLStreamEditor)
};

//==============================================================================
/** Resource provider for embedded web interface */
static ZipFile* getFLStreamWebAssets()
{
    // No embedded assets - will use fallback HTML
    return nullptr;
}

static const char* getMimeForExtension(const String& extension)
{
    static const std::unordered_map<String, const char*> mimeMap = {
        {{"htm"},   "text/html"},
        {{"html"},  "text/html"},
        {{"txt"},   "text/plain"},
        {{"jpg"},   "image/jpeg"},
        {{"jpeg"},  "image/jpeg"},
        {{"svg"},   "image/svg+xml"},
        {{"ico"},   "image/vnd.microsoft.icon"},
        {{"json"},  "application/json"},
        {{"png"},   "image/png"},
        {{"css"},   "text/css"},
        {{"map"},   "application/json"},
        {{"js"},    "text/javascript"},
        {{"woff2"}, "font/woff2"}
    };

    if (const auto it = mimeMap.find(extension.toLowerCase()); it != mimeMap.end())
        return it->second;

    return "text/plain";
}

static String getExtension(String filename)
{
    return filename.fromLastOccurrenceOf(".", false, false);
}

static auto streamToVector(InputStream& stream)
{
    std::vector<std::byte> result((size_t)stream.getTotalLength());
    stream.setPosition(0);
    [[maybe_unused]] const auto bytesRead = stream.read(result.data(), result.size());
    jassert(bytesRead == (ssize_t)result.size());
    return result;
}

extern const String localColouseusServerAddress;