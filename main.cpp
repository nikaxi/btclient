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

        std::vector<std::uint8_t> peer_id = generate_peer_id();

        std::vector<std::uint8_t> info_hash = std::vector<std::uint8_t>(torrent.info_hash.begin(), torrent.info_hash.end());
        Client client(peer_id, info_hash);

        client.set_local_bit_field(torrent);
        client.add_peers(client.get_peers(torrent));

        client.download();
        
        while(client.download_finish() == false) {
            std::cout <<"[" << std::this_thread::get_id() << "][Main Thread] 下载未完成，继续等待..." << client.get_progress() << std::endl;
            sleep(10); // 等待 5 秒后再次检查
        }

        std::cout << "下载完成！" << std::endl;
    } catch (const std::exception &ex) {
        std::cerr << "error:" << ex.what() ;
        return 1;
    }
}