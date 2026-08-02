#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_COMMAND_QUEUE_HPP_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_COMMAND_QUEUE_HPP_

#include <chrono>
#include <deque>
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>

#include "openocd_c_api.hpp"

namespace providers {

// server_loop() blocks in select() over sockets it owns (server.c); this is
// the only way to get work executed safely alongside it - a service_driver
// bound to a loopback port that server_loop already polls. Everything
// posted here runs on server_loop's thread, which is the single place
// OpenOCD's command context (and every global it touches) may be used from.
class CommandQueue {
 public:
    CommandQueue() = default;
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;
    ~CommandQueue();

    std::expected<void, std::string> Start(command_context* cmd_ctx, const std::string& wakeup_port);

    bool HasStarted() const { return started_; }

    // Blocks until server_loop() (started by Start()) returns.
    void Join();

    // Every server_loop turn a task submitted here waits behind is bounded by
    // this, so a caller blocked in RunSync() cannot wait forever.
    static constexpr std::chrono::milliseconds kDefaultTimeout{5000};

    template <typename Fn>
    auto Post(Fn&& fn) -> std::future<std::invoke_result_t<Fn>> {
        using R = std::invoke_result_t<Fn>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<Fn>(fn));
        std::future<R> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back([task]() { (*task)(); });
        }
        Wake();
        return future;
    }

    // Synchronous convenience: the shape every OpenOcdProvider public method
    // actually wants (see Phase 3 of the plan - futures at this layer,
    // synchronous wrappers above it).
    //
    // Bounded by timeout: if server_loop hasn't run the task in time, this
    // returns nullopt (or false for a void Fn) instead of blocking forever.
    // fn must not capture the caller's stack by reference - it may still be
    // running on server_loop's thread after this returns on timeout, so it
    // has to own everything it touches.
    template <typename Fn>
    auto RunSync(Fn&& fn, std::chrono::milliseconds timeout = kDefaultTimeout) {
        using R = std::invoke_result_t<Fn>;
        std::future<R> future = Post(std::forward<Fn>(fn));
        if constexpr (std::is_void_v<R>) {
            if (future.wait_for(timeout) != std::future_status::ready) return false;
            future.get();
            return true;
        } else {
            if (future.wait_for(timeout) != std::future_status::ready) return std::optional<R>(std::nullopt);
            return std::optional<R>(future.get());
        }
    }

 private:
    static int NewConnection(connection* conn);
    static int Input(connection* conn);
    static int ConnectionClosed(connection* conn);
    static void AddServiceTrampoline(void* raw);

    void DrainAndRun();
    void Wake();

    command_context* cmd_ctx_ = nullptr;
    std::thread worker_;
    std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
    int wakeup_client_fd_ = -1;
    bool started_ = false;
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_COMMAND_QUEUE_HPP_
