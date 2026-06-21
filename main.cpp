#include <iostream>
#include <fstream>
#include "bencode.hpp"
#include "utils.h"
#include "client.h"
#include "peer.h"
#include "torrent.h"



using namespace bencode;

extern const int PORT;




// "d8:announce18:http://tracker.com10:created by14:KTorrent 2.1.413:creation datei1182163277ee"
// /home/nikaxi/cpp-bencoding-master/sample/inputs/sample1.torrent
int main() {
    std::string torrent_file_path = "/home/nikaxi/debian-12.11.0-amd64-netinst.iso.torrent";
    try {
        // read from file
        std::ifstream f(torrent_file_path, std::ios::binary);
        Torrent torrent(f);
        // download pieces
        std::cout << "announce: " << torrent.announce << std::endl;

        Peer *peer = nullptr;
        std::array<uint8_t, 20> peer_id = generate_peer_id();
        std::array<uint8_t, 20> info_hash;

        auto encoded_info = bencode::encode(bencode::dict{
            {bencode::string("name"), bencode::data(torrent.info.name)},
            {bencode::string("piece length"), bencode::data(static_cast<long long>(torrent.info.piece_length))},
            {bencode::string("length"), bencode::data(static_cast<long long>(torrent.info.length))},
            {bencode::string("pieces"), bencode::data(std::string(torrent.info.pieces.begin(), torrent.info.pieces.end()))}
        });

        SHA1(reinterpret_cast<const unsigned char*>(encoded_info.data()), encoded_info.size(), 
                         reinterpret_cast<unsigned char*> (info_hash.data()));
        torrent.info_hash = info_hash;

        Client client(peer_id, torrent.info_hash);

        std::vector<string> peers = client.get_peers(torrent);

        for (auto &peer_str: peers) { 
            std::cout << "peer: " << peer_str << std::endl;
        }





    } catch (const std::exception &ex) {
        std::cerr << "error:" << ex.what() ;
        return 1;
    }
}