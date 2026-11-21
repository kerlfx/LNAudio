#include "runtime.h"

#include <exception>
#include <future>
#include <string>

namespace lnplayer
{

Runtime::Runtime()
    : work_guard_(asio::make_work_guard(io_context_))
    , thread_pool_(2)
{
    io_thread_ = std::thread([this]
    {
        io_context_.run();
    });
}

Runtime::~Runtime()
{
    shutdown();
}

void Runtime::shutdown()
{
    if (!io_thread_.joinable())
        return;

    // Drain handlers already queued on io_context before joining the pool.
    // Those handlers may be the producers of new worker tasks.
    bool io_barrier_completed = false;
    try
    {
        std::promise<void> barrier;
        std::future<void> future = barrier.get_future();
        asio::post(io_context_, [&barrier]
        {
            barrier.set_value();
        });
        future.wait();
        io_barrier_completed = true;
    }
    catch (...)
    {
    }

    if (!io_barrier_completed)
    {
        // If the barrier cannot be queued, stop the only worker that can
        // submit additional pool work before joining the pool.
        io_context_.stop();
        io_thread_.join();
        thread_pool_.join();
        work_guard_.reset();
        return;
    }

    // No queued io handler can submit new work now. Let all workers finish
    // posting their completions, then drain those completions on io_context.
    thread_pool_.join();
    work_guard_.reset();
    io_thread_.join();
}

asio::io_context& Runtime::io_context()
{
    return io_context_;
}

asio::thread_pool& Runtime::thread_pool()
{
    return thread_pool_;
}

bool Runtime::running_in_io_thread()
{
    return io_context_.get_executor().running_in_this_thread();
}

bool Runtime::open_music_library(std::string& error)
{
    std::lock_guard<std::mutex> lock(music_library_mutex_);
    if (music_library_open_)
        return true;

    try
    {
        if (!music_library_.open_default(error))
            return false;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }

    music_library_open_ = true;
    return true;
}

MusicLibrary& Runtime::music_library()
{
    return music_library_;
}

}
