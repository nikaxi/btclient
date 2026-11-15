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
#include <queue>


class PeerConnection
{
private:
    int epoll_fd;
    std::mutex mtx;                      // 用于保护 epoll_fd 的互斥锁
    std::vector<std::shared_ptr<Peer>> peers;              // 用于存储 Peer对象
    std::unordered_map<int, std::shared_ptr<Peer>> fds; // 用于存储 socket_fd 与 Peer 对象的映射
    std::queue<std::pair<int, std::shared_ptr<Peer>>> task_queue; // 用于存储 socket_fd 与 Peer 对象的映射
    std::condition_variable task_cv;
    std::mutex task_mutex;
    std::vector<std::thread> worker_threads;
    void start_thread_pool();
    void thread_worker();

    void stop_thread_pool();
    const int num_worker_threads = 4;

public:
    explicit PeerConnection(int epoll_fd) : epoll_fd(epoll_fd) {}
    ~PeerConnection();

    void run();

    void add_peers(std::vector<std::shared_ptr<Peer>> &&peers_)
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& peer : peers_)
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
        } else {
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
        } else {
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



    void wait_for_events()
    {
        struct epoll_event events[50];                   // 最多处理50个事件
        int nfds = epoll_wait(epoll_fd, events, 50, -1); // 阻塞等待事件
        LOG_INFO("[{}] epoll_wait returned {} events", thread_id_str(std::this_thread::get_id()), nfds);
        if (nfds == -1)
        {
            throw std::runtime_error("epoll_wait failed");
        }
        for (int i = 0; i < nfds; ++i)
        {
            int fd = events[i].data.fd;
            uint32_t event = events[i].events;
            LOG_INFO("[{}] Handling event for fd: {} with events: {}", thread_id_str(std::this_thread::get_id()), fd,  event);
            std::shared_ptr<Peer> peer = get_peer(fd);
            if (peer)
            {
                if (event & EPOLLOUT) {
                    peer->handshake(); // 发送握手消息
                    if (peer->handshake_state == 0) {
                        peer->handshake_state = 1; // 更新握手状态
                    }
                    modify(fd, EPOLLIN | EPOLLET); // 修改为只监听可读事件
                } 
                if (event & EPOLLIN) {
                    // 这里优化成推送到任务队列就可以了
                    task_queue.push(std::make_pair(fd, peer));
                    task_cv.notify_one();
                }
                if (event & (EPOLLERR | EPOLLHUP)) {
                    LOG_WARN("EPOLLERR or EPOLLHUP for fd: {}", fd);
                    del_fd(fd);
                    peer->close_conn(fd);
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