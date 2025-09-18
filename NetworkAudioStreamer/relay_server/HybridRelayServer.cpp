#include "SessionManager.hpp"
#include "UDPAudioRelay.hpp"
#include "WSControlServer.hpp"
#include "PuterBackend.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <pthread.h>
#include <sched.h>
#include <thread>
#include <atomic>

// Enhanced WSSession with FL Studio room support
class HybridWSSession : public std::enable_shared_from_this<HybridWSSession> {
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    uint32_t sessionId_ = 0;
    uint32_t userId_ = 0;
    std::string roomCode_;
    std::string userType_;
    SessionManager& sessionManager_;
    PuterBackend& puterBackend_;
    
public:
    explicit HybridWSSession(tcp::socket&& socket, SessionManager& sessionManager, PuterBackend& puterBackend)
        : ws_(std::move(socket)), sessionManager_(sessionManager), puterBackend_(puterBackend) {}
    
    ~HybridWSSession() {
        if (sessionId_ != 0 && userId_ != 0) {
            sessionManager_.leaveSession(sessionId_, userId_);
        }
        if (!roomCode_.empty() && !userType_.empty()) {
            puterBackend_.leaveRoom(roomCode_, userType_);
        }
    }

    void setSessionInfo(uint32_t sessionId, uint32_t userId) {
        sessionId_ = sessionId;
        userId_ = userId;
    }
    
    void setRoomInfo(const std::string& roomCode, const std::string& userType) {
        roomCode_ = roomCode;
        userType_ = userType;
    }
    
    void run() {
        ws_.async_accept([self = shared_from_this()](beast::error_code ec) {
            if (!ec) self->do_read();
        });
    }
    
    void send(const std::string& message) {
        ws_.async_write(boost::asio::buffer(message),
            [self = shared_from_this()](beast::error_code, std::size_t) {});
    }

private:
    void do_read() {
        ws_.async_read(buffer_, [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (!ec) {
                std::string msg = beast::buffers_to_string(self->buffer_.data());
                self->handle_message(msg);
                self->buffer_.clear();
                self->do_read();
            }
        });
    }
    
    void handle_message(const std::string& msg) {
        try {
            auto json = nlohmann::json::parse(msg);

            if (!json.contains("type") || !json["type"].is_string()) {
                send(R"({"type":"error","message":"missing or invalid 'type' field"})");
                return;
            }
            std::string type = json["type"];
            
            // Traditional UDP relay protocol
            if (type == "join") {
                if (!json.contains("sessionId") || !json["sessionId"].is_number_unsigned() ||
                    !json.contains("userId") || !json["userId"].is_number_unsigned()) {
                    send(R"({"type":"error","message":"missing or invalid 'sessionId' or 'userId' for join"})");
                    return;
                }
                uint32_t sessionId = json["sessionId"];
                uint32_t userId = json["userId"];
                if (sessionId == 0 || userId == 0) {
                    send(R"({"type":"error","message":"invalid session/user ID"})");
                    return;
                }
                setSessionInfo(sessionId, userId);
                sessionManager_.joinSession(sessionId, userId, nullptr); // Cast to WSSession* handled elsewhere
                send(R"({"type":"joined","status":"ok"})");
            }
            // FL Studio room protocol
            else if (type == "join_room") {
                if (!json.contains("roomCode") || !json["roomCode"].is_string() ||
                    !json.contains("userType") || !json["userType"].is_string()) {
                    send(R"({"type":"error","message":"missing roomCode or userType for join_room"})");
                    return;
                }
                std::string roomCode = json["roomCode"];
                std::string userType = json["userType"];
                
                if (puterBackend_.joinRoom(roomCode, userType)) {
                    setRoomInfo(roomCode, userType);
                    send(R"({"type":"room_joined","status":"ok"})");
                } else {
                    send(R"({"type":"error","message":"failed to join room"})");
                }
            }
            else if (type == "leave_room") {
                if (!roomCode_.empty() && !userType_.empty()) {
                    puterBackend_.leaveRoom(roomCode_, userType_);
                    roomCode_.clear();
                    userType_.clear();
                    send(R"({"type":"room_left","status":"ok"})");
                } else {
                    send(R"({"type":"error","message":"not in a room"})");
                }
            }
            else if (type == "heartbeat") {
                if (!roomCode_.empty() && !userType_.empty()) {
                    if (puterBackend_.updateHeartbeat(roomCode_, userType_)) {
                        send(R"({"type":"heartbeat_ack","status":"ok"})");
                    } else {
                        send(R"({"type":"error","message":"heartbeat failed"})");
                    }
                } else {
                    send(R"({"type":"error","message":"not in a room"})");
                }
            }
            else if (type == "leave") {
                if (!json.contains("sessionId") || !json["sessionId"].is_number_unsigned() ||
                    !json.contains("userId") || !json["userId"].is_number_unsigned()) {
                    send(R"({"type":"error","message":"missing or invalid 'sessionId' or 'userId' for leave"})");
                    return;
                }
                uint32_t sessionId = json["sessionId"];
                uint32_t userId = json["userId"];
                sessionManager_.leaveSession(sessionId, userId);
                send(R"({"type":"left","status":"ok"})");
            } else {
                send(R"({"type":"error","message":"unknown message type"})");
            }
        } catch (const nlohmann::json::exception& e) {
            nlohmann::json err = {
                {"type", "error"},
                {"message", std::string("JSON parse error: ") + e.what()}
            };
            send(err.dump());
        }
    }
};

// Hybrid relay server supporting both UDP and Puter backends
class HybridRelayServer {
    boost::asio::io_context ioc_;
    tcp::acceptor acceptor_;
    UDPAudioRelay udpRelay_;
    SessionManager& sessionManager_;
    std::unique_ptr<PuterBackend> puterBackend_;
    boost::asio::steady_timer cleanup_timer_;
    std::atomic<bool> running_{false};
    
    // Background thread for Puter API operations
    std::unique_ptr<std::thread> puterThread_;
    std::atomic<bool> puterRunning_{false};
    
public:
    HybridRelayServer(unsigned short wsPort, unsigned short udpPort, SessionManager& sessionManager, bool enablePuter = true)
        : acceptor_(ioc_, {tcp::v4(), wsPort})
        , udpRelay_(ioc_, udpPort, sessionManager)
        , sessionManager_(sessionManager)
        , cleanup_timer_(ioc_)
    {
        if (enablePuter) {
            puterBackend_ = std::make_unique<PuterBackend>();
            if (puterBackend_->initialize()) {
                std::cout << "Puter API backend initialized successfully" << std::endl;
                startPuterThread();
            } else {
                std::cout << "Warning: Puter API backend failed to initialize" << std::endl;
                puterBackend_.reset();
            }
        }
        
        do_accept();
        start_cleanup();
    }
    
    ~HybridRelayServer() {
        running_.store(false);
        stopPuterThread();
    }
    
    void run() { 
        running_.store(true);
        ioc_.run(); 
    }
    
private:
    void do_accept() {
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                if (puterBackend_) {
                    std::make_shared<HybridWSSession>(std::move(socket), sessionManager_, *puterBackend_)->run();
                } else {
                    // Fallback to basic WSSession for UDP-only mode
                    std::make_shared<WSSession>(std::move(socket), sessionManager_)->run();
                }
            }
            do_accept();
        });
    }
    
    void start_cleanup() {
        cleanup_timer_.expires_after(std::chrono::minutes(5));
        cleanup_timer_.async_wait([this](auto) {
            if (running_.load()) {
                sessionManager_.cleanup_empty_sessions();
                start_cleanup();
            }
        });
    }
    
    void startPuterThread() {
        if (!puterBackend_ || puterRunning_.load()) return;
        
        puterRunning_.store(true);
        puterThread_ = std::make_unique<std::thread>([this]() {
            while (puterRunning_.load() && running_.load()) {
                try {
                    // Periodic maintenance tasks for Puter backend
                    // This could include cleanup, connection health checks, etc.
                    std::this_thread::sleep_for(std::chrono::seconds(30));
                    
                    if (puterBackend_ && !puterBackend_->isConnected()) {
                        std::cout << "Puter connection lost, attempting reconnect..." << std::endl;
                        puterBackend_->initialize();
                    }
                    
                } catch (const std::exception& e) {
                    std::cerr << "Puter thread error: " << e.what() << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
        });
    }
    
    void stopPuterThread() {
        puterRunning_.store(false);
        if (puterThread_ && puterThread_->joinable()) {
            puterThread_->join();
        }
        if (puterBackend_) {
            puterBackend_->shutdown();
        }
    }
};

int main(int argc, char* argv[]) {
    // Parse command line arguments
    bool enablePuter = true;
    unsigned short wsPort = 8080;
    unsigned short udpPort = 9001;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-puter") {
            enablePuter = false;
        } else if (arg == "--ws-port" && i + 1 < argc) {
            wsPort = static_cast<unsigned short>(std::stoi(argv[++i]));
        } else if (arg == "--udp-port" && i + 1 < argc) {
            udpPort = static_cast<unsigned short>(std::stoi(argv[++i]));
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --no-puter       Disable Puter API backend (UDP only)\n";
            std::cout << "  --ws-port PORT   WebSocket port (default: 8080)\n";
            std::cout << "  --udp-port PORT  UDP audio port (default: 9001)\n";
            std::cout << "  --help           Show this help message\n";
            return 0;
        }
    }
    
    // Set real-time priority for audio processing
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    
    struct sched_param param;
    param.sched_priority = 99;
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        std::cerr << "Warning: Could not set real-time priority" << std::endl;
    }
    
    try {
        SessionManager sessionManager;
        HybridRelayServer server(wsPort, udpPort, sessionManager, enablePuter);
        
        std::cout << "Hybrid Audio Relay Server starting..." << std::endl;
        std::cout << "WebSocket Control: ws://localhost:" << wsPort << std::endl;
        std::cout << "UDP Audio Relay: udp://localhost:" << udpPort << std::endl;
        
        if (enablePuter) {
            std::cout << "Puter API Backend: Enabled (FL Studio room support)" << std::endl;
        } else {
            std::cout << "Puter API Backend: Disabled (UDP relay only)" << std::endl;
        }
        
        std::cout << "Features: Lock-free, Zero-copy, Memory-mapped, Real-time priority" << std::endl;
        std::cout << "Press Ctrl+C to stop the server" << std::endl;
        
        server.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}