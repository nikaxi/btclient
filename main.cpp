#include <iostream>
#include <fstream>
#include "bencode.hpp"
#include "utils.h"
#include "client.h"
#include "peer.h"
#include "torrent.h"
#include "log.h"



using namespace bencode;

extern const int PORT;




// "d8:announce18:http://tracker.com10:created by14:KTorrent 2.1.413:creation datei1182163277ee"
// /home/nikaxi/cpp-bencoding-master/sample/inputs/sample1.torrent
int main() {
    init_logging();  // 日志写入 btclient.log，stdout 只打印 info 及以上
    std::string torrent_file_path = "/home/nikaxi/debian.torrent";
    // std::string torrent_file_path = "/home/nikaxi/files.torrent";
    try {
        // read from file
        std::ifstream f(torrent_file_path, std::ios::binary);
        Torrent torrent(f);
        // download pieces
        std::cout << "announce: " << torrent.announce << std::endl;

        // 生成peer id标识自己
        std::vector<std::uint8_t> peer_id = generate_peer_id();

        std::vector<std::uint8_t> info_hash = std::vector<std::uint8_t>(torrent.info_hash.begin(), torrent.info_hash.end());
        Client client(peer_id, info_hash);

        client.set_local_bit_field(torrent);

        // announce 失败时重试，避免拿不到 peers 后仍然进入下载循环
        bool got_peers = false;
        for (int retry = 0; retry < 3 && !got_peers; ++retry)
        {
            got_peers = client.get_peers(torrent);
            if (!got_peers)
            {
                std::cout << "获取 peers 失败，5 秒后重试..." << std::endl;
                sleep(5);
            }
        }
        if (!got_peers)
        {
            std::cerr << "无法从 tracker 获取 peers，退出" << std::endl;
            return 1;
        }

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