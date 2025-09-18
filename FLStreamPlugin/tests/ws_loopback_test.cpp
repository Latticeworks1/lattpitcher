// Simple in-process loopback test using JUCE WebSocket server/client
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/WebSocketServer.h"

using namespace juce;

static void fillTestTone(AudioBuffer<float>& buf, double sr, float freq, float gain)
{
    buf.clear();
    float phase = 0.0f;
    const float inc = (float)(2.0 * MathConstants<double>::pi * freq / sr);
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const float s = std::sin(phase) * gain;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            buf.setSample(ch, i, s);
        phase += inc;
        if (phase > MathConstants<float>::twoPi)
            phase -= MathConstants<float>::twoPi;
    }
}

int main()
{
    Logger::writeToLog("FLStream Loopback Test starting...");

    FLStreamWebSocketServer server;
    std::atomic<int> serverPackets{0};

    server.onAudioReceived = [&](const OptimizedAudioPacket& pkt, const String& userId) {
        serverPackets.fetch_add(1);
        // Echo the received audio back to the same user (true loopback)
        AudioBuffer<float> tmp;
        pkt.toAudioBuffer(tmp);
        server.sendAudioToUser(tmp, userId);
    };

    // Find an available port in a safe range
    int port = 0;
    {
        Random r;
        bool started = false;
        for (int attempt = 0; attempt < 10 && !started; ++attempt) {
            port = 10000 + r.nextInt(5000); // 10000-14999
            if (server.startServer(port)) { started = true; break; }
            Thread::sleep(20);
        }
        if (!started) {
            Logger::writeToLog("Server failed to start");
            return 1;
        }
    }

    Logger::writeToLog("Server started");

    // Connect client
    FLStreamWebSocketClient client;
    if (!client.connectToServer("127.0.0.1", port, "default-room")) {
        Logger::writeToLog("Client failed to connect");
        server.stopServer();
        return 1;
    }
    Logger::writeToLog("Client connected");

    // Send audio from client -> server (expect echo back to client)
    const double sr = 48000.0;
    const int numSamples = 256;
    AudioBuffer<float> sendBuf(2, numSamples);
    fillTestTone(sendBuf, sr, 440.0f, 0.1f);
    client.sendAudio(sendBuf);

    // Wait for server to receive
    const auto t0 = Time::getMillisecondCounter();
    while (Time::getMillisecondCounter() - t0 < 2000 && serverPackets.load() == 0) {
        Thread::sleep(5);
    }
    if (serverPackets.load() == 0) {
        Logger::writeToLog("Server did not receive audio packet in time");
        client.disconnect(); server.stopServer();
        return 1;
    }
    Logger::writeToLog("Server received audio packet ✓");

    // Attempt to receive echoed audio on client
    AudioBuffer<float> recvBuf(2, numSamples);
    bool gotAudio = false;
    const auto t1 = Time::getMillisecondCounter();
    while (Time::getMillisecondCounter() - t1 < 3000) {
        if (client.receiveAudio(recvBuf)) { gotAudio = true; break; }
        Thread::sleep(5);
    }
    if (!gotAudio) {
        Logger::writeToLog("Client did not receive broadcast audio in time");
        client.disconnect(); server.stopServer();
        return 1;
    }
    Logger::writeToLog("Client received broadcast audio ✓");

    client.disconnect();
    server.stopServer();
    Logger::writeToLog("Loopback test passed ✓");
    return 0;
}
