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
#include <condition_variable>


using namespace bencode;

void Client::download()
{
    connection->run();
}

bool Client::is_piece_complete(size_t piece_idx)
{ 
    // 起始位置
    auto bit_idx = piece_idx * 8; 
    for (auto i = bit_idx; i < bit_idx + 8; i++)
    {
        if (!is_bit_set(i))  {
            return false;
        }
    }
    return true;
}


void Client::set_bit(size_t idx)
{
    auto byte_idx = idx / 8;
    auto bit_idx = idx % 8;
    if (byte_idx >= local_bit_field.size())
    {
        local_bit_field.resize(byte_idx + 1, 0);
    }

    local_bit_field[byte_idx] |= static_cast<std::uint8_t>(1 << (7 - bit_idx));
}

bool Client::is_bit_set(size_t idx)
{
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

        // 初始化任务队列
        for (int i=0; i< torrent.info.pieces.size(); ++i) {
            add_task(i);
        }
    }
    else
    {
        std::cout << "Error: " << res.error() << std::endl;
    }
    piece_length = torrent.info.piece_length; // 设置 piece_length
}


bool Client::download_finish()
{
    int count = 0;
    // 检查所有 piece 是否都已下载
    for (size_t i = 0; i < local_bit_field.size() * 8; ++i)
    {
        if (is_bit_set(i))
        {
            count++;
        }
    }
    if (count < local_bit_field.size() * 8)
    {
        return false;
    }
    std::cout << "所有 Piece 已下载完成！" << std::endl;
    return true;
}

// 以piece下载为单位，不是piece内的16k block为单位下载
int Client::get_task()
{
    std::unique_lock<std::mutex> lock(mtx); // 确保线程安全
    task_cv.wait(lock, [this]{ return !task_queue.empty();}); 
    if (task_queue.empty()) {
        return -1;
    } 
    int task = task_queue.front();
    task_queue.pop();
    return task;
}

void Client::add_task(int task) {
    std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
    task_queue.push(task);
    task_cv.notify_one();
}