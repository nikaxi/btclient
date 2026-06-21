#ifndef PEER_H
#define PEER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
struct Peer  {
    std::array<std::byte, 4> ip;
    std::array<std::byte, 2> port;

    Peer(std::array<std::byte, 4> ip_, std::array<std::byte, 2> port_):
        ip(ip_), port(port_) 
        {}
    std::string to_string() {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d:%d",
             std::to_integer<int>(ip[0]),
             std::to_integer<int>(ip[1]),
             std::to_integer<int>(ip[2]),
             std::to_integer<int>(ip[3]),
             (std::to_integer<int>(port[0]) << 8) | std::to_integer<int>(port[1]));
        return std::string(buffer);
    }
};

#endif