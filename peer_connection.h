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

        if (fd <= 0 || fd == epoll_fd)
        {
            LOG_ERROR("Invalid fd: {}", fd);
            return false;
        }

        // 已经注册
        if (fds.find(fd) != fds.end())
        {
            LOG_ERROR("fd {} already exists in epoll", fd);
            return false;
        }

        struct epoll_event ev{};
        ev.events = events; // 边缘触发模式
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

    bool modify(int fd, uint32_t events)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (fds.find(fd) == fds.end())
        {
            LOG_ERROR("fd {} not found in epoll", fd);
            return false;
        }

        struct epoll_event ev{};
        ev.events = events; // 边缘触发模式
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0)
        {
            LOG_ERROR("epoll_ctl modify failed: {} fd: {} epoll_fd: {}", strerror(errno), fd, epoll_fd);
            return false;
        }
        return true;
    }

    Peer *get_peer(int fd)
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = fds.find(fd);
        if (it != fds.end())
        {
            return it->second;
        } else {
            LOG_WARN("fd {} not found in fds", fd);
            fds.erase(fd); // 移除不存在的 fd

        }
        return nullptr;
    }

    int get_epoll_fd() const
    {
        return epoll_fd;
    }

    bool del(int fd)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (fds.find(fd) == fds.end())
        {
            LOG_ERROR("fd {} not found in epoll", fd);
            return false;
        }
        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) != 0)
        {
            LOG_ERROR("epoll_ctl delete failed: {} fd: {} epoll_fd: {}", strerror(errno), fd, epoll_fd);
            return false;
        }
        fds.erase(fd); // 从映射中移除
        return true;
    }

    void wait_for_events()
    {
        struct epoll_event events[50];                   // 最多处理50个事件
        int nfds = epoll_wait(epoll_fd, events, 50, -1); // 阻塞等待事件
        LOG_INFO("epoll_wait returned {} events", nfds);
        if (nfds == -1)
        {
            throw std::runtime_error("epoll_wait failed");
        }
        for (int i = 0; i < nfds; ++i)
        {
            int fd = events[i].data.fd;
            uint32_t event = events[i].events;
            LOG_INFO("Handling event for fd: {} with events: {}", fd,  event);
            Peer *peer = get_peer(fd);
            if (peer)
            {
                if (event & EPOLLOUT) {
                    peer->handshake(); // 发送握手消息
                    modify(fd, EPOLLIN | EPOLLET); // 修改为只监听可读事件
                } 
                if (event & EPOLLIN) {
                    peer->handle_events(fd); // 调用 Peer 的事件处理方法
                }
                if (event & (EPOLLERR | EPOLLHUP)) {
                    LOG_WARN("EPOLLERR or EPOLLHUP for fd: {}", fd);
                    peer->close_conn(fd); // 关闭连接
                }
            }
            else
            {
                LOG_ERROR("No associated Peer found for fd: {}", fd);
            }
        }
    }
};

#endif