#include "NetworkManager.h"
#include "NetworkAudioProcessor.h"

struct NetworkManager::Impl {
    std::unique_ptr<StreamingSocket> socket;
    std::unique_ptr<StreamingSocket> listener;
    std::vector<std::unique_ptr<StreamingSocket>> clients;
    std::vector<uint8_t> receiveBuffer;
    
    std::unique_ptr<StreamingSocket> webListener;
    std::unique_ptr<Thread> webThread;
    bool webRunning = false;
    
    Impl() : receiveBuffer(4096) {}
};

NetworkManager::NetworkManager() : pImpl_(std::make_unique<Impl>()) {}

NetworkManager::~NetworkManager() {
    stop();
}

bool NetworkManager::startServer(int port) {
    stop();
    
    pImpl_->listener = std::make_unique<StreamingSocket>();
    if (!pImpl_->listener->createListener(port, String())) {
        setError("Failed to create listener on port " + String(port));
        return false;
    }
    
    mode_ = Mode::Server;
    status_ = Status::Connected;
    return true;
}

bool NetworkManager::startClient(const String& address, int port) {
    stop();
    
    pImpl_->socket = std::make_unique<StreamingSocket>();
    if (!pImpl_->socket->connect(address, port, 5000)) {
        setError("Failed to connect to " + address + ":" + String(port));
        return false;
    }
    
    mode_ = Mode::Client;
    status_ = Status::Connected;
    return true;
}

void NetworkManager::stop() {
    stopWebInterface();
    
    mode_ = Mode::Inactive;
    status_ = Status::Disconnected;
    clientCount_ = 0;
    
    pImpl_->socket.reset();
    pImpl_->listener.reset();
    pImpl_->clients.clear();
}

bool NetworkManager::sendPacket(std::shared_ptr<AudioPacket> packet) {
    if (mode_ == Mode::Inactive || !packet || !packet->isValid()) {
        return false;
    }
    
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&packet->header);
    size_t totalSize = packet->totalSize();
    
    if (mode_ == Mode::Client && pImpl_->socket) {
        int sent = pImpl_->socket->write(data, sizeof(AudioPacketHeader));
        if (sent == sizeof(AudioPacketHeader) && !packet->audioData.empty()) {
            sent += pImpl_->socket->write(packet->audioData.data(), packet->header.dataSize);
        }
        return sent == static_cast<int>(totalSize);
    }
    
    if (mode_ == Mode::Server) {
        // Accept new connections
        if (pImpl_->listener) {
            auto newClient = pImpl_->listener->waitForNextConnection();
            if (newClient) {
                pImpl_->clients.emplace_back(std::move(newClient));
                clientCount_ = static_cast<int>(pImpl_->clients.size());
            }
        }
        
        // Send to all clients
        for (auto& client : pImpl_->clients) {
            if (client && client->isConnected()) {
                client->write(data, sizeof(AudioPacketHeader));
                if (!packet->audioData.empty()) {
                    client->write(packet->audioData.data(), packet->header.dataSize);
                }
            }
        }
        return true;
    }
    
    return false;
}

std::shared_ptr<AudioPacket> NetworkManager::receivePacket() {
    if (mode_ == Mode::Inactive) return nullptr;
    
    StreamingSocket* activeSocket = nullptr;
    if (mode_ == Mode::Client) {
        activeSocket = pImpl_->socket.get();
    } else if (mode_ == Mode::Server && !pImpl_->clients.empty()) {
        activeSocket = pImpl_->clients[0].get(); // Simple: use first client
    }
    
    if (!activeSocket || !activeSocket->isConnected()) return nullptr;
    
    // Read header first
    AudioPacketHeader header;
    int headerBytes = activeSocket->read(&header, sizeof(header), false);
    if (headerBytes != sizeof(header)) return nullptr;
    
    auto packet = std::make_shared<AudioPacket>();
    packet->header = header;
    
    // Read audio data if present
    if (header.dataSize > 0 && header.dataSize <= AudioPacket::MAX_AUDIO_SIZE) {
        packet->audioData.resize(header.dataSize);
        int dataBytes = activeSocket->read(packet->audioData.data(), header.dataSize, true);
        if (dataBytes != header.dataSize) return nullptr;
    }
    
    return packet->isValid() ? packet : nullptr;
}

String NetworkManager::getLastError() const {
    const ScopedLock lock(errorLock_);
    return lastError_;
}

void NetworkManager::setError(const String& error) {
    const ScopedLock lock(errorLock_);
    lastError_ = error;
    status_ = Status::Error;
    DBG("NetworkManager error: " << error);
}

class WebServerThread : public Thread {
public:
    WebServerThread(NetworkManager::Impl* impl, NetworkManager* manager, int port) 
        : Thread("WebServer"), impl_(impl), manager_(manager), port_(port) {}
        
    NetworkManager* getManager() const { return manager_; }
    
    void run() override {
        impl_->webListener = std::make_unique<StreamingSocket>();
        if (!impl_->webListener->createListener(port_, String())) {
            return;
        }
        
        while (!threadShouldExit()) {
            auto client = impl_->webListener->waitForNextConnection();
            if (client && !threadShouldExit()) {
                handleHttpRequest(client);
            }
        }
    }
    
private:
    NetworkManager::Impl* impl_;
    NetworkManager* manager_;
    int port_;
    
    void handleHttpRequest(StreamingSocket* client) {
        char buffer[1024];
        int bytesRead = client->read(buffer, sizeof(buffer) - 1, false);
        if (bytesRead <= 0) return;
        
        buffer[bytesRead] = '\0';
        String request(buffer);
        
        if (request.startsWith("GET")) {
            String response = generateWebPage();
            
            String httpResponse = "HTTP/1.1 200 OK\r\n";
            httpResponse += "Content-Type: text/html\r\n";
            httpResponse += "Content-Length: " + String(response.length()) + "\r\n";
            httpResponse += "\r\n";
            httpResponse += response;
            
            client->write(httpResponse.toUTF8(), httpResponse.getNumBytesAsUTF8());
        }
    }
    
    String generateWebPage() {
        String mode = (manager_->getMode() == NetworkManager::Mode::Server) ? "Server" : 
                     (manager_->getMode() == NetworkManager::Mode::Client) ? "Client" : "Inactive";
        String status = (manager_->getStatus() == NetworkManager::Status::Connected) ? "Connected" : 
                       (manager_->getStatus() == NetworkManager::Status::Connecting) ? "Connecting" : 
                       (manager_->getStatus() == NetworkManager::Status::Error) ? "Error" : "Disconnected";
        
        String html = "<!DOCTYPE html><html><head>";
        html += "<title>Network Audio Streamer - Debug Monitor</title>";
        html += "<meta http-equiv=\"refresh\" content=\"2\">";
        html += "<style>body{font-family:monospace;background:#111;color:#0f0;margin:20px;}";
        html += "h1,h2{color:#0ff;border-bottom:1px solid #333;}";
        html += "table{border-collapse:collapse;width:100%;}";
        html += "th,td{border:1px solid #333;padding:8px;text-align:left;}";
        html += "th{background:#222;color:#0ff;}";
        html += ".error{color:#f44;}.warning{color:#fa0;}.good{color:#0f0;}";
        html += ".stats{display:flex;gap:20px;flex-wrap:wrap;}";
        html += ".stat-box{border:1px solid #333;padding:10px;min-width:200px;background:#1a1a1a;}";
        html += "</style></head><body>";
        
        html += "<h1>🎵 Network Audio Streamer - Debug Monitor</h1>";
        
        // Network Status
        html += "<div class=\"stats\">";
        html += "<div class=\"stat-box\"><h3>Network Status</h3>";
        html += "<p><strong>Mode:</strong> " + mode + "</p>";
        html += "<p><strong>Status:</strong> " + status + "</p>";
        html += "<p><strong>Connected Clients:</strong> " + String(manager_->getConnectedClients()) + "</p>";
        html += "</div>";
        
        // Get audio monitor data if available
        if (manager_->audioProcessor_) {
            auto* monitor = manager_->audioProcessor_->getAudioMonitor();
            if (monitor) {
                const auto& stats = monitor->getStats();
                
                // Audio Statistics
                html += "<div class=\"stat-box\"><h3>Audio Statistics</h3>";
                html += "<p><strong>Packets Sent:</strong> " + String(stats.packetsSent.load()) + "</p>";
                html += "<p><strong>Packets Received:</strong> " + String(stats.packetsReceived.load()) + "</p>";
                html += "<p><strong>Packets Dropped:</strong> <span class=\"" + 
                    String(stats.packetsDropped.load() > 0 ? "error" : "good") + "\">" + 
                    String(stats.packetsDropped.load()) + "</span></p>";
                html += "<p><strong>Process Time:</strong> " + String(stats.processTimeMs.load(), 3) + "ms</p>";
                html += "</div>";
                
                // Buffer Health
                html += "<div class=\"stat-box\"><h3>Buffer Health</h3>";
                html += "<p><strong>Buffer Underruns:</strong> <span class=\"" + 
                    String(stats.bufferUnderruns.load() > 0 ? "error" : "good") + "\">" + 
                    String(stats.bufferUnderruns.load()) + "</span></p>";
                html += "<p><strong>Buffer Overruns:</strong> <span class=\"" + 
                    String(stats.bufferOverruns.load() > 0 ? "error" : "good") + "\">" + 
                    String(stats.bufferOverruns.load()) + "</span></p>";
                html += "<p><strong>Glitches Detected:</strong> <span class=\"" + 
                    String(stats.glitchCount.load() > 0 ? "warning" : "good") + "\">" + 
                    String(stats.glitchCount.load()) + "</span></p>";
                html += "</div>";
                
                // Performance
                html += "<div class=\"stat-box\"><h3>Performance</h3>";
                html += "<p><strong>CPU Usage:</strong> <span class=\"" + 
                    String(stats.cpuUsage.load() > 0.8 ? "error" : (stats.cpuUsage.load() > 0.5 ? "warning" : "good")) + "\">" + 
                    String(stats.cpuUsage.load() * 100, 1) + "%</span></p>";
                html += "<p><strong>Average Latency:</strong> " + String(stats.avgLatency.load(), 2) + "ms</p>";
                double inputDb = stats.inputLevel.load() > 0.0 ? Decibels::gainToDecibels(stats.inputLevel.load()) : -100.0;
                double outputDb = stats.outputLevel.load() > 0.0 ? Decibels::gainToDecibels(stats.outputLevel.load()) : -100.0;
                html += "<p><strong>Input Level:</strong> " + String(inputDb, 1) + "dB</p>";
                html += "<p><strong>Output Level:</strong> " + String(outputDb, 1) + "dB</p>";
                html += "</div>";
                html += "</div>";
                
                // Recent Glitches
                auto glitches = monitor->getRecentGlitches(15);
                if (!glitches.empty()) {
                    html += "<h2>⚠️ Recent Issues (" + String(glitches.size()) + ")</h2>";
                    html += "<table><tr><th>Time Ago</th><th>Type</th><th>Details</th><th>Severity</th></tr>";
                    
                    for (const auto& glitch : glitches) {
                        uint64_t ageMs = Time::getMillisecondCounter() - glitch.timestamp;
                        String ageStr;
                        if (ageMs < 1000) ageStr = String(ageMs) + "ms";
                        else if (ageMs < 60000) ageStr = String(ageMs / 1000.0, 1) + "s";
                        else ageStr = String(ageMs / 60000.0, 1) + "m";
                        
                        String severityClass = String(glitch.severity > 0.8 ? "error" : 
                                             (glitch.severity > 0.5 ? "warning" : "good"));
                        
                        html += "<tr><td>" + ageStr + "</td>";
                        html += "<td>" + glitch.type + "</td>";
                        html += "<td>" + glitch.details + "</td>";
                        html += "<td class=\"" + severityClass + "\">" + String(glitch.severity, 2) + "</td></tr>";
                    }
                    html += "</table>";
                } else {
                    html += "<h2>✅ No Recent Issues</h2>";
                    html += "<p>System is running smoothly!</p>";
                }
            }
        } else {
            html += "</div>";
            html += "<div class=\"stat-box error\"><h3>⚠️ Monitoring Unavailable</h3>";
            html += "<p>Audio processor not accessible for monitoring</p></div>";
        }
        
        html += "<br><p><em>Auto-refreshing every 2 seconds...</em></p>";
        html += "</body></html>";
        
        return html;
    }
};

bool NetworkManager::startWebInterface(int webPort) {
    stopWebInterface();
    
    pImpl_->webThread = std::make_unique<WebServerThread>(pImpl_.get(), this, webPort);
    pImpl_->webThread->startThread();
    pImpl_->webRunning = true;
    
    return true;
}

void NetworkManager::stopWebInterface() {
    if (pImpl_->webThread) {
        pImpl_->webThread->signalThreadShouldExit();
        pImpl_->webThread->waitForThreadToExit(2000);
        pImpl_->webThread.reset();
    }
    pImpl_->webListener.reset();
    pImpl_->webRunning = false;
}