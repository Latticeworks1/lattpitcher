#include "ColyseusRoomClient.h"

#if JUCE_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
#endif

//==============================================================================
/** Simple WebSocket implementation for Colyseus */
struct ColyseusRoomClient::WebSocketConnection
{
    WebSocketConnection(ColyseusRoomClient* parent) : roomClient(parent) {};
    ~WebSocketConnection() { disconnect(); }

    bool connect(const String& url)
    {
        // Parse URL
        URL serverUrl(url);
        String host = serverUrl.getDomain();
        int port = serverUrl.getPort();
        
        if (port == 0)
            port = url.startsWith("wss://") ? 443 : 80;

        // Create socket
        #if JUCE_WINDOWS
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            return false;
        
        socketHandle = socket(AF_INET, SOCK_STREAM, 0);
        if (socketHandle == INVALID_SOCKET)
        {
            WSACleanup();
            return false;
        }
        #else
        socketHandle = socket(AF_INET, SOCK_STREAM, 0);
        if (socketHandle < 0)
            return false;
        #endif

        // Resolve hostname
        struct hostent* server = gethostbyname(host.toUTF8());
        if (server == nullptr)
        {
            disconnect();
            return false;
        }

        // Connect
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);

        #if JUCE_WINDOWS
        if (::connect(socketHandle, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        #else
        if (::connect(socketHandle, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
        #endif
        {
            disconnect();
            return false;
        }

        // Send WebSocket handshake (connect directly to domain like working FL client)
        String handshake = 
            "GET / HTTP/1.1\r\n"
            "Host: " + host + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";

        if (send(handshake.toUTF8(), handshake.getNumBytesAsUTF8()) != handshake.getNumBytesAsUTF8())
        {
            disconnect();
            return false;
        }

        // Read handshake response
        char buffer[1024];
        ssize_t bytesRead = recv(buffer, sizeof(buffer) - 1);
        if (bytesRead <= 0)
        {
            disconnect();
            return false;
        }
        
        buffer[bytesRead] = '\0';
        String response(buffer);
        
        if (!response.contains("HTTP/1.1 101"))
        {
            disconnect();
            return false;
        }

        isConnected = true;
        return true;
    }

    void disconnect()
    {
        isConnected = false;
        
        #if JUCE_WINDOWS
        if (socketHandle != INVALID_SOCKET)
        {
            closesocket(socketHandle);
            socketHandle = INVALID_SOCKET;
            WSACleanup();
        }
        #else
        if (socketHandle >= 0)
        {
            close(socketHandle);
            socketHandle = -1;
        }
        #endif
    }

    bool sendMessage(const String& message)
    {
        if (!isConnected)
            return false;

        // Create WebSocket frame
        std::vector<uint8_t> frame;
        frame.push_back(0x81); // Text frame, FIN=1

        auto messageBytes = message.toUTF8();
        size_t messageLen = strlen(messageBytes);

        if (messageLen < 126)
        {
            frame.push_back(0x80 | static_cast<uint8_t>(messageLen)); // Masked, length
        }
        else if (messageLen < 65536)
        {
            frame.push_back(0x80 | 126);
            frame.push_back((messageLen >> 8) & 0xFF);
            frame.push_back(messageLen & 0xFF);
        }
        else
        {
            frame.push_back(0x80 | 127);
            for (int i = 7; i >= 0; --i)
                frame.push_back((messageLen >> (i * 8)) & 0xFF);
        }

        // Masking key
        uint8_t maskingKey[4] = {0x12, 0x34, 0x56, 0x78};
        for (int i = 0; i < 4; ++i)
            frame.push_back(maskingKey[i]);

        // Masked payload
        for (size_t i = 0; i < messageLen; ++i)
            frame.push_back(messageBytes[i] ^ maskingKey[i % 4]);

        return send(frame.data(), frame.size()) == frame.size();
    }

    bool sendBinaryFrame(const std::vector<uint8_t>& frame)
    {
        if (!isConnected)
            return false;
        return send(frame.data(), frame.size()) == frame.size();
    }

    String receiveMessage()
    {
        if (!isConnected)
            return {};

        uint8_t header[2];
        if (recv(header, 2) != 2)
            return {};

        uint8_t opcode = header[0] & 0x0F;
        
        // Handle both text (0x01) and binary (0x02) frames for Colyseus
        if (opcode == 0x02) // Binary frame - Colyseus audio data
        {
            handleBinaryFrame();
            return {}; // Binary frames don't return text
        }
        else if (opcode != 0x01) // Text frame
            return {};

        uint64_t payloadLen = header[1] & 0x7F;
        
        if (payloadLen == 126)
        {
            uint8_t lenBytes[2];
            if (recv(lenBytes, 2) != 2)
                return {};
            payloadLen = (lenBytes[0] << 8) | lenBytes[1];
        }
        else if (payloadLen == 127)
        {
            uint8_t lenBytes[8];
            if (recv(lenBytes, 8) != 8)
                return {};
            payloadLen = 0;
            for (int i = 0; i < 8; ++i)
                payloadLen = (payloadLen << 8) | lenBytes[i];
        }

        if (payloadLen > 1024 * 1024) // 1MB limit
            return {};

        std::vector<char> payload(payloadLen + 1);
        if (recv(payload.data(), payloadLen) != payloadLen)
            return {};
        
        payload[payloadLen] = '\0';
        return String(payload.data());
    }

    void handleBinaryFrame()
    {
        uint8_t header[1];
        if (recv(header, 1) != 1)
            return;

        uint64_t payloadLen = header[0] & 0x7F;
        if (payloadLen == 126)
        {
            uint8_t lenBytes[2];
            if (recv(lenBytes, 2) != 2)
                return;
            payloadLen = (lenBytes[0] << 8) | lenBytes[1];
        }

        if (payloadLen > 512 * 1024)
            return;

        std::vector<uint8_t> payload(payloadLen);
        if (recv(payload.data(), payloadLen) != payloadLen)
            return;

        // Colyseus binary message: [messageType][audioData]
        if (payloadLen > 4) // Minimum for "talk" + data
        {
            // Check if message type is "talk"
            if (payloadLen >= 4 && 
                payload[0] == 't' && payload[1] == 'a' && 
                payload[2] == 'l' && payload[3] == 'k')
            {
                // Extract audio data after "talk" header
                const uint8_t* audioData = payload.data() + 4;
                size_t audioDataSize = payloadLen - 4;
                
                // Convert to float samples
                if (audioDataSize % sizeof(float) == 0)
                {
                    const float* samples = reinterpret_cast<const float*>(audioData);
                    int numSamples = audioDataSize / sizeof(float);
                    
                    if (roomClient && roomClient->audioReceiveCallback)
                    {
                        roomClient->audioReceiveCallback(samples, numSamples, 1);
                    }
                }
            }
        }
    }

private:
    #if JUCE_WINDOWS
    SOCKET socketHandle = INVALID_SOCKET;
    #else
    int socketHandle = -1;
    #endif
    
    bool isConnected = false;
    ColyseusRoomClient* roomClient = nullptr;

    ssize_t send(const void* data, size_t len)
    {
        #if JUCE_WINDOWS
        return ::send(socketHandle, static_cast<const char*>(data), static_cast<int>(len), 0);
        #else
        return ::send(socketHandle, data, len, 0);
        #endif
    }

    ssize_t recv(void* data, size_t len)
    {
        #if JUCE_WINDOWS
        return ::recv(socketHandle, static_cast<char*>(data), static_cast<int>(len), 0);
        #else
        return ::recv(socketHandle, data, len, 0);
        #endif
    }
};

//==============================================================================
ColyseusRoomClient::ColyseusRoomClient()
    : Thread("ColyseusRoomClient"),
      wsConnection(std::make_unique<WebSocketConnection>(this))
{
}

ColyseusRoomClient::~ColyseusRoomClient()
{
    shouldStop = true;
    leaveRoom();
    
    if (isThreadRunning())
        stopThread(5000);
}

//==============================================================================
bool ColyseusRoomClient::joinRoom(const String& roomName, const String& serverUrl)
{
    const ScopedLock lock(roomStateLock);
    
    if (connected.load())
        leaveRoom();

    currentRoomName = roomName;
    this->serverUrl = serverUrl;

    shouldStop = false;
    startThread();

    // Wait for connection (with timeout)
    for (int i = 0; i < 50 && !connected.load(); ++i)
        Thread::sleep(100);

    return connected.load();
}

void ColyseusRoomClient::leaveRoom()
{
    if (connected.load())
    {
        sendMessage(ColyseusProtocol::createLeaveRoomMessage());
        Thread::sleep(100); // Give time for message to send
    }

    connected = false;
    wsConnection->disconnect();

    if (onRoomLeft)
        onRoomLeft(currentRoomName);

    const ScopedLock lock(roomStateLock);
    currentRoomName = "";
}

String ColyseusRoomClient::getCurrentRoom() const
{
    const ScopedLock lock(roomStateLock);
    return currentRoomName;
}

//==============================================================================
void ColyseusRoomClient::sendAudioData(const float* audioData, int numSamples, int numChannels)
{
    if (connected.load() && wsConnection)
    {
        // Send binary audio data using Colyseus sendBytes protocol
        size_t audioDataSize = numSamples * numChannels * sizeof(float);
        
        // Create binary message frame for WebSocket
        std::vector<uint8_t> frame;
        frame.push_back(0x82); // Binary frame, FIN=1
        
        // Add message type "talk" (4 bytes) + audio data
        String messageType = "talk";
        size_t totalSize = messageType.length() + audioDataSize;
        
        if (totalSize < 126)
        {
            frame.push_back(0x80 | static_cast<uint8_t>(totalSize));
        }
        else if (totalSize < 65536)
        {
            frame.push_back(0x80 | 126);
            frame.push_back((totalSize >> 8) & 0xFF);
            frame.push_back(totalSize & 0xFF);
        }
        
        // Masking key
        uint8_t maskingKey[4] = {0x12, 0x34, 0x56, 0x78};
        for (int i = 0; i < 4; ++i)
            frame.push_back(maskingKey[i]);
        
        // Add message type "talk"
        for (int i = 0; i < messageType.length(); ++i)
            frame.push_back(messageType[i] ^ maskingKey[i % 4]);
        
        // Add masked audio data
        const uint8_t* audioBytes = reinterpret_cast<const uint8_t*>(audioData);
        for (size_t i = 0; i < audioDataSize; ++i)
            frame.push_back(audioBytes[i] ^ maskingKey[(messageType.length() + i) % 4]);
        
        wsConnection->sendBinaryFrame(frame);
    }
}

void ColyseusRoomClient::setAudioReceiveCallback(std::function<void(const float*, int, int)> callback)
{
    const ScopedLock lock(audioCallbackLock);
    audioReceiveCallback = std::move(callback);
}

//==============================================================================
StringArray ColyseusRoomClient::getConnectedUserList() const
{
    const ScopedLock lock(userListLock);
    return userList;
}

//==============================================================================
void ColyseusRoomClient::run()
{
    if (!wsConnection->connect(serverUrl))
    {
        if (onError)
            onError("Failed to connect to server");
        return;
    }

    // Send room join request
    sendMessage(JSON::toString(ColyseusProtocol::createJoinRoomMessage(currentRoomName)));

    // Message loop
    while (!shouldStop.load() && !threadShouldExit())
    {
        String message = wsConnection->receiveMessage();
        if (message.isNotEmpty())
        {
            handleMessage(message);
        }
        else
        {
            Thread::sleep(10);
        }
    }

    wsConnection->disconnect();
}

//==============================================================================
void ColyseusRoomClient::sendMessage(const var& message)
{
    if (wsConnection)
    {
        String jsonString = JSON::toString(message);
        wsConnection->sendMessage(jsonString);
        
        if (onLogMessage)
            onLogMessage("Sent: " + jsonString);
    }
}

void ColyseusRoomClient::handleMessage(const String& message)
{
    if (onLogMessage)
        onLogMessage("Received: " + message);

    var parsedMessage;
    Result parseResult = JSON::parse(message, parsedMessage);
    
    if (parseResult.failed())
        return;

    if (ColyseusProtocol::isRoomJoinedMessage(parsedMessage))
    {
        handleRoomJoined(parsedMessage);
    }
    else if (ColyseusProtocol::isUserJoinedMessage(parsedMessage))
    {
        handleUserJoined(parsedMessage);
    }
    else if (ColyseusProtocol::isUserLeftMessage(parsedMessage))
    {
        handleUserLeft(parsedMessage);
    }
    else if (ColyseusProtocol::isAudioDataMessage(parsedMessage))
    {
        handleAudioData(parsedMessage);
    }
    else if (ColyseusProtocol::isErrorMessage(parsedMessage))
    {
        handleError(parsedMessage);
    }
}

//==============================================================================
void ColyseusRoomClient::handleRoomJoined(const var& data)
{
    connected = true;
    
    if (onRoomJoined)
        onRoomJoined(currentRoomName);
}

void ColyseusRoomClient::handleRoomLeft(const var& data)
{
    connected = false;
    
    if (onRoomLeft)
        onRoomLeft(currentRoomName);
}

void ColyseusRoomClient::handleUserJoined(const var& data)
{
    String userId = data["userId"].toString();
    String userName = data["userName"].toString();

    {
        const ScopedLock lock(userListLock);
        userList.addIfNotAlreadyThere(userId);
        connectedUsers = userList.size();
    }

    if (onUserJoined)
        onUserJoined(userId, userName);
}

void ColyseusRoomClient::handleUserLeft(const var& data)
{
    String userId = data["userId"].toString();

    {
        const ScopedLock lock(userListLock);
        userList.removeString(userId);
        connectedUsers = userList.size();
    }

    if (onUserLeft)
        onUserLeft(userId);
}

void ColyseusRoomClient::handleAudioData(const var& data)
{
    // Handle JSON audio messages (if any)
    const ScopedLock lock(audioCallbackLock);
    
    if (audioReceiveCallback && data.hasProperty("audioData"))
    {
        String audioDataBase64 = data["audioData"].toString();
        int numSamples = data["numSamples"];
        int numChannels = data["numChannels"];
        
        if (audioDataBase64.isNotEmpty() && numSamples > 0)
        {
            MemoryBlock audioBlock;
            if (audioBlock.fromBase64Encoding(audioDataBase64))
            {
                const float* samples = static_cast<const float*>(audioBlock.getData());
                audioReceiveCallback(samples, numSamples, numChannels);
            }
        }
    }
}

void ColyseusRoomClient::handleError(const var& data)
{
    String errorMessage = data["message"].toString();
    
    if (onError)
        onError(errorMessage);
}

//==============================================================================
namespace ColyseusProtocol
{
    var createJoinRoomMessage(const String& roomName)
    {
        DynamicObject::Ptr obj(new DynamicObject());
        obj->setProperty("method", "joinOrCreate");
        obj->setProperty("roomName", roomName);
        obj->setProperty("options", var{});
        return var(obj.get());
    }

    var createLeaveRoomMessage()
    {
        DynamicObject::Ptr obj(new DynamicObject());
        obj->setProperty("method", "leave");
        return var(obj.get());
    }

    var createAudioMessage(const float* audioData, int numSamples, int numChannels)
    {
        DynamicObject::Ptr obj(new DynamicObject());
        obj->setProperty("type", "audio");
        obj->setProperty("numSamples", numSamples);
        obj->setProperty("numChannels", numChannels);
        
        // Convert float audio to base64 for JSON transport
        size_t audioDataSize = static_cast<size_t>(numSamples) * static_cast<size_t>(numChannels) * sizeof(float);
        MemoryBlock audioBlock(audioData, audioDataSize);
        obj->setProperty("audioData", audioBlock.toBase64Encoding());
        
        return var(obj.get());
    }

    bool isRoomJoinedMessage(const var& message)
    {
        return message["type"].toString() == "roomJoined";
    }

    bool isUserJoinedMessage(const var& message)
    {
        return message["type"].toString() == "userJoined";
    }

    bool isUserLeftMessage(const var& message)
    {
        return message["type"].toString() == "userLeft";
    }

    bool isAudioDataMessage(const var& message)
    {
        return message["type"].toString() == "audio";
    }

    bool isErrorMessage(const var& message)
    {
        return message["type"].toString() == "error";
    }
}