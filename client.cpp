#include "client.h"
#include "peer.h"
#include <random>
#include "httplib.h"
#include "utils.h"
#include <fstream>
#include "torrent.h"
#include "bencode.hpp"
#include "utils.h"
using namespace bencode;

bool Client::process_incoming_message()
{
    // 1. 读取 4 字节的长度前缀 (大端序)
    uint8_t len_buf[4];
    if (!read_exact(len_buf, 4))
        return false;

    uint32_t msg_len = (len_buf[0] << 24) | (len_buf[1] << 16) | (len_buf[2] << 8) | len_buf[3];

    // 2. 处理 Keep-Alive 消息 (长度为 0)
    if (msg_len == 0)
    {
        std::cout << "[Peer] 收到 Keep-Alive 消息" << std::endl;
        return true;
    }

    // 3. 读取 1 字节的消息 ID
    uint8_t msg_id;
    if (!read_exact(&msg_id, 1))
        return false;

    // 4. 读取剩余的 Payload
    uint32_t payload_len = msg_len - 1;
    std::vector<uint8_t> payload(payload_len);
    if (payload_len > 0 && !read_exact(payload.data(), payload_len))
        return false;

    // 5. 根据 ID 分发处理
    switch (msg_id)
    {
    case 0:
        std::cout << "[Peer] 收到 Choke (被阻塞)" << std::endl;
        break;
    case 1: // Unchoke
        std::cout << "[Peer] 收到 Unchoke (解除阻塞)，可以请求数据了！" << std::endl;
        for (int i = 0; i < bit_field.size() * 8; ++i) {
            send_request(i, 0, 16384); // 请求前 5 个 Piece 的前 16KB 数据
        }
        break;
    case 2: // Unchoke
        std::cout << "[Peer] 收到 Interested (对方感兴趣)" << std::endl;
        break;
    case 7: // Piece (实际数据)
        handle_piece_message(payload);
        break;
    default:
        std::cout << "[Peer] 收到未处理的消息 ID: " << (int)msg_id << std::endl;
        break;
    }
    return true;
}

bool Client::connect()
{
    socket_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd == -1)
        return false;
    int port = peer->port_();
    std::string ip = peer->str_ip();

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    std::cout << "🔗到 ip:" << ip << " port:" << port << "\n";

    return ::connect(socket_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0;
}

// 内部辅助：从 TCP 流中精确读取指定长度的数据
bool Client::read_exact(uint8_t *buffer, size_t len)
{
    size_t received = 0;
    while (received < len)
    {
        ssize_t n = ::recv(socket_fd, reinterpret_cast<char *>(buffer + received), len - received, 0);
        if (n <= 0)
            return false; // 连接断开或出错
        received += n;
    }
    return true;
}

void Client::set_piece(int idx)
{
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
    return static_cast<int>(local_bit_field[byte_idx] & static_cast<int>(1 << (7 - bit_idx))) != 0;
}

bool Client::handshake()
{
    // 构造 68 字节的握手包
    uint8_t handshake_msg[68] = {0};
    handshake_msg[0] = 19; // 协议名长度
    std::memcpy(&handshake_msg[1], "BitTorrent protocol", 19);
    // 8 字节保留位 (全 0)

    // 复制 Info Hash 和 Peer ID
    std::memcpy(&handshake_msg[28], info_hash.data(), 20);
    std::memcpy(&handshake_msg[48], peer_id.data(), 20);

    auto res = send_raw(handshake_msg, 68);
    if (!res)
    {
        std::cout << "发送握手消息失败:" << res << "\n";
        return false;
    }

    // 接收对方的握手包
    uint8_t recv_buf[68] = {0};
    ssize_t n = ::recv(socket_fd, reinterpret_cast<char *>(recv_buf), 68, 0);
    if (n != 68 || recv_buf[0] != 19)
    {
        return false;
    }
    std::cout << "完成接收握手消息包\n";
    // 校验对方返回的 Info Hash 是否一致
    return std::memcmp(&recv_buf[28], info_hash.data(), 20) == 0;
}

std::vector<Peer> Client::get_peers(Torrent &torrent)
{
    std::vector<Peer> peers_vec;
    // send request to announce_url
    std::string host = parse_announce_url(torrent.announce);
    httplib::Client cli(host);

    for (auto &byte : info_hash)
    {
        std::cout << static_cast<int>(byte) << " ";
    }

    std::string url = "/announce?compact=1&info_hash=" + url_encode(std::vector(info_hash.begin(), info_hash.end())) + "&peer_id=" + url_encode(std::vector(peer_id.begin(), peer_id.end())) + "&port=" + std::to_string(PORT) + "&uploaded=0&event=started&downloaded=0&left=" + std::to_string(torrent.info.length);

    std::cout << "URL: " << url << std::endl;

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
            peers_vec.push_back(Peer(ip, port));
        }
    }
    else
    {
        std::cout << "Error: " << res.error() << std::endl;
    }

    return peers_vec;
}
bool Client::send_interested()
{
    // 消息格式：4 字节长度 + 1 字节 ID (Interested 的 ID 是 2)
    std::cout << "发送感兴趣消息\n";
    uint8_t msg[5] = {0, 0, 0, 1, 2};
    if (!send_raw(msg, 5))
    {
        std::cout << "发送 Interested 消息失败\n";
        return false;
    }
    return true;
}

bool Client::send_request(uint32_t index, uint32_t begin, uint32_t length)
{
    // Request 消息格式：4字节长度(13) + 1字节ID(6) + 4字节index + 4字节begin + 4字节length
    uint8_t msg[17] = {0};

    // 1. 长度前缀 (13 = 1 + 4 + 4 + 4)
    msg[0] = 0;
    msg[1] = 0;
    msg[2] = 0;
    msg[3] = 13;
    // 2. 消息 ID (6 = Request)
    msg[4] = 6;

    // 3. 填充 Payload (大端序)
    msg[5] = (index >> 24) & 0xFF;
    msg[6] = (index >> 16) & 0xFF;
    msg[7] = (index >> 8) & 0xFF;
    msg[8] = index & 0xFF;

    msg[9] = (begin >> 24) & 0xFF;
    msg[10] = (begin >> 16) & 0xFF;
    msg[11] = (begin >> 8) & 0xFF;
    msg[12] = begin & 0xFF;

    msg[13] = (length >> 24) & 0xFF;
    msg[14] = (length >> 16) & 0xFF;
    msg[15] = (length >> 8) & 0xFF;
    msg[16] = length & 0xFF;

    return send_raw(msg, 17);

    return true;
}

bool Client::recv_bitfield()
{
    // 1. 读取 4 字节的长度前缀 (大端序)
    uint8_t len_buf[4];
    if (!read_exact(len_buf, 4))
        return false;

    uint32_t msg_len = (len_buf[0] << 24) | (len_buf[1] << 16) | (len_buf[2] << 8) | len_buf[3];

    // 3 读取 1 字节的消息 ID
    uint8_t msg_id;
    if (!read_exact(&msg_id, 1))
        return false;

    // 4. 读取剩余的 Payload
    uint32_t payload_len = msg_len - 1;
    std::vector<uint8_t> payload(payload_len);
    if (payload_len > 0 && !read_exact(payload.data(), payload_len))
        return false;

    // 解析 Bitfield 消息的 Payload
    bit_field.clear();
    bit_field.insert(bit_field.end(), payload.begin(), payload.end());
    return true;
}
