#include "dap/transport.hpp"

#ifdef _WIN32
// NOMINMAX keeps windows.h from defining min/max as macros over std::min.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#else
#include <poll.h>
#endif
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <iostream>
#include <string_view>
#include <utility>

#include "llvm/Support/raw_ostream.h"

namespace dap {

namespace {
constexpr size_t kReadChunkSize = 4096;
constexpr std::string_view kContentLengthHeader = "Content-Length:";
}  // namespace

#ifdef _WIN32

namespace {
constexpr auto kReaderStopPollInterval = std::chrono::milliseconds(5);
}  // namespace

struct Transport::ReaderState {
    std::mutex mutex;
    std::condition_variable cv;
    std::string buffer;
    bool eof = false;
    bool stopped = false;
    bool finished = false;
    HANDLE thread_handle = nullptr;

    // Satisfied once there is something to report: bytes, end of input, or a
    // stop request.
    bool Ready() const { return !buffer.empty() || eof || stopped; }
};

Transport::Transport(int in_fd, int out_fd)
    : in_fd_(in_fd), out_fd_(out_fd), reader_(std::make_shared<ReaderState>()) {
    // ReadFile on the underlying handle, not _read: _read holds a CRT lock
    // for the whole blocking call, which deadlocks any close() of the same fd.
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(in_fd_));
    reader_thread_ = std::thread([state = reader_, handle]() {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                            &state->thread_handle, 0, FALSE, DUPLICATE_SAME_ACCESS);
        }
        char chunk[kReadChunkSize];
        for (;;) {
            DWORD n = 0;
            const BOOL ok = ReadFile(handle, chunk, static_cast<DWORD>(sizeof(chunk)), &n, nullptr);
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!ok || n == 0) {
                state->eof = true;
                state->finished = true;
                state->cv.notify_all();
                return;
            }
            state->buffer.append(chunk, n);
            if (state->stopped) {
                state->finished = true;
                state->cv.notify_all();
                return;
            }
            state->cv.notify_all();
        }
    });
}

void Transport::CancelReader() {
    if (!reader_) return;

    std::unique_lock<std::mutex> lock(reader_->mutex);
    reader_->stopped = true;
    reader_->cv.notify_all();
    // CancelSynchronousIo is a no-op if the read has not started yet, so it
    // is retried until the thread actually leaves the loop.
    while (!reader_->finished) {
        if (reader_->thread_handle != nullptr) CancelSynchronousIo(reader_->thread_handle);
        reader_->cv.wait_for(lock, kReaderStopPollInterval);
    }
}

void Transport::StopReader() {
    if (!reader_) return;
    CancelReader();
    if (reader_thread_.joinable()) reader_thread_.join();

    if (reader_->thread_handle != nullptr) {
        CloseHandle(reader_->thread_handle);
        reader_->thread_handle = nullptr;
    }
    reader_.reset();
}

Transport::~Transport() { StopReader(); }

Transport::Transport(Transport &&other) noexcept
    : in_fd_(other.in_fd_), out_fd_(other.out_fd_), read_buffer_(std::move(other.read_buffer_)),
      read_pos_(other.read_pos_), reader_(std::move(other.reader_)),
      reader_thread_(std::move(other.reader_thread_)) {}

Transport &Transport::operator=(Transport &&other) noexcept {
    if (this == &other) return *this;
    StopReader();

    in_fd_ = other.in_fd_;
    out_fd_ = other.out_fd_;
    read_buffer_ = std::move(other.read_buffer_);
    read_pos_ = other.read_pos_;
    reader_ = std::move(other.reader_);
    reader_thread_ = std::move(other.reader_thread_);
    return *this;
}

void Transport::RequestStop() { CancelReader(); }

bool Transport::WaitForReadable() {
    if (!reader_) return true;  // moved-from: no interrupt support, fall back to blocking read

    std::unique_lock<std::mutex> lock(reader_->mutex);
    reader_->cv.wait(lock, [this] { return reader_->Ready(); });
    return !reader_->stopped && !reader_->buffer.empty();
}

std::ptrdiff_t Transport::ReadBytes(char *dst, size_t count) {
    if (!reader_) return _read(in_fd_, dst, static_cast<unsigned int>(count));

    std::unique_lock<std::mutex> lock(reader_->mutex);
    reader_->cv.wait(lock, [this] { return reader_->Ready(); });
    if (reader_->stopped) return -1;
    if (reader_->buffer.empty()) return reader_->eof ? 0 : -1;

    const size_t taken = std::min(count, reader_->buffer.size());
    std::memcpy(dst, reader_->buffer.data(), taken);
    reader_->buffer.erase(0, taken);
    return static_cast<std::ptrdiff_t>(taken);
}

#else

Transport::Transport(int in_fd, int out_fd) : in_fd_(in_fd), out_fd_(out_fd) {
    int fds[2];
    if (pipe(fds) == 0) {
        stop_pipe_read_ = fds[0];
        stop_pipe_write_ = fds[1];
    }
}

Transport::~Transport() {
    if (stop_pipe_read_ >= 0) close(stop_pipe_read_);
    if (stop_pipe_write_ >= 0) close(stop_pipe_write_);
}

Transport::Transport(Transport &&other) noexcept
    : in_fd_(other.in_fd_), out_fd_(other.out_fd_), read_buffer_(std::move(other.read_buffer_)),
      read_pos_(other.read_pos_), stop_pipe_read_(other.stop_pipe_read_),
      stop_pipe_write_(other.stop_pipe_write_) {
    other.stop_pipe_read_ = -1;
    other.stop_pipe_write_ = -1;
}

Transport &Transport::operator=(Transport &&other) noexcept {
    if (this == &other) return *this;
    if (stop_pipe_read_ >= 0) close(stop_pipe_read_);
    if (stop_pipe_write_ >= 0) close(stop_pipe_write_);

    in_fd_ = other.in_fd_;
    out_fd_ = other.out_fd_;
    read_buffer_ = std::move(other.read_buffer_);
    read_pos_ = other.read_pos_;
    stop_pipe_read_ = other.stop_pipe_read_;
    stop_pipe_write_ = other.stop_pipe_write_;
    other.stop_pipe_read_ = -1;
    other.stop_pipe_write_ = -1;
    return *this;
}

void Transport::RequestStop() {
    if (stop_pipe_write_ < 0) return;
    const char byte = 0;
    (void)write(stop_pipe_write_, &byte, 1);
}

bool Transport::WaitForReadable() {
    if (stop_pipe_read_ < 0) return true;  // moved-from: no interrupt support, fall back to blocking read

    pollfd fds[2] = {
        {in_fd_, POLLIN, 0},
        {stop_pipe_read_, POLLIN, 0},
    };
    for (;;) {
        const int ready = poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        break;
    }
    return !(fds[1].revents & POLLIN);
}

std::ptrdiff_t Transport::ReadBytes(char *dst, size_t count) {
    return read(in_fd_, dst, count);
}

#endif

bool Transport::ReadHeaderLine(std::string &line) {
    for (;;) {
        const size_t crlf_pos = read_buffer_.find("\r\n", read_pos_);
        if (crlf_pos != std::string::npos) {
            line.assign(read_buffer_, read_pos_, crlf_pos - read_pos_);
            read_pos_ = crlf_pos + 2;
            return true;
        }

        read_buffer_.erase(0, read_pos_);
        read_pos_ = 0;

        if (!WaitForReadable()) return false;

        char chunk[kReadChunkSize];
        const std::ptrdiff_t n = ReadBytes(chunk, sizeof(chunk));
        if (n <= 0) {
            return false;
        }
        read_buffer_.append(chunk, static_cast<size_t>(n));
    }
}

std::optional<llvm::json::Value> Transport::ReadMessage() {
    std::optional<size_t> content_length;
    std::string line;
    while (ReadHeaderLine(line)) {
        if (line.empty()) {
            break;
        }
        if (auto value = std::string_view{line};
            value.starts_with(kContentLengthHeader)) {
            value.remove_prefix(kContentLengthHeader.size());
            while (!value.empty() && value.front() == ' ') {
                value.remove_prefix(1);
            }
            size_t parsed = 0;
            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (ec == std::errc{} && ptr == value.data() + value.size()) {
                content_length = parsed;
            }
        }
    }
    if (!content_length) {
        return std::nullopt;
    }

    // drain what's buffered past the header block before pulling more from the descriptor
    std::string body;
    body.reserve(*content_length);
    const size_t buffered = read_buffer_.size() - read_pos_;
    const size_t take_from_buffer = std::min(buffered, *content_length);
    body.append(read_buffer_, read_pos_, take_from_buffer);
    read_pos_ += take_from_buffer;

    size_t remaining = *content_length - take_from_buffer;
    while (remaining > 0) {
        if (!WaitForReadable()) return std::nullopt;

        char chunk[kReadChunkSize];
        const std::ptrdiff_t n = ReadBytes(chunk, std::min(remaining, sizeof(chunk)));
        if (n <= 0) {
            return std::nullopt;
        }
        body.append(chunk, static_cast<size_t>(n));
        remaining -= static_cast<size_t>(n);
    }

    llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(body);
    if (!parsed) {
        std::cerr << "dap: malformed JSON body: " << llvm::toString(parsed.takeError()) << "\n";
        return std::nullopt;
    }
    return *parsed;
}

bool Transport::WriteMessage(const llvm::json::Value &message) {
    std::string body;
    llvm::raw_string_ostream(body) << message;

    const std::string framed = std::format("{} {}\r\n\r\n{}",kContentLengthHeader ,body.size(), body);

    size_t written = 0;
    while (written < framed.size()) {
        const std::ptrdiff_t n = write(out_fd_, framed.data() + written, framed.size() - written);
        if (n <= 0) {
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace dap
