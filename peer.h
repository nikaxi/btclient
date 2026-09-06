#ifndef PEER_H
#define PEER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>
#include <cstddef>
#include <string>
#include <cstring>
#include <stdexcept>
#include <memory>
// 跨平台 Socket 头文件
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#endif

class Client; // 前向声明

struct Peer
{
    std::array<std::byte, 4> ip;
    std::array<std::byte, 2> port;

    Peer(std::array<std::byte, 4> ip_, std::array<std::byte, 2> port_, Client& client_) : ip(ip_), port(port_), client(client_)
    {
    }

    ~Peer()
    {
        ::close(socket_fd);
    }
    std::string to_string() const
    {
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
    std::string str_ip()
    {
        char buf[16]; // IPv4最长15字符 + '\0'1
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                 std::to_integer<int>(ip[0]),
                 std::to_integer<int>(ip[1]),
                 std::to_integer<int>(ip[2]),
                 std::to_integer<int>(ip[3]));
        return std::string(buf);
    }

    int port_()
    {
        return (std::to_integer<int>(port[0]) << 8) | std::to_integer<int>(port[1]);
    }

    void run();
    void connect();
    // handshake()、send_interested()、wait_for_unchoke()、send_request()、process_incoming() 等方法可以在这里实现
    void handshake();
    bool recv_bitfield();
    bool send_interested();
    bool send_request(uint32_t index, uint32_t begin, uint32_t length);
    bool process_incoming_message();
    void send_have(uint32_t index);

    bool read_exact(uint8_t *buffer, size_t len);

    bool send_raw(const uint8_t *data, size_t len)
    {
        size_t sent = 0;

        while (sent < len)
        {
            ssize_t res = ::send(socket_fd, data + sent, len - sent, 0);
            // 发送失败或对端断开
            if (res <= 0)
                return false;
            sent += res;
        }
        return true;
    }

    void handle_piece_message(const std::vector<uint8_t> &payload);


    int socket_fd;
    std::vector<std::uint8_t> bit_field;
    std::vector<std::uint8_t> peer_id;
    Client& client; 
};

#endif