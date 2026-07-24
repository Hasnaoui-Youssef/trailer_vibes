#ifndef TRAILER_TRACE_MODEL_SOURCE_LOCATION_HPP_
#define TRAILER_TRACE_MODEL_SOURCE_LOCATION_HPP_

#include <cstdint>
#include <string>
#include <vector>

namespace model {

// One frame of a possibly-inlined call chain: the function, file, and
// line/column an instruction's address maps to at one level of inlining.
struct InlineFrame {
    std::string function;
    std::string file;
    uint32_t line;
    uint32_t column;

    bool operator==(const InlineFrame &other) const {
        return line == other.line && column == other.column && function == other.function && file == other.file;
    }
    bool operator!=(const InlineFrame &other) const { return !(*this == other); }
};

// The full inline chain an instruction's address resolves to, innermost
// (the line actually executing) first, outermost (the real, non-inlined
// enclosing function) last. A single, non-inlined frame is the common
// case: `frames.size() == 1`. An empty `frames` means no debug info
// covered this address.
struct SourceLocation {
    std::vector<InlineFrame> frames;

    bool operator==(const SourceLocation &other) const { return frames == other.frames; }
    bool operator!=(const SourceLocation &other) const { return !(*this == other); }
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_SOURCE_LOCATION_HPP_
