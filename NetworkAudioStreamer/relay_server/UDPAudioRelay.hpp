#pragma once
#include <boost/asio.hpp>
#include <array>
#include <iostream>
#include <sys/socket.h>
#include <sys/uio.h>
#include "AudioPacket.hpp"
class SessionManager;

class UDPAudioRelay {
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint remote_endpoint_;
    std::array<uint8_t, 2048> recv_buffer_;
    SessionManager& sessionManager_;
    int socket_fd_;
    
public:
    UDPAudioRelay(boost::asio::io_context& ioc, unsigned short port, SessionManager& sessionManager) 
        : socket_(ioc, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)), sessionManager_(sessionManager) {
        socket_fd_ = socket_.native_handle();
        
        int opt = 1;
        setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        
#ifdef SO_ZEROCOPY
        opt = 1;
        setsockopt(socket_fd_, SOL_SOCKET, SO_ZEROCOPY, &opt, sizeof(opt));
#endif
        
        opt = 262144;
        setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
        setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));
        
        start_receive();
    }
    
private:
    void start_receive() {
        socket_.async_receive_from(
            boost::asio::buffer(recv_buffer_), remote_endpoint_,
            [this](const boost::system::error_code& ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd >= AudioPacket::HEADER_SIZE) {
                    handle_audio_packet(bytes_recvd);
                }
                start_receive();
            }
        );
    }
    
    void handle_audio_packet(size_t bytes_received) {
        if (bytes_received < sizeof(AudioPacketHeader)) return;
        auto* packet = reinterpret_cast<const AudioPacket*>(recv_buffer_.data());
        if (bytes_received < packet->header.dataSize + sizeof(AudioPacketHeader)) return;
        if (packet->isValid()) {
            relay_to_session(packet, bytes_received);
        }
    }
    
    void relay_to_session(const AudioPacket* packet, size_t size);
    
    void send_to_endpoints_batch(const std::vector<boost::asio::ip::udp::endpoint>& targets, const void* data, size_t size) {
        if (targets.empty()) return;
        
        std::vector<struct iovec> iovecs(targets.size());
        std::vector<struct msghdr> msgs(targets.size());
        std::vector<struct sockaddr_in> addrs(targets.size());
        
        for (size_t i = 0; i < targets.size(); ++i) {
            iovecs[i].iov_base = const_cast<void*>(data);
            iovecs[i].iov_len = size;
            
            addrs[i].sin_family = AF_INET;
            addrs[i].sin_port = htons(targets[i].port());
            addrs[i].sin_addr.s_addr = htonl(targets[i].address().to_v4().to_uint());
            
            msgs[i].msg_name = &addrs[i];
            msgs[i].msg_namelen = sizeof(addrs[i]);
            msgs[i].msg_iov = &iovecs[i];
            msgs[i].msg_iovlen = 1;
            msgs[i].msg_control = nullptr;
            msgs[i].msg_controllen = 0;
        }
        
#ifdef MSG_ZEROCOPY
        int sent = sendmsg(socket_fd_, &msgs[0], MSG_DONTWAIT | MSG_ZEROCOPY);
#else
        int sent = sendmsg(socket_fd_, &msgs[0], MSG_DONTWAIT);
#endif
        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "Batch UDP send failed: " << strerror(errno) << std::endl;
        }
    }
};
