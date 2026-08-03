#include "command_queue.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>

#include "openocd_jmp.h"

namespace providers {

namespace {

// OpenOCD stores descriptors as int (connection::fd), which Winsock permits -
// socket handles are documented to fit in 32 bits. Only the spelling of the
// invalid value and of close differs, so both are wrapped here.
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
int CloseSocket(int fd) { return closesocket(static_cast<socket_t>(fd)); }
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
int CloseSocket(int fd) { return close(fd); }
#endif

// remove_services() (server.c) unconditionally free()s each service's priv
// pointer, so add_service() can't be handed a plain CommandQueue* (not a
// malloc'd block, and one instance per process anyway) - it's tracked here
// instead, and add_service() gets priv=nullptr.
CommandQueue* g_active_queue = nullptr;

struct AddServiceArgs {
    const char* port;
    int result;
};

struct ServerLoopArgs {
    command_context* cmd_ctx;
};

void ServerLoopTrampoline(void* raw) {
    auto* args = static_cast<ServerLoopArgs*>(raw);
    server_loop(args->cmd_ctx);
}

}  // namespace

void CommandQueue::AddServiceTrampoline(void* raw) {
    auto* args = static_cast<AddServiceArgs*>(raw);
    static const struct service_driver driver = {
        "openocd_provider_wakeup", nullptr, &NewConnection, &Input, &ConnectionClosed, nullptr,
    };
    args->result = add_service(&driver, args->port, 1, nullptr);
}

int CommandQueue::NewConnection(connection* /*conn*/) { return ERROR_OK; }

int CommandQueue::Input(connection* conn) {
    char buf[64];
    recv(static_cast<socket_t>(conn->fd), buf, static_cast<int>(sizeof(buf)), 0);
    if (g_active_queue) g_active_queue->DrainAndRun();
    return ERROR_OK;
}

int CommandQueue::ConnectionClosed(connection* /*conn*/) { return ERROR_OK; }

void CommandQueue::DrainAndRun() {
    std::deque<std::function<void()>> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::swap(ready, tasks_);
    }
    for (auto& task : ready) task();
}

void CommandQueue::Wake() {
    if (wakeup_client_fd_ < 0) return;
    char byte = 0;
    send(static_cast<socket_t>(wakeup_client_fd_), &byte, 1, 0);
}

std::expected<void, std::string> CommandQueue::Start(command_context* cmd_ctx, const std::string& wakeup_port) {
    cmd_ctx_ = cmd_ctx;
    g_active_queue = this;

    AddServiceArgs add_args{wakeup_port.c_str(), ERROR_FAIL};
    int exit_code = 0;
    if (!openocd_call_guarded(&AddServiceTrampoline, &add_args, &exit_code)) {
        return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") while registering the wakeup service");
    }
    if (add_args.result != ERROR_OK) {
        return std::unexpected("add_service(" + wakeup_port + ") failed");
    }

    const socket_t client = socket(AF_INET, SOCK_STREAM, 0);
    if (client == kInvalidSocket) return std::unexpected("socket() failed for the wakeup client");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(std::stoi(wakeup_port)));

    const int client_fd = static_cast<int>(client);
    if (connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        CloseSocket(client_fd);
        return std::unexpected("connect() to the wakeup service failed");
    }
    wakeup_client_fd_ = client_fd;

    worker_ = std::thread([this, cmd_ctx]() {
        ServerLoopArgs loop_args{cmd_ctx};
        int loop_exit_code = 0;
        openocd_call_guarded(&ServerLoopTrampoline, &loop_args, &loop_exit_code);
    });
    started_ = true;

    return {};
}

void CommandQueue::Join() {
    if (worker_.joinable()) worker_.join();
}

CommandQueue::~CommandQueue() {
    Join();
    if (wakeup_client_fd_ >= 0) CloseSocket(wakeup_client_fd_);
    if (g_active_queue == this) g_active_queue = nullptr;
}

}  // namespace providers
