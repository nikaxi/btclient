#ifndef CLIENT_H
#define CLIENT_H

#include <vector>
#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include "peer.h"
#include <iostream>
#include <cstring>
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


struct Torrent;
const int PORT = 6882;

using vec_u8 = std::vector<std::byte>;

class Client{
    public:
    void set_bit(int idx);
    bool is_bit_set(int idx);
    void set_piece(int idx);

    bool has_piece(int idx);

    bool recv_bitfield();

    void set_peer(Peer *p) {
        peer = p;
    }

    std::vector<Peer> get_peers(Torrent &torrent);

    Client(std::array<std::uint8_t, 20> &peer_id_, std::array<std::uint8_t, 20> &info_hash_):
        peer_id(peer_id_), info_hash(info_hash_), socket_fd(-1){}

    // bool request_piece(int index, std::vector<std::uint8_t> &piece_hash);    
     bool send_request(uint32_t index, uint32_t begin, uint32_t length);

    bool send_raw(const uint8_t* data, size_t len) {
        size_t sent = 0;

        while (sent < len) {
            ssize_t res = ::send(socket_fd, data+sent, len - sent, 0);
            // 发送失败或对端断开
            if (res <= 0) return false;
            sent += res;
        }
        return true;
    }

    bool connect();
    bool handshake();
    bool send_interested();
    bool read_exact(uint8_t* buffer, size_t len) ;
    bool process_incoming_message();


   void handle_piece_message(const std::vector<uint8_t>& payload) {
        if (payload.size() < 8) return; // 至少需要 4(index) + 4(begin) 字节

        uint32_t index = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
        uint32_t begin = (payload[4] << 24) | (payload[5] << 16) | (payload[6] << 8) | payload[7];
        
        std::cout << "[Peer] 收到 Piece 数据: Index=" << index 
                  << ", Offset=" << begin 
                  << ", Size=" << (payload.size() - 8) << " bytes" << std::endl;
        
        // 这里可以将数据存储到对应的 piece 缓冲区中
        std::vector<std::uint8_t> piece_data(payload.begin() + 8, payload.end());
        set_piece_buffer(index, piece_data);
        // 标记该 piece 已下载
        set_piece(index);
        // 发送 have 消息通知对方
        send_have(index);
    }

    void set_piece_buffer(int index, const std::vector<std::uint8_t>& buffer) {
        int pos = index * 16384; // 每个 piece 的大小为 16KB
        if (piece_buffer.size() < pos + buffer.size()) {
            piece_buffer.resize(pos + buffer.size());
        }
        std::memcpy(piece_buffer.data() + pos, buffer.data(), buffer.size());
        std::cout << "已存储 Piece 索引: " << index << " 的数据, 大小: " << buffer.size() << " bytes" << std::endl;
    }

    // 发送have 消息
    bool send_have(uint32_t index) {
        uint8_t msg[9] = {0};
        // 1. 长度前缀 (5 = 1 + 4)
        msg[0] = 0;
        msg[1] = 0;     
        msg[2] = 0;
        msg[3] = 5;
        // 2. 消息 ID (4 = Have)
        msg[4] = 4;
        // 3. 填充 Payload (大端序)
        msg[5] = (index >> 24) & 0xFF;
        msg[6] = (index >> 16) & 0xFF;
        msg[7] = (index >> 8) & 0xFF;   
        msg[8] = index & 0xFF;
        
        return send_raw(msg, 9);
    }


    private:
    int socket_fd;
    bool choked;
    std::vector<std::uint8_t> local_bit_field;
    std::vector<std::uint8_t> bit_field;
    std::array<std::uint8_t, 20> peer_id;
    std::array<std::uint8_t, 20> info_hash;
    Peer *peer;
    std::vector<std::uint8_t> piece_buffer; // 用于存储接收到的 piece 数据

};

#endif