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

#include "utils.h"

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
    bool connect();

    void handshake();
    bool recv_bitfield();
    bool send_interested();
    bool send_request(uint32_t index, uint32_t begin, uint32_t length);
    bool process_incoming_message(int index);
    void send_have(uint32_t index);

    bool read_exact(uint8_t *buffer, size_t len);
    bool send_raw(const uint8_t *data, size_t len)
    {
        // 检查socket_fd 
        if (!is_socket_sendable(socket_fd)) {
            log_with_thread_id("socket_fd 不可用, 无法发送数据");
            return false;
        }

        size_t sent = 0;
        while (sent < len)
        {
            ssize_t res = ::send(socket_fd, data + sent, len - sent, 0);
            // 发送失败或对端断开
            if (res <= 0) {
                if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    continue; // 非阻塞模式下，继续尝试发送
                } else {
                    log_with_thread_id("send failed or connection closed");
                    return false;
                }
            }
            sent += res;
        }
        return true;
    }

    bool handle_piece_message(const std::vector<uint8_t> &payload);


    int socket_fd;
    std::vector<std::uint8_t> bit_field;
    std::vector<std::uint8_t> peer_id;
    std::vector<std::uint8_t> piece_buffer; // 用于存储接收到的 piece 数据
    Client& client; 
};

#endif