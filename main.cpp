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
    std::string torrent_file_path = "/home/nikaxi/debian.torrent";
    // std::string torrent_file_path = "/home/nikaxi/files.torrent";
    try {
        // read from file
        std::ifstream f(torrent_file_path, std::ios::binary);
        Torrent torrent(f);
        // download pieces
        std::cout << "announce: " << torrent.announce << std::endl;

        Peer *peer = nullptr;
        std::array<std::uint8_t, 20> peer_id = generate_peer_id();

        Client client(peer_id, torrent.info_hash);

        std::vector<Peer> peers = client.get_peers(torrent);


        // Print the list of peers

        // connect() ➔ handshake() ➔ exchange_bitfield() ➔ send_interested() ➔ wait_for_unchoke() ➔ loop { send_request() & process_incoming() }。
        for (auto &peer : peers) {
            client.set_peer(&peer);
            auto conn_res = client.connect();
            if (!conn_res) {
                std::cerr << "Failed to connect to peer: " << peer.to_string() << std::endl;
                continue;
            }
            client.handshake();
            client.process_incoming_message();
            for(;;) {
                std::cout << "Waiting for incoming messages from peer: " << peer.to_string() << std::endl;
                auto res = client.process_incoming_message();
                if (!res) {
                    std::cerr << "Failed to process incoming message from peer: " << peer.to_string() << std::endl;
                    break;
                }
            }
        }
        // 从pieces map 中获取每个piece的哈希值，发出请求，下载每个piece
        // for (const auto &[index, piece_hash] : torrent.info.pieces) {
        //     // 这里可以调用Client的请求方法，传入index和piece_hash
        //     // client.request_piece(index, piece_hash);
        // }
        

    } catch (const std::exception &ex) {
        std::cerr << "error:" << ex.what() ;
        return 1;
    }
}