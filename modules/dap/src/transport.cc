#include "dap/transport.hpp"

#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <iostream>
#include <string_view>

#include "llvm/Support/raw_ostream.h"

namespace dap {

namespace {
constexpr size_t kReadChunkSize = 4096;
constexpr std::string_view kContentLengthHeader = "Content-Length:";
}  // namespace

Transport::Transport(int in_fd, int out_fd) : in_fd_(in_fd), out_fd_(out_fd) {}

bool Transport::ReadHeaderLine(std::string &line) {
    for (;;) {
        const size_t newline = read_buffer_.find('\n', read_pos_);
        if (newline != std::string::npos) {
            line.assign(read_buffer_, read_pos_, newline - read_pos_);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            read_pos_ = newline + 1;
            return true;
        }

        // No newline buffered yet: drop the already-consumed prefix so the
        // buffer doesn't grow unboundedly across many small reads, then
        // pull more bytes in.
        read_buffer_.erase(0, read_pos_);
        read_pos_ = 0;

        char chunk[kReadChunkSize];
        const ssize_t n = read(in_fd_, chunk, sizeof(chunk));
        if (n <= 0) {
            return false;  // EOF or error.
        }
        read_buffer_.append(chunk, static_cast<size_t>(n));
    }
}

std::optional<llvm::json::Value> Transport::ReadMessage() {
    // Consume header lines through the blank line that ends them, tracking
    // Content-Length along the way. It's the only header this adapter emits
    // or needs to honor; any other header line is ignored per the DAP/LSP
    // framing spec's "unknown headers are ignored" guidance.
    std::optional<size_t> content_length;
    std::string line;
    while (ReadHeaderLine(line)) {
        if (line.empty()) {
            break;  // Blank line: end of headers.
        }
        if (line.size() > kContentLengthHeader.size() &&
            std::string_view(line).substr(0, kContentLengthHeader.size()) == kContentLengthHeader) {
            std::string_view value(line);
            value.remove_prefix(kContentLengthHeader.size());
            while (!value.empty() && value.front() == ' ') {
                value.remove_prefix(1);
            }
            size_t parsed = 0;
            const std::from_chars_result result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec == std::errc()) {
                content_length = parsed;
            }
        }
    }
    if (!content_length) {
        return std::nullopt;  // EOF before a full header block, or no Content-Length seen.
    }

    // Read exactly *content_length body bytes, first draining whatever's
    // already buffered past the header block before pulling more from the
    // descriptor.
    std::string body;
    body.reserve(*content_length);
    const size_t buffered = read_buffer_.size() - read_pos_;
    const size_t take_from_buffer = std::min(buffered, *content_length);
    body.append(read_buffer_, read_pos_, take_from_buffer);
    read_pos_ += take_from_buffer;

    size_t remaining = *content_length - take_from_buffer;
    while (remaining > 0) {
        char chunk[kReadChunkSize];
        const ssize_t n = read(in_fd_, chunk, std::min(remaining, sizeof(chunk)));
        if (n <= 0) {
            return std::nullopt;  // EOF mid-body.
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

    const std::string framed = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;

    size_t written = 0;
    while (written < framed.size()) {
        const ssize_t n = write(out_fd_, framed.data() + written, framed.size() - written);
        if (n <= 0) {
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace dap
