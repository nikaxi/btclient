#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> q;
    mutable std::mutex mtx; // mutable 允许在 const 函数中加锁（如果需要）
    std::condition_variable cv;

public:
    ThreadSafeQueue() = default;
    ~ThreadSafeQueue() = default;

    // 禁用拷贝构造和拷贝赋值（队列通常作为独占资源管理）
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // 1. 压入任务（支持左值和右值）
    void push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            q.push(item);
        }
        cv.notify_one(); // 唤醒一个正在等待的线程
    }

    void push(T&& item) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            q.push(std::move(item));
        }
        cv.notify_one(); // 唤醒一个正在等待的线程
    }

    // 2. 阻塞式弹出（推荐给工作线程：队列为空时线程会休眠，直到有任务）
    void wait_and_pop(T& item) {
        std::unique_lock<std::mutex> lock(mtx);
        // 使用 lambda 表达式防止虚假唤醒 (Spurious Wakeup)
        cv.wait(lock, [this]() { return !q.empty(); });
        
        item = std::move(q.front());
        q.pop();
    }

    // 3. 非阻塞式弹出（尝试弹出，成功返回 true，空则返回 false）
    bool pop(T& item) {
        std::lock_guard<std::mutex> lock(mtx);
        if (q.empty()) {
            return false;
        }
        item = std::move(q.front());
        q.pop();
        return true;
    }

    // 4. 检查队列是否为空
    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx);
        return q.empty();
    }

    // 5. 获取当前队列大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx);
        return q.size();
    }
};