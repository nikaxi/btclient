#include "peer_connection.h"
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include "peer.h"

void PeerConnection::start_thread_pool()
{
    // 初始化工作线程
    for (int i = 0; i < num_worker_threads; ++i)
    {
        worker_threads.emplace_back(&PeerConnection::thread_worker, this);
    }
}

void PeerConnection::run()
{
    start_thread_pool();
    for (auto &peer : peers)
    {
        peer->connect();
    }
    while (true)
    {
        wait_for_events();
    }
}

void PeerConnection::thread_worker()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(task_mutex);
        task_cv.wait(lock, [this]
                     {  LOG_INFO("[{}] task_cv.wait() returned, task_queue.size() = {}", thread_id_str(std::this_thread::get_id()), task_queue.size());
                        return !task_queue.empty(); 
                    });

        if (!task_queue.empty())
        {
            auto [fd, peer] = task_queue.front();
            task_queue.pop();
            peer->handle_events(fd);
        }
        lock.unlock();
    }
}


void PeerConnection::stop_thread_pool() {
    for (auto &thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads.clear();
}

PeerConnection::~PeerConnection() {
    stop_thread_pool();
}