#ifndef PEER_CONNECTION_H
#define PEER_CONNECTION_H

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>
#include <sys/epoll.h>
#include "log.h"

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
#include <mutex>
#include <unordered_map>
#include <memory>
#include "peer.h"
#include <fcntl.h>


class PeerConnection
{
private:
    int epoll_fd;
    std::mutex mtx;                      // 用于保护 epoll_fd 的互斥锁
    std::unordered_map<int, Peer *> fds; // 用于存储 socket_fd 与 Peer 对象的映射
public:
    explicit PeerConnection(int epoll_fd) : epoll_fd(epoll_fd) {}

    bool add(int fd, uint32_t events, Peer *peer)
    {
        std::lock_guard<std::mutex> lock(mtx);
        LOG_INFO("Adding fd {} to epoll {} with events {}", fd, epoll_fd, events);

        if (fd <= 0 || fd == epoll_fd)
        {
            LOG_ERROR("Invalid fd: {}", fd);
            return false;
        }

        // 验证 socket_fd 是否是合法的 socket
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        if (::getsockname(fd, (struct sockaddr *)&addr, &addr_len) == -1)
        {
            LOG_ERROR("fd is not a valid socket: {}", fd);
            return false;
        }
        else
        {
            LOG_INFO("fd {} is a valid socket", fd);
        }

        // 验证 epoll_fd 是否是合法的 epoll
        if (::fcntl(epoll_fd, F_GETFD) == -1)
        {
            LOG_ERROR("epoll_fd is invalid or closed: {}", epoll_fd);
            return false;
        } else {
            LOG_INFO("epoll_fd {} is valid", epoll_fd);
        }
        if (fds.find(fd) != fds.end())
        {
            LOG_ERROR("fd {} already exists in epoll", fd);
            return false;
        }
        struct epoll_event ev{};
        ev.events = EPOLLOUT | EPOLLET; // 边缘触发模式
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0)
        {
            LOG_ERROR("epoll_ctl failed: {} fd: {} epoll_fd: {}", strerror(errno), fd, epoll_fd);
            ::close(fd); // 关闭 socket_fd，防止资源泄漏
            return false;
        }
        fds[fd] = peer; // 将 socket_fd 与 Peer 对象关联
        return true;
    }

    Peer *get_peer(int fd)
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = fds.find(fd);
        if (it != fds.end())
        {
            return it->second;
        }
        return nullptr;
    }

    int get_epoll_fd() const
    {
        return epoll_fd;
    }

    // // 当有数据发送数据
    // bool send_data(const uint8_t *data, size_t len) {
    //     size_t total_sent = 0;
    //     while (total_sent < len) {
    //         ssize_t sent = ::send(sock_fd, reinterpret_cast<const char *>(data + total_sent), len - total_sent, 0);
    //         if (sent <= 0) {
    //             if (sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    //                 continue; // 非阻塞模式下，继续尝试发送
    //             } else {
    //                 std::cerr << "Send failed or connection closed" << std::endl;
    //                 return false; // 连接断开或出错
    //             }
    //         }
    //         total_sent += sent;
    //     }
    //     return true;
    // }
    // // 当有数据接收数据
    // bool recv_data(uint8_t *buffer, size_t len) {
    //     size_t total_received = 0;
    //     while (total_received < len) {
    //         ssize_t received = ::recv(sock_fd, reinterpret_cast<char *>(buffer + total_received), len - total_received, 0);
    //         if (received <= 0) {
    //             if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    //                 continue; // 非阻塞模式下，继续尝试接收
    //             } else {
    //                 std::cerr << "Recv failed or connection closed" << std::endl;
    //                 return false; // 连接断开或出错
    //             }
    //         }
    //         total_received += received;
    //     }
    //     return true;
    // }

    // // 检测事件 处理读写请求
    // void handle_events(uint32_t events) {
    //     // 从epoll中获取事件类型，进行相应的处理
    //     if (events & EPOLLIN) {
    //         // 处理可读事件
    //         std::cout << "Socket is readable" << std::endl;
    //         // 这里可以调用 recv_data 方法接收数据
    //         if (recv_data(recv_buffer, BUFFER_SIZE)) {
    //             // 处理接收到的数据
    //             std::cout << "Received data: " << std::string(recv_buffer, recv_buffer + BUFFER_SIZE) << std::endl;
    //         }
    //     }

    //     if (events & EPOLLOUT) {
    //         // 处理可写事件
    //         std::cout << "Socket is writable" << std::endl;
    //         // 这里可以调用 send_data 方法发送数据
    //         if (send_data(send_buffer, BUFFER_SIZE)) {
    //             std::cout << "Sent data successfully" << std::endl;
    //         }
    //     }

    //     if (events & (EPOLLERR | EPOLLHUP)) {
    //         // 处理错误或挂起事件
    //         std::cerr << "Socket error or hang up" << std::endl;
    //         // 这里可以进行清理工作，例如关闭 socket
    //         ::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, nullptr);
    //         ::close(sock_fd);
    //     }
    // }

    // 等待事件触发
    void wait_for_events()
    {
        struct epoll_event events[10];                   // 最多处理10个事件
        int nfds = epoll_wait(epoll_fd, events, 10, -1); // 阻塞等待事件
        if (nfds == -1)
        {
            throw std::runtime_error("epoll_wait failed");
        }
        for (int i = 0; i < nfds; ++i)
        {
            int fd = events[i].data.fd;
            Peer *peer = get_peer(fd);
            if (peer)
            {
                peer->handle_events(events[i].events); // 调用 Peer 的事件处理方法
            }
            else
            {
                LOG_ERROR("No associated Peer found for fd: {}", fd);
            }
        }
    }
};

#endif