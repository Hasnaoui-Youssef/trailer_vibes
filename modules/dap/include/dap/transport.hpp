#ifndef TRAILER_DAP_TRANSPORT_HPP_
#define TRAILER_DAP_TRANSPORT_HPP_

#include <optional>
#include <string>

#include "llvm/Support/JSON.h"

namespace dap {

// Debug Adapter Protocol wire framing: a "Content-Length: <N>\r\n\r\n"
// header followed by exactly N bytes of UTF-8 JSON (the same framing LSP
// uses). Reimplemented independently of lldb/Host/JSONTransport.h (an
// lldbHost internal this module deliberately does not link, to keep the
// wire layer decoupled from LLDB) rather than vendored - the framing is a
// small, self-contained protocol detail with no SB API surface.
class Transport {
public:
    // Reads framed messages from `in_fd`, writes them to `out_fd`. Does not
    // take ownership of either descriptor.
    Transport(int in_fd, int out_fd);

    // Blocks until one full framed message is read. Returns nullopt on EOF
    // (the client closed its end) or a malformed frame/JSON body - either
    // way there is nothing more to usefully read, so the caller should stop
    // its read loop. The two cases aren't distinguished in the return value;
    // a malformed frame is logged to stderr before returning nullopt.
    std::optional<llvm::json::Value> ReadMessage();

    // Serializes `message` and writes it as one framed message. Returns
    // false if the write failed (e.g. the client closed its end).
    bool WriteMessage(const llvm::json::Value &message);

private:
    // Fills `line` with the next header line (without the trailing
    // "\r\n"/"\n"), consuming it from the input stream. Returns false on
    // EOF before a newline was seen.
    bool ReadHeaderLine(std::string &line);

    int in_fd_;
    int out_fd_;
    std::string read_buffer_;
    size_t read_pos_ = 0;
};

}  // namespace dap

#endif  // TRAILER_DAP_TRANSPORT_HPP_
