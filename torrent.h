// torrent.h
#ifndef TORRENT_H
#define TORRENT_H

#include <string>
#include <vector>
#include <cstddef>
#include <fstream>
#include <openssl/sha.h>
#include <array>

struct Info {
    std::string name;
    long piece_length;
    long length;
    std::vector<std::byte> pieces;
};

struct Torrent {
    Torrent(std::ifstream &f_stream);
    void set_hash();
    std::string announce;
    Info info;
    std::array<uint8_t, 20> info_hash;
};

#endif // TORRENT_H