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

    bool has_piece(int idx);
    bool set_piece(int idx);

    vec_u8 recv_bitfield();

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

        std::cout << "send socket_fd:" << socket_fd;
        while (sent < len) {
            ssize_t res = ::send(socket_fd, data+sent, len - sent, 0);
            std::cout << "send res:" << res;
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
        
        // TODO: 1. 将 payload.data() + 8 的数据写入本地缓存
        // TODO: 2. 如果整个 Piece 下载完成，进行 SHA-1 校验
        // TODO: 3. 校验通过后，写入磁盘并发送 Have 消息给其他 Peer
    }


    private:
    int socket_fd;
    bool choked;
    std::vector<std::byte> bit_field;
    std::array<std::uint8_t, 20> peer_id;
    std::array<std::uint8_t, 20> info_hash;
    Peer *peer;
};

#endif