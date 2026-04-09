#pragma once

#include <boost/asio.hpp>

#include <memory>
#include <thread>

namespace bto::util {

// Runs boost::asio::io_context on a worker thread with executor_work_guard so run() does not exit
// on an empty queue before async work is posted (cross-thread race with the caller).
class IoContextThread {
public:
    IoContextThread();
    ~IoContextThread();

    IoContextThread(const IoContextThread&) = delete;
    IoContextThread& operator=(const IoContextThread&) = delete;
    IoContextThread(IoContextThread&&) = delete;
    IoContextThread& operator=(IoContextThread&&) = delete;

    boost::asio::io_context& context() { return io_context_; }

    // Releases work guard, stops io_context, and joins the worker thread (safe to call repeatedly).
    void shutdown();

private:
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
        work_guard_;
    std::thread thread_;
    bool shutdown_{false};
};

}  // namespace bto::util
