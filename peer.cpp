#include "peer.h"
#include "client.h"
#include <thread>

void Peer::run()
{
    connect();
    handshake();
    if (!recv_bitfield())
    {
        std::cerr << "Failed to receive bitfield from peer: " << to_string() << std::endl;
        return;
    }
    if (!send_interested())
    {
        return;
    }
    for (;;)
    {
        auto i = client.get_task();
        if (i == -1)
        {
            std::cout << "[" << std::this_thread::get_id() << "][Peer] 没有更多任务，退出线程" << std::endl;
            break; // 没有更多任务，退出线程
        }
        auto res = process_incoming_message(i);
        if (!res)
        {
             client.reset_task(i); // 重置任务状态
            std::cerr << "Failed to process incoming message from peer: " << to_string() << std::endl;
            break;
        }
    }
}

void Peer::connect()
{
    // 创建 socket
    socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        std::cerr << "创建 socket 失败\n";
        return;
    }

    // 设置 sockaddr_in 结构体
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_());
    addr.sin_addr.s_addr = *(uint32_t *)ip.data();

    // 连接到 peer
    if (::connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "连接到 peer " << to_string() << " 失败\n";
        ::close(socket_fd);
        return;
    }
}
bool Peer::read_exact(uint8_t *buffer, size_t len)
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

void Peer::handshake()
{
    // 构建 handshake 消息
    std::vector<uint8_t> handshake_msg(68);

    handshake_msg[0] = 19;                                                     // pstrlen
    auto info_hash = client.get_info_hash();                                   // 获取 info_hash
    const std::string pstr = "BitTorrent protocol";                            // pstr
    std::copy(pstr.begin(), pstr.end(), handshake_msg.begin() + 1);            // pstr
    std::fill(handshake_msg.begin() + 20, handshake_msg.begin() + 28, 0);      // reserved
    std::copy(info_hash.begin(), info_hash.end(), handshake_msg.begin() + 28); // info_hash
    std::copy(peer_id.begin(), peer_id.end(), handshake_msg.begin() + 48);     // peer_id
    // 发送 handshake 消息
    if (!send_raw(handshake_msg.data(), handshake_msg.size()))
    {
        std::cerr << "发送 handshake 消息失败\n";
        return;
    }
    // 接收 handshake 响应
    std::vector<uint8_t> response(68);
    if (!read_exact(response.data(), response.size()))
    {
        std::cerr << "接收 handshake 响应失败\n";
        return;
    }
    if (!std::equal(info_hash.begin(), info_hash.end(), response.begin() + 28))
    {
        std::cerr << "info_hash 不匹配\n";
        return;
    }
}

void Peer::handle_piece_message(const std::vector<uint8_t> &payload)
{
    if (payload.size() < 8)
        return; // 至少需要 4(index) + 4(begin) 字节

    uint32_t index = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
    uint32_t begin = (payload[4] << 24) | (payload[5] << 16) | (payload[6] << 8) | payload[7];

    // std::cout << "[" << std::this_thread::get_id() << "][Peer] 收到 Piece 数据: Index=" << index
    //           << ", Offset=" << begin
    //           << ", Size=" << (payload.size() - 8) << " bytes" << std::endl;

    // 这里可以将数据存储到对应的 piece 缓冲区中
    if (piece_buffer.size() < begin + (payload.size() - 8))
    {
        piece_buffer.resize(begin + (payload.size() - 8));
    }

    std::copy(payload.begin() + 8, payload.end(), piece_buffer.begin() + begin);
    // 检查是否已经接收完整个 piece，如果是，则调用 Client 的 set_piece_buffer 方法
    if (begin + (payload.size() - 8) >= client.get_piece_len())
    {
        client.set_piece_buffer(index, piece_buffer);
        send_have(index);
    }
}
bool Peer::send_interested()
{
    // 消息格式：4 字节长度 + 1 字节 ID (Interested 的 ID 是 2)
    std::cout << "[" << std::this_thread::get_id() << "][Peer] 发送感兴趣消息\n";

    uint8_t msg[5] = {0, 0, 0, 1, 2};
    if (!send_raw(msg, 5))
    {
        std::cout << "[" << std::this_thread::get_id() << "][Peer] 发送 Interested 消息失败\n";
        return false;
    }
    return true;
}
bool Peer::send_request(uint32_t index, uint32_t begin, uint32_t length)
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

bool Peer::recv_bitfield()
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
    bit_field.insert(bit_field.end(), payload.begin(), payload.end());
    return true;
}
bool Peer::process_incoming_message(int i)
{
    auto thread_id = std::this_thread::get_id();
    auto len = client.get_piece_len();
    // 1. 读取 4 字节的长度前缀 (大端序)
    uint8_t len_buf[4];
    if (!read_exact(len_buf, 4))
        return false;

    uint32_t msg_len = (len_buf[0] << 24) | (len_buf[1] << 16) | (len_buf[2] << 8) | len_buf[3];

    // 2. 处理 Keep-Alive 消息 (长度为 0)
    if (msg_len == 0)
    {
        std::cout << "[" << thread_id << "][Peer] 收到 Keep-Alive 消息" << std::endl;
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
    case 0: // Choke
        std::cout << "[" << thread_id << "][Peer]" << to_string() << " 收到 Choke (被阻塞)" << std::endl;
        break;
    case 1: // Unchoke
    {
        if ((bit_field[i / 8] & (1 << (7 - (i % 8)))) == 0)
        {
            std::cout << "[" << thread_id << "][Peer] Piece " << i << ": 对方没有该分片，跳过" << std::endl;
        }
        else
        {
            for (int offset = 0; offset < len; offset += 16384)
            {
                std::cout << "[" << thread_id << "][Peer] 发送请求: Piece Index=" << i << ", Offset=" << offset << ", Length=" << len << std::endl;
                uint32_t request_length = std::min(static_cast<uint32_t>(16384), static_cast<uint32_t>(len - offset));
                send_request(i, offset, request_length);
            }
        }
    }
    break;
    case 2:
        std::cout << "[" << thread_id << "][Peer] 收到 Interested (对方感兴趣)" << std::endl;
        break;
    case 7: // Piece (实际数据)
        handle_piece_message(payload);
        // 是否已经下载完成
        if (client.is_bit_set(i)) {
            auto i = client.get_task();
            if (i != -1)
            {
                if ((bit_field[i / 8] & (1 << (7 - (i % 8)))) == 0)
                {
                    std::cout << "[" << thread_id << "][Peer] Piece " << i << ": 对方没有该分片，跳过" << std::endl;
                }
                else
                {
                    for (int offset = 0; offset < len; offset += 16384)
                    {
                        std::cout << "[" << thread_id << "][Peer] 发送请求: Piece Index=" << i << ", Offset=" << offset << ", Length=" << len << std::endl;
                        uint32_t request_length = std::min(static_cast<uint32_t>(16384), static_cast<uint32_t>(len - offset));
                        send_request(i, offset, request_length);
                    }
                }
            }

        }
        break;
    default:
        std::cout << "[" << thread_id << "][Peer] 收到未处理的消息 ID: " << (int)msg_id << std::endl;
        break;
    }
    return true;
}

void Peer::send_have(uint32_t index)
{
    // Have 消息格式：4字节长度(5) + 1字节ID(4) + 4字节index
    uint8_t msg[9] = {0};

    // 1. 长度前缀 (5 = 1 + 4)
    msg[0] = 0;
    msg[1] = 0;
    msg[2] = 0;
    msg[3] = 5;

    // 2. 消息 ID (4 = Have)
    msg[4] = 4;

    // 3. 填充 Payload (大端序)
    msg[5] = (index >> 24) & 0xFF;
    msg[6] = (index >> 16) & 0xFF;
    msg[7] = (index >> 8) & 0xFF;
    msg[8] = index & 0xFF;

    if (!send_raw(msg, 9))
    {
        std::cerr << "发送 Have 消息失败\n";
        return;
    }
    else
    {
        std::cout << "[" << std::this_thread::get_id() << "][Peer] 已发送 Have 消息: Index=" << index << std::endl;
    }
}