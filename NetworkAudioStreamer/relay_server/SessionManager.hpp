#pragma once
#include <array>
#include <vector>
#include <memory>
#include <atomic>
#include <sys/mman.h>
#include <boost/asio/ip/udp.hpp>

class WSSession; // forward declaration to avoid header cycle

using udp = boost::asio::ip::udp;

struct alignas(64) Client {
    std::atomic<uint32_t> userId{0};
    std::atomic<WSSession*> wsSession{nullptr};
    std::atomic<uint32_t> endpoint_addr{0};
    std::atomic<uint16_t> endpoint_port{0};
    std::atomic<bool> hasAudioEndpoint{false};
};

struct alignas(64) Session {
    std::atomic<uint32_t> sessionId{0};
    std::array<Client, 8> clients;
    std::atomic<uint8_t> client_count{0};
    
    bool addClient(uint32_t userId, WSSession* ws) {
        uint8_t count = client_count.load();
        if (count >= 8) return false;
        
        for (int i = 0; i < 8; ++i) {
            uint32_t expected = 0;
            if (clients[i].userId.compare_exchange_weak(expected, userId)) {
                clients[i].wsSession.store(ws);
                client_count.fetch_add(1);
                return true;
            }
        }
        return false;
    }
    
    void removeClient(uint32_t userId) {
        for (int i = 0; i < 8; ++i) {
            if (clients[i].userId.load() == userId) {
                clients[i].userId.store(0);
                clients[i].wsSession.store(nullptr);
                clients[i].hasAudioEndpoint.store(false);
                client_count.fetch_sub(1);
                break;
            }
        }
    }
    
    void setAudioEndpoint(uint32_t userId, const udp::endpoint& endpoint) {
        for (int i = 0; i < 8; ++i) {
            if (clients[i].userId.load() == userId) {
                clients[i].endpoint_addr.store(endpoint.address().to_v4().to_uint());
                clients[i].endpoint_port.store(endpoint.port());
                clients[i].hasAudioEndpoint.store(true);
                break;
            }
        }
    }
    
    std::vector<udp::endpoint> getOtherAudioEndpoints(uint32_t excludeUserId) const {
        std::vector<udp::endpoint> endpoints;
        for (int i = 0; i < 8; ++i) {
            uint32_t userId = clients[i].userId.load();
            if (userId != 0 && userId != excludeUserId && clients[i].hasAudioEndpoint.load()) {
                auto addr = boost::asio::ip::address_v4(clients[i].endpoint_addr.load());
                endpoints.emplace_back(addr, clients[i].endpoint_port.load());
            }
        }
        return endpoints;
    }
};

class SessionManager {
    Session* sessions_;
    std::atomic<uint32_t> session_count_{0};
    
public:
    SessionManager() {
        sessions_ = static_cast<Session*>(
            mmap(nullptr, sizeof(Session) * 100, 
                 PROT_READ | PROT_WRITE, 
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        );
        if (sessions_ == MAP_FAILED) {
            throw std::runtime_error("Failed to allocate session storage");
        }
        
        for (int i = 0; i < 100; ++i) {
            new(&sessions_[i]) Session();
        }
    }
    
    ~SessionManager() {
        if (sessions_ != MAP_FAILED) {
            for (int i = 0; i < 100; ++i) {
                sessions_[i].~Session();
            }
            munmap(sessions_, sizeof(Session) * 100);
        }
    }
    bool joinSession(uint32_t sessionId, uint32_t userId, WSSession* ws) {
        for (int i = 0; i < 100; ++i) {
            uint32_t expected = 0;
            if (sessions_[i].sessionId.compare_exchange_weak(expected, sessionId)) {
                return sessions_[i].addClient(userId, ws);
            }
            if (sessions_[i].sessionId.load() == sessionId) {
                return sessions_[i].addClient(userId, ws);
            }
        }
        return false;
    }

    void leaveSession(uint32_t sessionId, uint32_t userId);
    
    void setClientAudioEndpoint(uint32_t sessionId, uint32_t userId, const udp::endpoint& endpoint) {
        for (int i = 0; i < 100; ++i) {
            if (sessions_[i].sessionId.load() == sessionId) {
                sessions_[i].setAudioEndpoint(userId, endpoint);
                break;
            }
        }
    }
    
    std::vector<udp::endpoint> getRelayTargets(uint32_t sessionId, uint32_t excludeUserId) {
        for (int i = 0; i < 100; ++i) {
            if (sessions_[i].sessionId.load() == sessionId) {
                return sessions_[i].getOtherAudioEndpoints(excludeUserId);
            }
        }
        return {};
    }
    
    void cleanup_empty_sessions() {
        for (int i = 0; i < 100; ++i) {
            if (sessions_[i].client_count.load() == 0) {
                sessions_[i].sessionId.store(0);
            }
        }
    }
};

// No global SessionManager; instances are passed explicitly
