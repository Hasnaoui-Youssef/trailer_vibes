#ifndef TRAILER_DAP_TRANSPORT_HPP_
#define TRAILER_DAP_TRANSPORT_HPP_

#include <optional>
#include <string>

#include "llvm/Support/JSON.h"

namespace dap {

// DAP wire framing
class Transport {
public:
    Transport(int in_fd, int out_fd);
    ~Transport();

    // Owns a self-pipe used to interrupt a blocking read from another
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
    // false in the latter case (or on a poll error) - the caller should
    // give up the read immediately when this returns false.
    bool WaitForReadable();

    int in_fd_;
    int out_fd_;
    std::string read_buffer_;
    size_t read_pos_ = 0;

    int stop_pipe_read_ = -1;
    int stop_pipe_write_ = -1;
};

}  // namespace dap

#endif  // TRAILER_DAP_TRANSPORT_HPP_
