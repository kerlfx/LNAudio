#ifndef LNPLAYER_RUNTIME_H
#define LNPLAYER_RUNTIME_H

#include <asio.hpp>

#include "music_library.h"

#include <thread>

namespace lnplayer
{

class Runtime
{
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    asio::io_context& io_context();
    asio::thread_pool& thread_pool();
    bool running_in_io_thread() const;
    bool open_music_library(std::string& error);
    MusicLibrary& music_library();
    void shutdown();

private:
    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    asio::thread_pool thread_pool_;
    MusicLibrary music_library_;
    std::mutex music_library_mutex_;
    bool music_library_open_ = false;
    std::thread io_thread_;
};

}

#endif
