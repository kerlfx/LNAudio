#include "runtime.h"

#include <exception>
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

    // Worker tasks may post their completion handlers back to io_context.
    // Keep the work guard alive until every worker has finished posting.
    thread_pool_.join();

    // With no more producers, release the guard and let io_context drain
    // all completion handlers before joining its event thread.
    work_guard_.reset();
    if (io_thread_.joinable())
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

bool Runtime::running_in_io_thread() const
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
