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

    std::optional<llvm::json::Value> ReadMessage();

    bool WriteMessage(const llvm::json::Value &message);

private:
    bool ReadHeaderLine(std::string &line);

    int in_fd_;
    int out_fd_;
    std::string read_buffer_;
    size_t read_pos_ = 0;
};

}  // namespace dap

#endif  // TRAILER_DAP_TRANSPORT_HPP_
