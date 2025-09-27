#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "FLStreamProcessor.h"

using namespace juce;

//==============================================================================
/** General Web Browser for FL Stream Plugin */
struct FLStreamWebView : WebBrowserComponent
{
    FLStreamWebView(TextEditor& addressBox) 
        : WebBrowserComponent(WebBrowserComponent::Options{}
            .withBackend(WebBrowserComponent::Options::Backend::defaultBackend)),
          addressTextBox(addressBox) {}

    // Update address bar when navigating
    bool pageAboutToLoad(const String& newURL) override
    {
        addressTextBox.setText(newURL, false);
        return true; // Allow all navigation
    }

    // Handle new window requests
    void newWindowAttemptingToLoad(const String& newURL) override
    {
        goToURL(newURL); // Load in same window
    }

private:
    TextEditor& addressTextBox;
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

    // Browser navigation components
    TextEditor addressTextBox;
    TextButton goButton{"Go", "Go to URL"};
    TextButton backButton{"<<", "Back"};
    TextButton forwardButton{">>", "Forward"};
    TextButton homeButton{"Home", "FL Stream Voice Chat"};

    // Main WebView component - general browser
    std::unique_ptr<FLStreamWebView> webComponent;
    
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