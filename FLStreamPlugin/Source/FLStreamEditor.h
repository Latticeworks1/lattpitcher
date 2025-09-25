#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "FLStreamProcessor.h"

using namespace juce;

//==============================================================================
/** Single Page WebView for FL Stream Plugin Room Management */
struct FLStreamWebView : WebBrowserComponent
{
    using WebBrowserComponent::WebBrowserComponent;

    // Prevent navigation away from our room management interface
    bool pageAboutToLoad(const String& newURL) override;
};

//==============================================================================
/** FL Stream Plugin WebView Editor - Colyseus Room Management */
class FLStreamEditor : public AudioProcessorEditor, private Timer
{
public:
    explicit FLStreamEditor(FLStreamProcessor& processor);

    //==============================================================================
    void paint(Graphics&) override;
    void resized() override;

    int getControlParameterIndex(Component&) override
    {
        return controlParameterIndexReceiver.getControlParameterIndex();
    }

    void timerCallback() override;

    std::optional<WebBrowserComponent::Resource> getResource(const String& url);

private:
    FLStreamProcessor& processorRef;

    // Minimal WebView relays
    WebSliderRelay roomVolumeRelay{"roomVolumeSlider"};
    WebToggleButtonRelay muteToggleRelay{"muteToggle"};
    WebToggleButtonRelay talkButtonRelay{"talkButton"};
    
    WebControlParameterIndexReceiver controlParameterIndexReceiver;

    // Main WebView component with room management interface
    FLStreamWebView webComponent{
        WebBrowserComponent::Options{}
            .withBackend(WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options(WebBrowserComponent::Options::WinWebView2{}
                .withUserDataFolder(File::getSpecialLocation(File::SpecialLocationType::tempDirectory)))
            .withNativeIntegrationEnabled()
            .withOptionsFrom(roomVolumeRelay)
            .withOptionsFrom(muteToggleRelay)
            .withOptionsFrom(talkButtonRelay)
            .withOptionsFrom(controlParameterIndexReceiver)
            .withNativeFunction("joinRoom", [this](auto& var, auto complete)
            {
                String roomName = var[0].toString();
                String serverAddress = var[1].toString();
                
                bool success = processorRef.joinRoom(roomName, serverAddress);
                if (success)
                {
                    complete("Successfully joined room: " + roomName);
                }
                else
                {
                    complete("Failed to join room: " + roomName);
                }
            })
            .withNativeFunction("leaveRoom", [this](auto&, auto complete)
            {
                processorRef.leaveRoom();
                complete("Left room");
            })
            .withNativeFunction("getStatus", [this](auto&, auto complete)
            {
                DynamicObject::Ptr status(new DynamicObject());
                status->setProperty("roomName", processorRef.getCurrentRoomName());
                status->setProperty("serverAddress", processorRef.getServerAddress());
                status->setProperty("isConnected", processorRef.isRoomConnected());
                status->setProperty("roomEnabled", processorRef.isRoomConnected());
                status->setProperty("connectedUsers", processorRef.connectedUsers.load());
                status->setProperty("audioLevel", processorRef.audioLevel.load());
                
                // Enhanced status information
                status->setProperty("connectionStatus", processorRef.getConnectionStatusText());
                status->setProperty("lastError", processorRef.getLastErrorMessage());
                status->setProperty("lastLog", processorRef.getLastLogMessage());
                status->setProperty("isConnecting", processorRef.isConnecting());
                
                complete(var(status));
            })
            .withResourceProvider([this](const auto& url)
            {
                return getResource(url);
            }, URL{"http://localhost:3000/"}.getOrigin())
    };

    // Parameter attachments for VST automation
    WebSliderParameterAttachment roomVolumeAttachment;
    WebToggleButtonParameterAttachment muteAttachment;
    WebToggleButtonParameterAttachment talkButtonAttachment;

    // Real-time status data for web interface
    std::deque<Array<var>> statusFrames;

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