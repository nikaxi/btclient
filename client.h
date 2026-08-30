#ifndef CLIENT_H
#define CLIENT_H

#include <vector>
#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include "peer.h"


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

    std::vector<Peer> get_peers(Torrent &torrent);

    Client(std::array<std::uint8_t, 20> &peer_id_, std::array<std::uint8_t, 20> &info_hash_):
        peer_id(peer_id_), info_hash(info_hash_), socket_fd(-1){}

    bool request_piece(int index, std::vector<std::uint8_t> &piece_hash);    

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

    bool connect() {
        sock_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_fd == -1) return false;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        return ::connect(sock_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    bool handshake();









    private:
    int socket_fd;
    bool choked;
    std::vector<std::byte> bit_field;
    std::array<std::uint8_t, 20> peer_id;
    std::array<std::uint8_t, 20> info_hash;
    Peer *peer;
};

#endif