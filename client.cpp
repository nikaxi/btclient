#include "client.h"
#include "peer.h"
#include <random>
#include "httplib.h"
#include "utils.h"
#include <fstream>
#include "torrent.h"
#include "bencode.hpp"
#include "utils.h"
#include <thread>
#include <mutex>

using namespace bencode;

std::mutex mtx; // 用于保护 local_bit_field 的互斥锁


bool Client::download() {
    for(auto &peer: peers) {
        // peer.run();
        std::thread t(&Peer::run, &peer);
        t.detach(); // 分离线程，让它在后台运行
    }
    return true;
}

// 内部辅助：从 TCP 流中精确读取指定长度的数据
void Client::set_piece(int idx)
{
    std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
    set_bit(idx);
    std::cout << "已下载 Piece 索引: " << idx << std::endl;
}

void Client::set_bit(int idx)
{
    
    auto byte_idx = idx / 8;
    auto bit_idx = idx % 8;
    if (byte_idx >= local_bit_field.size())
    {
        local_bit_field.resize(byte_idx + 1, 0);
    }

    std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
    local_bit_field[byte_idx] |= static_cast<std::uint8_t>(1 << (7 - bit_idx));
}


bool Client::is_bit_set(int idx)
{
    auto byte_idx = idx / 8;
    auto bit_idx = idx % 8;
    if (byte_idx >= local_bit_field.size())
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
    return static_cast<int>(local_bit_field[byte_idx] & static_cast<int>(1 << (7 - bit_idx))) != 0;
}


std::vector<Peer> Client::get_peers(Torrent &torrent)
{
    std::vector<Peer> peers_vec;
    // send request to announce_url
    std::string host = parse_announce_url(torrent.announce);
    httplib::Client cli(host);
    std::string url = "/announce?compact=1&info_hash=" + url_encode(std::vector(torrent.info_hash.begin(), torrent.info_hash.end())) + "&peer_id=" + url_encode(std::vector(peer_id.begin(), peer_id.end())) + "&port=" + std::to_string(PORT) + "&uploaded=0&event=started&downloaded=0&left=" + std::to_string(torrent.info.length);
    // std::cout << "URL: " << url << std::endl;

    cli.set_follow_location(true); // follow redirects
    httplib::Result res = cli.Get(url);

    if (res && res->status == 200)
    {
        auto decoded = bencode::decode(res->body);
        auto peers = std::get<bencode::string>(decoded["peers"]);
        for (auto i = 0; i < peers.size(); i += 6)
        {
            std::array<std::byte, 4> ip;
            std::array<std::byte, 2> port;
            for (auto j = 0; j < 4; j++)
            {
                ip[j] = static_cast<std::byte>(peers[i + j]);
            }
            for (auto j = 0; j < 2; j++)
            {
                port[j] = static_cast<std::byte>(peers[i + j + 4]);
            }
            // convert to string
            peers_vec.push_back(Peer(ip, port, *this));
        }
    }
    else
    {
        std::cout << "Error: " << res.error() << std::endl;
    }

    return peers_vec;
}

