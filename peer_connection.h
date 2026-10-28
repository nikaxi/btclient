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
#include "safequeue.h"

class PeerConnection
{
private:
    int epoll_fd;
    std::mutex mtx;                                     // 用于保护 epoll_fd 的互斥锁
    std::vector<std::shared_ptr<Peer>> peers;           // 用于存储 Peer对象
    std::unordered_map<int, std::shared_ptr<Peer>> fds; // 用于存储 socket_fd 与 Peer 对象的映射
    ThreadSafeQueue<std::shared_ptr<Peer>> peer_task_queue;

public:
    explicit PeerConnection(int epoll_fd) : epoll_fd(epoll_fd) {}

    void run()
    {
        std::thread([&]()
                    {
                        while (true)
                        {
                            int count = 0;
                            for (auto &peer : peers)
                            {
                                if (peer->socket_fd == -1)
                                    count += 1;
                            }
                            LOG_INFO("-1 fd nums is {}", count);
                            sleep(10);
                        } })
            .detach();
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
            peer->close_socket_fd();
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

    void handle_peer(std::shared_ptr<Peer> peer)
    {
        bool expected = false;
        if (peer->is_processing.compare_exchange_strong(expected, true))
        {
            peer_task_queue.push(peer);
        }
    }

    void worker_thread_func()
    {
        while (true)
        {
            std::shared_ptr<Peer> peer;
            if (!peer_task_queue.pop(peer))
            {
                std::this_thread::yield(); // 队列为空时让出 CPU 或微调休眠
                continue;
            }
            peer->handle_events();
            // 【极其重要】处理完毕后，释放忙碌标志，允许下一轮 epoll 事件再次调度它
            peer->is_processing = false;
        }
    }

    // 初始化时启动 N 个工作线程
    void start_worker_pool(int thread_count)
    {
        for (int i = 0; i < thread_count; ++i)
        {
            std::thread(&PeerConnection::worker_thread_func, this).detach();
        }
    }

    void wait_for_events()
    {
        while (true)
        {
            struct epoll_event events[100];                     // 最多处理50个事件
            int nfds = epoll_wait(epoll_fd, events, 100, 3000); // 阻塞等待事件
            if (nfds == 0)
            {
                LOG_INFO("epoll_wait returned {} events wait some(10) seconds", nfds);
                sleep(10);
                continue;
            }

            LOG_INFO("epoll_wait returned {} events", nfds);
            for (int i = 0; i < nfds; ++i)
            {
                int fd = events[i].data.fd;
                std::shared_ptr<Peer> peer = get_peer(fd);
                uint32_t event = events[i].events;
                if (event & EPOLLOUT)
                {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                    if (err == 0)
                    {
                        // 连接成功
                        peer->handshake();

                        // 【关键步骤】连接成功后，必须把监听事件从 EPOLLOUT 改为 EPOLLIN
                        epoll_event ev{};
                        ev.events = EPOLLIN | EPOLLET; // 以后主要靠读事件收 BT 报文
                        ev.data.fd = fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
                        LOG_INFO("{} 异步连接成功！", peer->to_string());
                    }
                    else
                    {
                        LOG_WARN("{} 异步连接失败！", peer->to_string());
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                        peer->close_socket_fd();
                    }
                }
                else if (event & EPOLLIN)
                {
                    handle_peer(peer);
                }
                else if (event & (EPOLLERR | EPOLLHUP))
                {
                    LOG_WARN("{} 异步事件ERROR！", peer->to_string());
                    peer->close_socket_fd();
                }
                else
                {
                    LOG_WARN("{} 异步未处理", peer->to_string());
                }
            }
        }
    }
};

#endif