#pragma once
#include <cstdint>
#include <array>

struct AudioPacketHeader {
    uint32_t sessionId;
    uint32_t userId;
    uint32_t sequence;
    uint32_t timestamp;
    uint16_t dataSize;
    uint8_t channels;
    uint8_t reserved;
} __attribute__((packed));

class AudioPacket {
public:
    static constexpr size_t MAX_AUDIO_SIZE = 1920;
    static constexpr size_t HEADER_SIZE = sizeof(AudioPacketHeader);
    
    AudioPacketHeader header{};
    std::array<uint8_t, MAX_AUDIO_SIZE> data{};
    
    size_t totalSize() const { return HEADER_SIZE + header.dataSize; }
    bool isValid() const { 
        return header.dataSize <= MAX_AUDIO_SIZE; 
    }
};