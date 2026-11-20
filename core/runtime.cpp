#include "runtime.h"

#include <algorithm>

namespace lnplayer
{

Runtime::Runtime()
    : work_guard_(asio::make_work_guard(io_context_))
    , thread_pool_(std::max(2u, std::thread::hardware_concurrency()))
    , io_thread_([this]
      {
          io_context_.run();
      })
{
}

Runtime::~Runtime()
{
    work_guard_.reset();
    io_context_.stop();
    if (io_thread_.joinable())
        io_thread_.join();
    thread_pool_.join();
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

}
