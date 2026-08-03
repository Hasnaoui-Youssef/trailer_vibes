// Deterministic test for the Phase 4 reader/dispatch split: a 'cancel'
// naming a request still sitting in the dispatch queue must be observable
// before that request's handler ever runs, even while dispatch is stuck on
// an earlier, slow request. No hardware, no LLDB - drives Orchestrator
// directly over a pair of real pipes standing in for stdin/stdout.

#include "dap/orchestrator.hpp"
#include "dap/request_handler.hpp"
#include "dap/transport.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/raw_ostream.h"

namespace {

int MakePipe(int fds[2]) {
#ifdef _WIN32
    return _pipe(fds, 4096, _O_BINARY);
#else
    return pipe(fds);
#endif
}

int Fail(const std::string &message) {
    std::cerr << "orchestrator_cancel_test: " << message << "\n";
    return EXIT_FAILURE;
}

std::string Frame(const llvm::json::Value &message) {
    std::string body;
    llvm::raw_string_ostream(body) << message;
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

bool WriteAll(int fd, const std::string &data) {
    size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = write(fd, data.data() + written, data.size() - written);
        if (n <= 0) return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

llvm::json::Value MakeRequest(int64_t seq, llvm::StringRef command,
                               std::optional<llvm::json::Value> arguments = std::nullopt) {
    llvm::json::Object obj{
        {"type", "request"},
        {"seq", seq},
        {"command", command},
    };
    if (arguments) obj["arguments"] = std::move(*arguments);
    return llvm::json::Value(std::move(obj));
}

// Blocks in Run() until Release() is called, then sends a plain success
// response - stands in for a slow, already-dispatched request so a request
// queued behind it can be cancelled before dispatch ever reaches it.
class SlowHandler : public dap::IRequestHandler {
public:
    explicit SlowHandler(dap::Orchestrator &orchestrator) : orchestrator_(orchestrator) {}

    void Run(const dap::protocol::Request &request) override {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return released_; });

        dap::protocol::Response response;
        response.command = request.command;
        response.request_seq = request.seq;
        response.success = true;
        orchestrator_.Send(response);
    }

    void Release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        cv_.notify_one();
    }

private:
    dap::Orchestrator &orchestrator_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool released_ = false;
};

// Mirrors BaseRequestHandler::Run()'s cancellation short-circuit (this test
// lives in dap_core, which doesn't depend on dap_handlers). Records whether
// its body ever actually ran, past the cancellation check.
class QuickHandler : public dap::IRequestHandler {
public:
    explicit QuickHandler(dap::Orchestrator &orchestrator) : orchestrator_(orchestrator) {}

    void Run(const dap::protocol::Request &request) override {
        if (orchestrator_.IsCancelled(request)) {
            dap::protocol::Response response;
            response.command = request.command;
            response.request_seq = request.seq;
            response.success = false;
            response.message = dap::protocol::eResponseMessageCancelled;
            orchestrator_.Send(response);
            return;
        }

        ran_ = true;
        dap::protocol::Response response;
        response.command = request.command;
        response.request_seq = request.seq;
        response.success = true;
        orchestrator_.Send(response);
    }

    bool ran() const { return ran_; }

private:
    dap::Orchestrator &orchestrator_;
    bool ran_ = false;
};

// Mirrors the real CancelRequestHandler (dap_handlers, not linked here):
// clears the mark once the target request's fate is sealed, and
// acknowledges the cancel request itself.
class CancelHandler : public dap::IRequestHandler {
public:
    explicit CancelHandler(dap::Orchestrator &orchestrator) : orchestrator_(orchestrator) {}

    void Run(const dap::protocol::Request &request) override {
        dap::protocol::CancelArguments args;
        if (request.arguments) {
            llvm::json::Path::Root root{"arguments"};
            fromJSON(*request.arguments, args, root);
        }
        orchestrator_.ClearCancelRequest(args);

        dap::protocol::Response response;
        response.command = request.command;
        response.request_seq = request.seq;
        response.success = true;
        orchestrator_.Send(response);
    }

private:
    dap::Orchestrator &orchestrator_;
};

}  // namespace

int main() {
    int stdin_pipe[2];
    int stdout_pipe[2];
    if (MakePipe(stdin_pipe) != 0 || MakePipe(stdout_pipe) != 0) return Fail("pipe() failed");

    dap::Transport transport(/*in_fd=*/stdin_pipe[0], /*out_fd=*/stdout_pipe[1]);
    dap::Orchestrator orchestrator(std::move(transport));

    SlowHandler slow(orchestrator);
    QuickHandler quick(orchestrator);
    CancelHandler cancel(orchestrator);
    orchestrator.RegisterHandler("slow", slow);
    orchestrator.RegisterHandler("quick", quick);
    orchestrator.RegisterHandler("cancel", cancel);

    std::thread run_thread([&]() { orchestrator.Run(); });
    const llvm::scope_exit stop_and_join([&]() {
        orchestrator.RequestStop();
        if (run_thread.joinable()) run_thread.join();
    });

    // Request A (slow, seq 1) dispatches immediately and blocks. Request B
    // (quick, seq 2) queues behind it. cancel(2) is read independently by
    // the reader thread while dispatch is still stuck in A - this is
    // exactly the scenario the split makes possible.
    if (!WriteAll(stdin_pipe[1], Frame(MakeRequest(1, "slow"))) ||
        !WriteAll(stdin_pipe[1], Frame(MakeRequest(2, "quick"))) ||
        !WriteAll(stdin_pipe[1],
                  Frame(MakeRequest(3, "cancel", llvm::json::Object{{"requestId", 2}})))) {
        return Fail("failed to write requests");
    }

    // Give the reader thread time to read and process all three messages -
    // it runs independently of the dispatch thread, which won't move past
    // request 1 until Release() below.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    slow.Release();

    dap::Transport response_reader(/*in_fd=*/stdout_pipe[0], /*out_fd=*/-1);
    std::optional<dap::protocol::Response> response_1, response_2, response_3;
    for (int i = 0; i < 3; ++i) {
        std::optional<llvm::json::Value> message = response_reader.ReadMessage();
        if (!message) return Fail("ReadMessage() failed while collecting responses");

        dap::protocol::Response response;
        llvm::json::Path::Root root{"message"};
        if (!fromJSON(*message, response, root)) return Fail("received a malformed response");

        if (response.request_seq == 1) response_1 = std::move(response);
        else if (response.request_seq == 2) response_2 = std::move(response);
        else if (response.request_seq == 3) response_3 = std::move(response);
        else return Fail("response for an unexpected request_seq");
    }

    if (!response_1 || !response_1->success) return Fail("request 1 (slow) did not succeed");
    if (!response_3 || !response_3->success) return Fail("request 3 (cancel) did not succeed");

    if (!response_2) return Fail("no response for request 2 (quick)");
    if (response_2->success) return Fail("request 2 (quick) succeeded - it should have been cancelled");
    if (!response_2->message || !std::holds_alternative<dap::protocol::ResponseMessage>(*response_2->message) ||
        std::get<dap::protocol::ResponseMessage>(*response_2->message) != dap::protocol::eResponseMessageCancelled) {
        return Fail("request 2 (quick) failed for a reason other than cancellation");
    }
    if (quick.ran()) return Fail("QuickHandler::Run() executed its body despite being cancelled");

    // Explicit here (not just via the scope_exit guard) so the reader
    // thread is stopped and joined before its fds are closed below - the
    // guard remains as a safety net for the early-return Fail() paths
    // above, none of which reach these closes anyway.
    orchestrator.RequestStop();
    if (run_thread.joinable()) run_thread.join();
    response_reader.RequestStop();

    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);

    std::cout << "orchestrator_cancel_test: ok (queued request cancelled before it ran)\n";
    return EXIT_SUCCESS;
}
