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
    std::string to_string() const {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d:%d",
             std::to_integer<int>(ip[0]),
             std::to_integer<int>(ip[1]),
             std::to_integer<int>(ip[2]),
             std::to_integer<int>(ip[3]),
             (std::to_integer<int>(port[0]) << 8) | std::to_integer<int>(port[1]));
        return std::string(buffer);
    }

    // ip 的字符串形式
    std::string str_ip() {
         char buf[16]; // IPv4最长15字符 + '\0'1
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
             std::to_integer<int>(ip[0]),
             std::to_integer<int>(ip[1]),
             std::to_integer<int>(ip[2]),
             std::to_integer<int>(ip[3])
            );
    return std::string(buf);
    }

    int port_() {
        return (std::to_integer<int>(port[0]) << 8) | std::to_integer<int>(port[1]);
    }

    void run();
};

#endif