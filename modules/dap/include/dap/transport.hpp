#ifndef TRAILER_DAP_TRANSPORT_HPP_
#define TRAILER_DAP_TRANSPORT_HPP_

#include <cstddef>
#include <optional>
#include <string>
#ifdef _WIN32
#include <memory>
#include <thread>
#endif

#include "llvm/Support/JSON.h"

namespace dap {

// DAP wire framing
class Transport {
public:
    Transport(int in_fd, int out_fd);
    ~Transport();

    // Owns an OS handle used to interrupt a blocking read from another
    // thread - copying would double-close it, so only moves are allowed.
    Transport(const Transport &) = delete;
    Transport &operator=(const Transport &) = delete;
    Transport(Transport &&other) noexcept;
    Transport &operator=(Transport &&other) noexcept;

    std::optional<llvm::json::Value> ReadMessage();

    bool WriteMessage(const llvm::json::Value &message);

    // Interrupts a blocking ReadMessage() call on another thread - the next
    // (or currently in-progress) call returns std::nullopt as if the peer
    // had closed the connection. Safe to call more than once and safe to
    // call before any read is in progress.
    void RequestStop();

private:
    bool ReadHeaderLine(std::string &line);

    // Blocks until in_fd_ has data or RequestStop() is called. Returns
    // false in the latter case (or on a read error) - the caller should
    // give up the read immediately when this returns false.
    bool WaitForReadable();

    // Consumes up to count bytes: >0 bytes read, 0 at end of input, <0 on
    // error or after RequestStop().
    std::ptrdiff_t ReadBytes(char *dst, size_t count);

#ifdef _WIN32
    // Unblocks the reader and returns once it has left its read. Windows
    // refuses to close a handle with a read pending on it, so this has to
    // complete before anything closes in_fd_.
    void CancelReader();

    // CancelReader() plus joining and releasing the thread.
    void StopReader();
#endif

    int in_fd_;
    int out_fd_;
    std::string read_buffer_;
    size_t read_pos_ = 0;

#ifdef _WIN32
    // Windows cannot wait on a pipe or console for readiness, so a thread
    // does blocking reads and this is the buffer it hands them over through.
    struct ReaderState;
    std::shared_ptr<ReaderState> reader_;
    std::thread reader_thread_;
#else
    int stop_pipe_read_ = -1;
    int stop_pipe_write_ = -1;
#endif
};

}  // namespace dap

#endif  // TRAILER_DAP_TRANSPORT_HPP_
