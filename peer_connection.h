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
    std::mutex mtx;                                     // 用于保护 epoll_fd 的互斥锁
    std::vector<std::shared_ptr<Peer>> peers;           // 用于存储 Peer对象
    std::unordered_map<int, std::shared_ptr<Peer>> fds; // 用于存储 socket_fd 与 Peer 对象的映射
public:
    explicit PeerConnection(int epoll_fd) : epoll_fd(epoll_fd) {}

    void run()
    {
        for (auto &peer : peers)
        {
            peer->connect();
        }
        wait_for_events();
    }

    void add_peers(std::vector<std::shared_ptr<Peer>> &&peers_)
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto &peer : peers_)
        {
            peers.push_back(std::move(peer));
        }
    }

    bool add(int fd, uint32_t events, std::shared_ptr<Peer> peer)
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
        else
        {
            LOG_INFO("epoll_ctl add success: fd: {} epoll_fd: {}", fd, epoll_fd);
            fds[fd] = peer; // 将 socket_fd 与 Peer 对象关联
            return true;
        }
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

    std::shared_ptr<Peer> get_peer(int fd)
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = fds.find(fd);
        if (it != fds.end())
        {
            return it->second;
        }
        else
        {
            LOG_WARN("fd {} not found in fds", fd);
        }
        return nullptr;
    }

    int get_epoll_fd() const
    {
        return epoll_fd;
    }

    bool del_fd(int fd)
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
        return true;
    }

    void handle_peer(std::shared_ptr<Peer> peer, int event, int fd)
    {
        peer->handle_events(fd); // 调用 Peer 的事件处理方法
    }

    void wait_for_events()
    {
        while (true)
        {
            struct epoll_event events[50];                   // 最多处理50个事件
            int nfds = epoll_wait(epoll_fd, events, 50, -1); // 阻塞等待事件
            LOG_INFO("epoll_wait returned {} events", nfds);
            for (int i = 0; i < nfds; ++i)
            {
                int fd = events[i].data.fd;
                uint32_t event = events[i].events;
                if (event & EPOLLOUT)
                {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                    if (err == 0)
                    {
                        // 连接成功
                        std::cout << "Peer 异步连接成功！" << std::endl;

                        // 【关键步骤】连接成功后，必须把监听事件从 EPOLLOUT 改为 EPOLLIN
                        epoll_event ev{};
                        ev.events = EPOLLIN | EPOLLET; // 以后主要靠读事件收 BT 报文
                        ev.data.fd = fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
                    }
                    else
                    {
                        // 握手失败（如对方拒绝连接、超时等）
                        std::cerr << "Peer 连接失败，关闭 fd: " << fd << std::endl;
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                    }
                }
                else if (event & EPOLLIN) {
                    std::shared_ptr<Peer> peer = get_peer(fd);
                    handle_peer(peer, event, fd);
                }
            }
        }
    }
};

#endif