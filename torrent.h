// torrent.h
#ifndef TORRENT_H
#define TORRENT_H

#include <string>
#include <vector>
#include <cstddef>
#include <fstream>
#include <openssl/sha.h>
#include <array>
#include <cstdint>
#include <map>
#include "bencode.hpp"

struct Info {
    std::string name;
    long piece_length;
    long length;
    std::map<std::uint32_t, std::vector<std::uint8_t>> pieces;
};

struct Torrent {
    Torrent(std::ifstream &f_stream);
    void set_hash(bencode::dict &info_dict);
    std::string announce;
    Info info;
    std::array<uint8_t, 20> info_hash;
};

#endif // TORRENT_H