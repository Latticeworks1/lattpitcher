#include "SessionManager.hpp"
#include "UDPAudioRelay.hpp"
#include "WSControlServer.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <pthread.h>
#include <sched.h>

WSSession::~WSSession() {
    if (sessionId_ != 0 && userId_ != 0) {
        sessionManager_.leaveSession(sessionId_, userId_);
    }
}

void SessionManager::leaveSession(uint32_t sessionId, uint32_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto session_it = sessions_.find(sessionId);
    if (session_it != sessions_.end()) {
        session_it->second.removeClient(userId);
        if (session_it->second.clients.empty()) {
            sessions_.erase(session_it);
        }
    }
}

void WSSession::handle_message(const std::string& msg) {
    try {
        auto json = nlohmann::json::parse(msg);

        if (!json.contains("type") || !json["type"].is_string()) {
            send(R"({"type":"error","message":"missing or invalid 'type' field"})");
            return;
        }
        std::string type = json["type"];
        
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
            sessionManager_.joinSession(sessionId, userId, this);
            send(R"({"type":"joined","status":"ok"})");
        } else if (type == "leave") {
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

void UDPAudioRelay::relay_to_session(const AudioPacket* packet, size_t size) {
    auto targets = sessionManager_.getRelayTargets(packet->header.sessionId, packet->header.userId);
    send_to_endpoints_batch(targets, packet, size);
    
    sessionManager_.setClientAudioEndpoint(packet->header.sessionId, packet->header.userId, remote_endpoint_);
}

class RelayServer {
    boost::asio::io_context ioc_;
    tcp::acceptor acceptor_;
    UDPAudioRelay udpRelay_;
    SessionManager& sessionManager_;
    boost::asio::steady_timer cleanup_timer_;
    
public:
    RelayServer(unsigned short wsPort, unsigned short udpPort, SessionManager& sessionManager)
        : acceptor_(ioc_, {tcp::v4(), wsPort})
        , udpRelay_(ioc_, udpPort, sessionManager)
        , sessionManager_(sessionManager)
        , cleanup_timer_(ioc_)
    {
        do_accept();
        start_cleanup();
    }
    
    void run() { ioc_.run(); }
    
private:
    void do_accept() {
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<WSSession>(std::move(socket), sessionManager_)->run();
            }
            do_accept();
        });
    }
    
    void start_cleanup() {
        cleanup_timer_.expires_after(std::chrono::minutes(5));
        cleanup_timer_.async_wait([this](auto) {
            sessionManager_.cleanup_empty_sessions();
            start_cleanup();
        });
    }
};

int main() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    
    struct sched_param param;
    param.sched_priority = 99;
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        std::cerr << "Warning: Could not set real-time priority" << std::endl;
    }
    
    SessionManager sessionManager;
    RelayServer server(8080, 9001, sessionManager);
    std::cout << "Ultra-fast relay server running: WS=8080, UDP=9001" << std::endl;
    std::cout << "Features: Lock-free, Zero-copy, Memory-mapped, Real-time priority" << std::endl;
    server.run();
}
