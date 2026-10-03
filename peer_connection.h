#ifndef PEER_CONNECTION_H
#define PEER_CONNECTION_H

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>
#include <sys/epoll.h>

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

const int BUFFER_SIZE = 4096; // 缓冲区大小

class PeerConnection {
private:
    int sock_fd;
    int epoll_fd;
    uint8_t send_buffer[BUFFER_SIZE];
    uint8_t recv_buffer[BUFFER_SIZE];
public:
    PeerConnection(int fd);
    ~PeerConnection();
    // 注册事件到 epoll
    void register_events(int epoll_fd) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET; // 边缘触发模式
        ev.data.fd = sock_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev) == -1) {
            throw std::runtime_error("Failed to register socket with epoll");
        }
    }
    // 当有数据发送数据
    bool send_data(const uint8_t *data, size_t len) {
        size_t total_sent = 0;
        while (total_sent < len) {
            ssize_t sent = ::send(sock_fd, reinterpret_cast<const char *>(data + total_sent), len - total_sent, 0);
            if (sent <= 0) {
                if (sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    continue; // 非阻塞模式下，继续尝试发送
                } else {
                    std::cerr << "Send failed or connection closed" << std::endl;
                    return false; // 连接断开或出错
                }
            }
            total_sent += sent;
        }
        return true;
    }
    // 当有数据接收数据
    bool recv_data(uint8_t *buffer, size_t len) {
        size_t total_received = 0;
        while (total_received < len) {
            ssize_t received = ::recv(sock_fd, reinterpret_cast<char *>(buffer + total_received), len - total_received, 0);
            if (received <= 0) {
                if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    continue; // 非阻塞模式下，继续尝试接收
                } else {
                    std::cerr << "Recv failed or connection closed" << std::endl;
                    return false; // 连接断开或出错
                }
            }
            total_received += received;
        }
        return true;
    }

    // 检测事件 处理读写请求
    void handle_events(uint32_t events) {
        // 从epoll中获取事件类型，进行相应的处理
        if (events & EPOLLIN) {
            // 处理可读事件
            std::cout << "Socket is readable" << std::endl;
            // 这里可以调用 recv_data 方法接收数据
            recv_data(recv_buffer, BUFFER_SIZE);
        } 

        if (events & EPOLLOUT) {
            // 处理可写事件
            std::cout << "Socket is writable" << std::endl;
            // 这里可以调用 send_data 方法发送数据
            send_data(send_buffer, BUFFER_SIZE);
        }

        if (events & (EPOLLERR | EPOLLHUP)) {
            // 处理错误或挂起事件
            std::cerr << "Socket error or hang up" << std::endl;
            // 这里可以进行清理工作，例如关闭 socket
            ::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, nullptr);
            ::close(sock_fd);
        }
    }

    // 等待事件触发
    void wait_for_events(int epoll_fd) {
        struct epoll_event events[10]; // 最多处理10个事件
        int nfds = epoll_wait(epoll_fd, events, 10, -1); // 阻塞等待事件
        if (nfds == -1) {
            throw std::runtime_error("epoll_wait failed");
        }
        for (int i = 0; i < nfds; ++i) {
            handle_events(events[i].events);
        }
    }
};


#endif