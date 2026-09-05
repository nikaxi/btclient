#ifndef PEER_CONNECTION_H
#define PEER_CONNECTION_H

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>

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

class PeerConnection {
private:
    int sock_fd;
    std::string ip;
    uint16_t port;
    std::vector<uint8_t> info_hash; // 20 字节
    std::vector<uint8_t> peer_id;   // 20 字节

    // 内部辅助方法：发送原始数据
    bool send_raw(const uint8_t* data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            ssize_t res = ::send(sock_fd, reinterpret_cast<const char*>(data + sent), len - sent, 0);
            if (res <= 0) return false; // 发送失败或对端断开
            sent += res;
        }
        return true;
    }

public:
    PeerConnection(const std::string& ip, uint16_t port, 
                   const std::vector<uint8_t>& info_hash, 
                   const std::vector<uint8_t>& peer_id)
        : ip(ip), port(port), info_hash(info_hash), peer_id(peer_id), sock_fd(-1) {}

    ~PeerConnection() {
        if (sock_fd != -1) {
#ifdef _WIN32
            ::closesocket(sock_fd);
#else
            ::close(sock_fd);
#endif
        }
    }

    // 1. 建立 TCP 连接
    bool connect() {
        sock_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_fd == -1) return false;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        return ::connect(sock_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    // 2. 执行 BT 握手 (Handshake)
    bool handshake() {
        // 构造 68 字节的握手包
        uint8_t handshake_msg[68] = {0};
        handshake_msg[0] = 19; // 协议名长度
        std::memcpy(&handshake_msg[1], "BitTorrent protocol", 19);
        // 8 字节保留位 (全 0)
        
        // 复制 Info Hash 和 Peer ID
        std::memcpy(&handshake_msg[28], info_hash.data(), 20);
        std::memcpy(&handshake_msg[48], peer_id.data(), 20);

        // 发送握手包
        if (!send_raw(handshake_msg, 68)) return false;

        // 接收对方的握手包
        uint8_t recv_buf[68] = {0};
        ssize_t n = ::recv(sock_fd, reinterpret_cast<char*>(recv_buf), 68, 0);
        if (n != 68 || recv_buf[0] != 19) return false;

        // 校验对方返回的 Info Hash 是否一致
        return std::memcmp(&recv_buf[28], info_hash.data(), 20) == 0;
    }

    // 3. 发送 Interested (感兴趣) 消息
    bool send_interested() {
        // 消息格式：4 字节长度 + 1 字节 ID (Interested 的 ID 是 2)
        uint8_t msg[5] = {0, 0, 0, 1, 2}; 
        return send_raw(msg, 5);
    }


    // 内部辅助：从 TCP 流中精确读取指定长度的数据
    bool read_exact(uint8_t* buffer, size_t len) {
        size_t received = 0;
        while (received < len) {
            ssize_t n = ::recv(sock_fd, reinterpret_cast<char*>(buffer + received), len - received, 0);
            if (n <= 0) return false; // 连接断开或出错
            received += n;
        }
        return true;
    }

      // 接收并处理来自 Peer 的消息
    bool process_incoming_message() {
        // 1. 读取 4 字节的长度前缀 (大端序)
        uint8_t len_buf[4];
        if (!read_exact(len_buf, 4)) return false;

        uint32_t msg_len = (len_buf[0] << 24) | (len_buf[1] << 16) | (len_buf[2] << 8) | len_buf[3];

        // 2. 处理 Keep-Alive 消息 (长度为 0)
        if (msg_len == 0) {
            std::cout << "[Peer] 收到 Keep-Alive 消息" << std::endl;
            return true; 
        }

        // 3. 读取 1 字节的消息 ID
        uint8_t msg_id;
        if (!read_exact(&msg_id, 1)) return false;

        // 4. 读取剩余的 Payload
        uint32_t payload_len = msg_len - 1;
        std::vector<uint8_t> payload(payload_len);
        if (payload_len > 0 && !read_exact(payload.data(), payload_len)) return false;

        // 5. 根据 ID 分发处理
        switch (msg_id) {
            case 1: // Choke
                std::cout << "[Peer] 收到 Choke (被阻塞)" << std::endl;
                // TODO: 更新状态，停止发送 Request
                break;
            case 2: // Unchoke
                std::cout << "[Peer] 收到 Unchoke (解除阻塞)，可以请求数据了！" << std::endl;
                // TODO: 触发发送 Request 逻辑
                break;
            case 5: // Bitfield
                std::cout << "[Peer] 收到 Bitfield，长度: " << payload_len << " 字节" << std::endl;
                // TODO: 解析 Bitfield，记录对方拥有哪些 Piece
                break;
            case 7: // Piece (实际数据)
                handle_piece_message(payload);
                break;
            default:
                std::cout << "[Peer] 收到未处理的消息 ID: " << (int)msg_id << std::endl;
                break;
        }
        return true;
    }
 // 处理 Piece 消息
    void handle_piece_message(const std::vector<uint8_t>& payload) {
        if (payload.size() < 8) return; // 至少需要 4(index) + 4(begin) 字节

        uint32_t index = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
        uint32_t begin = (payload[4] << 24) | (payload[5] << 16) | (payload[6] << 8) | payload[7];
        
        std::cout << "[Peer] 收到 Piece 数据: Index=" << index 
                  << ", Offset=" << begin 
                  << ", Size=" << (payload.size() - 8) << " bytes" << std::endl;
        
        // TODO: 1. 将 payload.data() + 8 的数据写入本地缓存
        // TODO: 2. 如果整个 Piece 下载完成，进行 SHA-1 校验
        // TODO: 3. 校验通过后，写入磁盘并发送 Have 消息给其他 Peer
    }

    // 发送 Request 消息
    // index: 分片(Piece)的索引
    // begin: 该 Block 在 Piece 中的字节偏移量
    // length: 请求的字节数 (通常为 16384)
    bool send_request(uint32_t index, uint32_t begin, uint32_t length) {
        // Request 消息格式：4字节长度(13) + 1字节ID(6) + 4字节index + 4字节begin + 4字节length
        uint8_t msg[17] = {0};
        
        // 1. 长度前缀 (13 = 1 + 4 + 4 + 4)
        msg[0] = 0; msg[1] = 0; msg[2] = 0; msg[3] = 13;
        // 2. 消息 ID (6 = Request)
        msg[4] = 6;
        
        // 3. 填充 Payload (大端序)
        msg[5]  = (index >> 24) & 0xFF; msg[6]  = (index >> 16) & 0xFF;
        msg[7]  = (index >> 8) & 0xFF;  msg[8]  = index & 0xFF;
        
        msg[9]  = (begin >> 24) & 0xFF; msg[10] = (begin >> 16) & 0xFF;
        msg[11] = (begin >> 8) & 0xFF;  msg[12] = begin & 0xFF;
        
        msg[13] = (length >> 24) & 0xFF; msg[14] = (length >> 16) & 0xFF;
        msg[15] = (length >> 8) & 0xFF;  msg[16] = length & 0xFF;

        return send_raw(msg, 17);
    }


    
};


#endif