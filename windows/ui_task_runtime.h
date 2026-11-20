#ifndef LNPLAYER_UI_TASK_RUNTIME_H
#define LNPLAYER_UI_TASK_RUNTIME_H

#include <asio.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

class UiTaskTracker final
{
public:
    bool try_begin();
    bool accepting() const;
    void finish();
    void close_and_wait();

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t pending_ = 0;
    std::atomic<bool> accepting_{true};
};

class UiTaskRuntime final
{
public:
    static UiTaskRuntime& instance();

    asio::io_context& io_context();
    asio::thread_pool::executor_type thread_pool_executor();

private:
    UiTaskRuntime();
    ~UiTaskRuntime();

    UiTaskRuntime(const UiTaskRuntime&) = delete;
    UiTaskRuntime& operator=(const UiTaskRuntime&) = delete;

    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    asio::thread_pool thread_pool_;
    std::thread io_thread_;
};

#endif
