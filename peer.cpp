#include "peer.h"
#include "utils.h"
#include <thread>
#include "client.h"

void Peer::run()
{
    if (!connect())
        return;
    LOG_INFO("[{}] Peer {} 连接成功", thread_id_str(std::this_thread::get_id()), to_string());
    auto connection = client->get_connection();
    auto peer = connection->get_peer(socket_fd);

    // 5. 等待事件发生并处理消息
    while (true) {
        connection->wait_for_events();
        // 6. 处理所有事件
        process_events();
        // 7. 检查是否完成任务
        if (is_task_complete()) {
            break;
        }
    }
}

bool Peer::connect()
{
    // 创建 socket mac 上 SOCK_NONBLOCK 不支持，使用 fcntl 设置非阻塞
    socket_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_fd < 0)
    {
        std::cerr << "创建 socket 失败\n";
        return false;
    }

    // 设置 sockaddr_in 结构体
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_());
    addr.sin_addr.s_addr = *(uint32_t *)ip.data();

    // 连接到 peer
    auto connection = client->get_connection();
    if (::connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        // EINPROGRESS 表示连接正在进行中，这是非阻塞模式下的正常情况
        if (errno != EINPROGRESS && errno != EWOULDBLOCK)
        {
            log_with_thread_id("connect socket_fd failed");
            ::close(socket_fd);
            return false;
        }
        if (!connection->add(socket_fd, EPOLLOUT | EPOLLET, this))
        {
            ::close(socket_fd);
            return false;
        }
    }
    else
    {
        // 这里基本只能是回环连接成功的情况，直接添加到 epoll 监听
        if (!connection->add(socket_fd, EPOLLIN | EPOLLET, this))
        {
            LOG_WARN("Failed to add socket_fd {} to epoll", socket_fd);
            ::close(socket_fd);
            return false;
        }
    }
    return true;
}
bool Peer::read_exact(uint8_t *buffer, size_t len)
{
    size_t received = 0;
    while (received < len)
    {
        ssize_t n = ::recv(socket_fd, reinterpret_cast<char *>(buffer + received), len - received, 0);
        if (n <= 0)
        {
            if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                continue;
            }
            else
            {
                log_with_thread_id("recv failed or connection closed");
            }
            return false; // 连接断开或出错
        }
        received += n;
    }
    return true;
}

void Peer::handshake()
{
    // 构建 handshake 消息
    std::vector<uint8_t> handshake_msg(68);

    handshake_msg[0] = 19;                                                     // pstrlen
    auto info_hash = client->get_info_hash();                                  // 获取 info_hash
    const std::string pstr = "BitTorrent protocol";                            // pstr
    std::copy(pstr.begin(), pstr.end(), handshake_msg.begin() + 1);            // pstr
    std::fill(handshake_msg.begin() + 20, handshake_msg.begin() + 28, 0);      // reserved
    std::copy(info_hash.begin(), info_hash.end(), handshake_msg.begin() + 28); // info_hash
    std::copy(peer_id.begin(), peer_id.end(), handshake_msg.begin() + 48);     // peer_id
    // 发送 handshake 消息
    if (!send_raw(handshake_msg.data(), handshake_msg.size()))
    {
        return;
    }
}

bool Peer::handle_piece_message(const std::vector<uint8_t> &payload)
{
    if (payload.size() < 8)
    {
        log_with_thread_id("payload 字节不足8");
        return false; // 至少需要 4(index) + 4(begin) 字节
    }

    uint32_t index = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
    uint32_t begin = (payload[4] << 24) | (payload[5] << 16) | (payload[6] << 8) | payload[7];

    // 这里可以将数据存储到对应的 piece 缓冲区中
    if (piece_buffer.size() < begin + (payload.size() - 8))
    {
        piece_buffer.resize(begin + (payload.size() - 8));
    }

    std::copy(payload.begin() + 8, payload.end(), piece_buffer.begin() + begin);
    // 检查是否已经接收完整个 piece，如果是，则调用 Client 的 set_piece_buffer 方法
    if (begin + (payload.size() - 8) >= client->get_piece_len())
    {
        client->set_piece_buffer(index, piece_buffer);
        piece_buffer.clear(); // 清空缓冲区，为下一个 piece 做准备
        // send_have(index);  // 纯下载不需要发送have 消息
        // 分片下载完成
        return true;
    }
    else
    {
        // 未完成
        return false;
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
    LOG_INFO("[{}] Peer {} 发送 Request 消息: index={}, begin={}, length={}", thread_id_str(std::this_thread::get_id()), to_string(), index, begin, length);
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

bool Peer::process_incoming_message(const std::vector<uint8_t> &data)
{
    bool finished = false;
    auto thread_id = std::this_thread::get_id();
    auto len = client->get_piece_len();

        // 3. 验证消息长度和结构
        if (data.size() < 1) {
            LOG_WARN("[{}] 收到无效消息: 数据长度不足", thread_id_str(std::this_thread::get_id()));
            return false;
        }
        
        // 3. 读取 1 字节的消息 ID
        uint8_t msg_id = data[0];
        std::vector<uint8_t> payload = std::vector<uint8_t>(data.begin() + 1, data.end());
        
        // 4. 验证 payload 长度 (根据消息类型)
        switch (msg_id) {
            case 3: // Piece 消息需要至少 9 字节 (index:4, begin:4, block:1+)
                if (payload.size() < 9) {
                    LOG_WARN("[{}] 收到无效 Piece 消息: 数据不足 {} bytes", thread_id_str(std::this_thread::get_id()), 9);
                    return false;
                }
                break;
            // 可以添加其他消息类型的验证...
        }
        
        switch (msg_id)
    {
    case 0: // Choke
        std::cout << "[" << thread_id << "][Peer]" << to_string() << " 收到 Choke (被阻塞)" << std::endl;
        break;
    case 1: // Unchoke
    {
        LOG_INFO("[{}] Peer {} 收到 Unchoke (解除阻塞)", thread_id_str(std::this_thread::get_id()), to_string());
        auto i = client->get_task();
        for (int offset = 0; offset < len; offset += 16384)
        {
            uint32_t request_length = std::min(static_cast<uint32_t>(16384), static_cast<uint32_t>(len - offset));
            send_request(i, offset, request_length);
        }
    }
    break;
    case 2:
        LOG_INFO("[{}] Peer {} 收到 Interested (对方感兴趣)", thread_id_str(std::this_thread::get_id()), to_string());
        break;
    // 握手消息
    case 3:
        LOG_INFO("[{}] Peer {} 收到 Not Interested (对方不感兴趣)", thread_id_str(std::this_thread::get_id()), to_string());
        break;
    case 4:
        LOG_INFO("[{}] Peer {} 收到 Have 消息", thread_id_str(std::this_thread::get_id()), to_string());
        break;
    case 5:
        LOG_INFO("[{}] Peer {} 收到 Bitfield 消息", thread_id_str(std::this_thread::get_id()), to_string());
        bit_field.insert(bit_field.end(), payload.begin(), payload.end());
        send_interested(); // 发送感兴趣消息
        break;
    case 7:
    { // Piece (实际数据)
        LOG_INFO("[{}] Peer {} 收到 Piece 消息", thread_id_str(std::this_thread::get_id()), to_string());
        finished = handle_piece_message(payload);
        // 是否已经下载完成
        if (finished)
        {
            auto i = client->get_task();
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
                        uint32_t request_length = std::min(static_cast<uint32_t>(16384), static_cast<uint32_t>(len - offset));
                        send_request(i, offset, request_length);
                    }
                }
            }
        }
        break;
    }
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

  void Peer::read_data(int fd)
    {
        // 读取数据的缓冲区
        // Non-blocking read with timeout
        constexpr auto timeout = std::chrono::seconds(1);
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            uint8_t buffer[4096];
            ssize_t bytes_received = ::recv(fd, buffer, sizeof(buffer), 0);
            if (bytes_received > 0)
            {
                recv_buffer.insert(recv_buffer.end(), buffer, buffer + bytes_received);
                memset(buffer, 0, sizeof(buffer)); // 清空缓冲区
            } 
            else if (bytes_received == 0)
            {
                LOG_INFO("[{}] Peer {} 对端关闭连接", thread_id_str(std::this_thread::get_id()), to_string());
                close_conn(fd);
                break;
            } 
            else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                close_conn(fd);
                return;
            }
        }

        if (handshake_state == 0)
        {
            if (recv_buffer.size() >= 68)
            {
                std::vector<uint8_t> handshake_msg(recv_buffer.begin(), recv_buffer.begin() + 68);
                if (valid_handshake(handshake_msg))
                {
                    handshake_state = 2; // 握手成功
                    LOG_INFO("[{}] Peer {} 握手成功", thread_id_str(std::this_thread::get_id()), to_string());
                    recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 68);
                }
                else
                {
                    LOG_WARN("[{}] Peer {} 握手失败", thread_id_str(std::this_thread::get_id()), to_string());
                    close_conn(fd);
                    return;
                }
            }
        }
        if (handshake_state == 2)
        {
            if (recv_buffer.size() >= 4)
            {
                uint32_t msg_len = (recv_buffer[0] << 24) | (recv_buffer[1] << 16) | (recv_buffer[2] << 8) | recv_buffer[3];
                std::vector<uint8_t> msg(recv_buffer.begin() + 4, recv_buffer.begin() + 4 + msg_len);
                process_incoming_message(msg);
                recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + msg_len);
            }
        }
    }

    void Peer::close_conn(int fd)
    {
        ::close(fd);
        LOG_INFO("[{}] Peer {} 关闭连接", thread_id_str(std::this_thread::get_id()), to_string());
        auto conn = client->get_connection();
        conn->del(fd);
    }