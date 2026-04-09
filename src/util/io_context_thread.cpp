#include "util/io_context_thread.hpp"

namespace bto::util {

// Starts the io_context worker thread; work_guard prevents run() from returning on an empty queue.
IoContextThread::IoContextThread()
    : work_guard_(std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
          boost::asio::make_work_guard(io_context_)))
    , thread_([this]() { io_context_.run(); }) {}

IoContextThread::~IoContextThread() {
    shutdown();
}

void IoContextThread::shutdown() {
    if (shutdown_) {
        return;
    }
    shutdown_ = true;
    work_guard_.reset();
    io_context_.stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace bto::util
