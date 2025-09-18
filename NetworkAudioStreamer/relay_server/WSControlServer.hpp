#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <string>

class SessionManager;

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

class WSSession : public std::enable_shared_from_this<WSSession> {
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    uint32_t sessionId_ = 0;
    uint32_t userId_ = 0;
    SessionManager& sessionManager_;
    
public:
    explicit WSSession(tcp::socket&& socket, SessionManager& sessionManager)
        : ws_(std::move(socket)), sessionManager_(sessionManager) {}
    
    ~WSSession();

    void setSessionInfo(uint32_t sessionId, uint32_t userId) {
        sessionId_ = sessionId;
        userId_ = userId;
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
    
    void handle_message(const std::string& msg);
};

// Destructor is defined in a .cpp to avoid include cycles
