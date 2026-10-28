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

void Client::download()
{
    connection->start_worker_pool(10);
    connection->run();
}

void Client::set_bit_internal(int idx)
{
    auto byte_idx = idx / 8;
    auto bit_idx = idx % 8;
    if (byte_idx >= local_bit_field.size())
    {
        local_bit_field.resize(byte_idx + 1, 0);
    }

    local_bit_field[byte_idx] |= static_cast<std::uint8_t>(1 << (7 - bit_idx));
}

bool Client::is_bit_set_internal(int idx)
{
    auto byte_idx = idx / 8;
    auto bit_idx = idx % 8;
    if (byte_idx >= local_bit_field.size())
    {
        return false;
    }
    return static_cast<std::uint8_t>(local_bit_field[byte_idx] & static_cast<std::uint8_t>(1 << (7 - bit_idx))) != 0;
}


void Client::set_bit(int idx)
{
    std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
    auto byte_idx = idx / 8;
    auto bit_idx = idx % 8;
    if (byte_idx >= local_bit_field.size())
    {
        local_bit_field.resize(byte_idx + 1, 0);
    }

    local_bit_field[byte_idx] |= static_cast<std::uint8_t>(1 << (7 - bit_idx));
}

bool Client::is_bit_set(int idx)
{
    std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
    auto byte_idx = idx / 8;
    auto bit_idx = idx % 8;
    if (byte_idx >= local_bit_field.size())
    {
        return false;
    }
    return static_cast<int>(local_bit_field[byte_idx] & static_cast<int>(1 << (7 - bit_idx))) != 0;
}

void Client::get_peers(Torrent &torrent)
{
    // send request to announce_url
    std::string host = parse_announce_url(torrent.announce);
    httplib::Client cli(host);
    cli.set_follow_location(true); // follow redirects 解决302问题
    std::string url = "/announce?compact=1&info_hash=" + url_encode(std::vector(torrent.info_hash.begin(), torrent.info_hash.end())) + "&peer_id=" + url_encode(std::vector(peer_id.begin(), peer_id.end())) + "&port=" + std::to_string(PORT) + "&uploaded=0&event=started&downloaded=0&left=" + std::to_string(torrent.info.length);

    httplib::Result res = cli.Get(url);
    if (res && res->status == 200)
    {
        auto d = bencode::decode(res->body);
        // interval 900 秒
        auto interval = std::get_if<bencode::integer>(&d["interval"]);
        auto peers_data = std::get_if<bencode::string>(&d["peers"]);

        const char* raw = peers_data->data();
        auto len = peers_data->size();
        std::vector<std::shared_ptr<Peer>> peers;
        for (auto i = 0; i+6 <= len; i += 6)
        {
            std::array<std::byte, 4> ip;
            std::array<std::byte, 2> port;
            for (auto j = 0; j < 4; j++)
            {
                ip[j] = static_cast<std::byte>(raw[i + j]);
            }
            for (auto j = 0; j < 2; j++)
            {
                port[j] = static_cast<std::byte>(raw[i + j + 4]);
            }
            peers.push_back(std::make_shared<Peer>(ip, port, this));
        }
        LOG_INFO("获取到 {} 个 peers", peers.size());
        // 将 局部的peers 添加到 PeerConnection 中，并且只能是移动的方式
        connection->add_peers(std::move(peers)); // 将 peers 添加到 PeerConnection 中
    }
    else
    {
        std::cout << "Error: " << res.error() << std::endl;
    }
}


bool Client::download_finish()
{
    // 检查所有 piece 是否都已下载, piece 的数能模8是补0的，所以需要跟piece总数比较
    size_t total_pieces = torrent->info.pieces.size() / 20;
    for (size_t i = 0; i < total_pieces ; ++i)
    {
        if (!is_bit_set(i))
        {
            return false;
        } 
    }
    std::cout << "所有 Piece 已下载完成！" << std::endl;
    return true;
}


int Client::get_task()
{
    std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
    auto thread_id = thread_id_str(std::this_thread::get_id());
    for (size_t i = 0; i < local_bit_field.size() * 8 ; ++i)
    {
        if (is_bit_set_internal(i)){
            continue;
        } 
        if (tasks.find(i) != tasks.end()) {
            if (tasks.at(i) == 1) {
                continue;
            }
        } else {
            tasks[i] = 1;
            LOG_INFO("[{}] get task: {}", thread_id, i);
            return i;
        }
    }
    return -1;
}