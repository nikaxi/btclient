#ifndef CLIENT_H
#define CLIENT_H
#include "peer.h"
#include <vector>
#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <mutex>
#include "torrent.h"
#include <thread>

// 跨平台 Socket 头文件
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

const int PORT = 6882;

class Client : public std::enable_shared_from_this<Client>
{
public:
    void set_bit(int idx);
    bool is_bit_set(int idx);


    void add_peers(std::vector<Peer> &&peers_)
    {
        peers = std::move(peers_);
    }

    std::vector<Peer> get_peers(Torrent &torrent);

    void set_local_bit_field(const Torrent &torrent)
    {
        auto bitfield_size = (torrent.info.pieces.size() + 7) / 8; // 计算 bitfield 的字节数
        local_bit_field.resize(bitfield_size, 0); // 初始化为全 0，
    }

    Client(std::vector<std::uint8_t> &peer_id_, const std::vector<std::uint8_t> &info_hash_) : peer_id(peer_id_), info_hash(info_hash_)
    {
    }

    void set_piece_buffer(int index, const std::vector<std::uint8_t> &buffer)
    {
        {
            std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
            int pos = index * piece_length;      
            if (piece_buffer.size() < pos + buffer.size())
            {
                piece_buffer.resize(pos + buffer.size());
            }
            std::memcpy(piece_buffer.data() + pos, buffer.data(), buffer.size());
            auto total = local_bit_field.size() * 8;
            std::cout << "[" << std::this_thread::get_id() << "][Client] 已存储 Piece 索引: " << index << " 的数据, 大小: " << buffer.size() << " bytes" << std::endl;
            std::cout << "[" << std::this_thread::get_id() << "][Client] 当前下载进度: " << progress++ << " / " << total  << std::endl;
        }
        set_bit(index); // 标记该 piece 已下载
    }

    void download();

    bool download_finish();

    int get_progress()
    {
        std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
        return progress;
    }

    void print_local_bitfield()
    {
        std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
        std::cout << "Local Bitfield: ";
        for (const auto &byte : local_bit_field)
        {
            for (int i = 7; i >= 0; --i)
            {
                std::cout << ((byte >> i) & 1);
            }
            std::cout << " ";
        }
        std::cout << std::endl;
    }

    int get_task();

    std::vector<std::uint8_t> &get_info_hash()
    {
        return info_hash;
    }

    int get_piece_len() const
    {
        return piece_length;
    }

    void reset_task(int index)
    {
        std::lock_guard<std::mutex> lock(mtx); // 确保线程安全
        tasks.erase(index); // 移除任务状态，表示该 piece 可以重新下载
    }

private:
    void set_bit_internal(int idx);
    bool is_bit_set_internal(int idx);
    std::vector<std::uint8_t> peer_id;
    std::vector<std::uint8_t> local_bit_field;
    std::vector<Peer> peers;
    int progress = 0; // 下载进度，已下载的 piece 数量
    std::vector<std::uint8_t> info_hash;
    std::vector<std::uint8_t> piece_buffer; // 用于存储接收到的 piece 数据
    std::mutex mtx;                         // 用于保护 local_bit_field 和 piece_buffer 的互斥锁
    std::map<int, int> tasks; // 用于存储每个 piece的下载状态，key 是 piece 索引，value 是下载状态（1: 下载中, 2: 已下载）
    int piece_length; // 每个 piece 的长度
};

#endif