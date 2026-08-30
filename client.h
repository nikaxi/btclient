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

    std::vector<std::string> get_peers(Torrent &torrent);

    Client(std::array<std::uint8_t, 20> &peer_id_, std::array<std::uint8_t, 20> &info_hash_):
        peer_id(peer_id_), info_hash(info_hash_){}
    
    bool request_piece(int index, std::vector<std::uint8_t> &piece_hash);    

    private:
    int socket;
    bool choked;
    std::vector<std::byte> bit_field;
    std::array<std::uint8_t, 20> peer_id;
    std::array<std::uint8_t, 20> info_hash;
    Peer *peer;
};

#endif