#ifndef TRAILER_TRACE_MODEL_FUNCTION_BLOCK_HPP_
#define TRAILER_TRACE_MODEL_FUNCTION_BLOCK_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "trace_model/line_block.hpp"

namespace model {

// A maximal, chronological run of LineBlocks whose outermost (real,
// non-inlined) enclosing function is the same - i.e. the coarser view
// obtained by merging adjacent LineBlocks rather than a second,
// independently-computed pass. Like LineBlock, this is chronological, not
// a deduplicated per-function summary: recursion or repeated calls produce
// sibling FunctionBlocks, not one merged block.
struct FunctionBlock {
    std::string function_name;  // Empty when no debug info covered this span.
    uint64_t entry_addr;        // First instruction's address in this block.

    std::vector<LineBlock> line_blocks;
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_FUNCTION_BLOCK_HPP_
