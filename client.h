#ifndef CLIENT_H
#define CLIENT_H
#include "peer.h"
#include <vector>
#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <mutex>

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

class Client : public std::enable_shared_from_this<Client> {
    public:
    void set_bit(int idx);
    bool is_bit_set(int idx);
    void set_piece(int idx);

    void add_peers(std::vector<Peer>&& peers_) {
        peers = std::move(peers_);
    }

    std::vector<Peer> get_peers(Torrent &torrent);

    Client(std::vector<std::uint8_t> &peer_id_, const std::vector<std::uint8_t> &info_hash_):peer_id(peer_id_), info_hash(info_hash_){
    }

    void set_piece_buffer(int index, const std::vector<std::uint8_t>& buffer) {
        int pos = index * 16384; // 每个 piece 的大小为 16KB
        if (piece_buffer.size() < pos + buffer.size()) {
            piece_buffer.resize(pos + buffer.size());
        }
        std::memcpy(piece_buffer.data() + pos, buffer.data(), buffer.size());
        std::cout << "已存储 Piece 索引: " << index << " 的数据, 大小: " << buffer.size() << " bytes" << std::endl;
    }

    bool download();

    bool download_finish() {
        // 检查所有 piece 是否都已下载
        for (size_t i = 0; i < local_bit_field.size() * 8; ++i) {
            if (!is_bit_set(i)) {
                std::cout << "Piece " << i << " 尚未下载完成" << std::endl;
                return false;
            }
        }
        return true;
    }


    std::vector<std::uint8_t>& get_info_hash() {
        return info_hash;
    }

    private:
    std::vector<std::uint8_t> peer_id;
    std::vector<std::uint8_t> local_bit_field;
    std::vector<Peer> peers;
    std::vector<std::uint8_t> info_hash;
    std::vector<std::uint8_t> piece_buffer; // 用于存储接收到的 piece 数据

};

#endif