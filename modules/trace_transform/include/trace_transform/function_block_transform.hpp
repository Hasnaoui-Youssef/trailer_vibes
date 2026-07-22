#ifndef TRAILER_TRACE_TRANSFORM_FUNCTION_BLOCK_TRANSFORM_HPP_
#define TRAILER_TRACE_TRANSFORM_FUNCTION_BLOCK_TRANSFORM_HPP_

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "trace_model/function_block.hpp"
#include "trace_model/instruction_info.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_sink/trace_record.hpp"
#include "trace_transform/reconstructed_instruction_transform.hpp"
#include "trace_transform/transform.hpp"

namespace xform {

namespace detail {

// Whether two locations should merge into the same FunctionBlock: true
// when they share the same outermost (real, non-inlined) enclosing
// function. Two unresolved locations (no debug info) are treated as the
// same "unknown" function, so a run of undebuggable instructions collapses
// into one block instead of one per instruction.
inline bool SameFunction(const model::SourceLocation &a, const model::SourceLocation &b) {
    const bool a_empty = a.frames.empty();
    const bool b_empty = b.frames.empty();
    if (a_empty != b_empty) {
        return false;
    }
    if (a_empty) {
        return true;
    }
    return a.frames.back().function == b.frames.back().function;
}

// Case-insensitive suffix check for the conventional assembly source file
// extensions. Used by ForceNewLineBlock below.
inline bool IsAssemblySourceFile(std::string_view file) {
    auto has_suffix = [&](std::string_view suffix) {
        if (file.size() < suffix.size()) {
            return false;
        }
        const std::string_view tail = file.substr(file.size() - suffix.size());
        return std::ranges::equal(tail, suffix, [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
    };
    return has_suffix(".s") || has_suffix(".asm");
}

// Assembly source lines are essentially always exactly one instruction
// each, so LineBlock's usual merging of consecutive same-location
// instructions (valuable for C, where one line often expands to several
// instructions) adds a layer with no informational value for assembly
// source. Rather than changing the data model, this simply forces a new
// LineBlock every instruction whenever the innermost frame resolves to an
// assembly file. Unresolved locations (empty frames - no debug info at
// all) are untouched: "no debug info" and "assembly source" are different
// concepts, and today's "merge unresolved instructions into one block"
// behavior is preserved.
inline bool ForceNewLineBlock(const model::SourceLocation &location) {
    if (location.frames.empty()) {
        return false;
    }
    return IsAssemblySourceFile(location.frames.front().file);
}

}  // namespace detail

// The engine's TraceRecord -> function/line-grouping projection. Composes
// over TraceTransform<ReconstructedInstruction> rather than re-walking
// records independently - this reuses that specialization's range-walking,
// executed-flag, and provenance-stitching logic outright, at the cost of
// materializing an intermediate vector<ReconstructedInstruction> even when
// only FunctionBlocks are wanted. For an embedded-firmware-sized trace
// (hundreds of instructions) that cost is negligible, and it demonstrates
// the Transform mechanism actually composing rather than every
// specialization reimplementing its own walk.
template <>
struct TraceTransform<model::FunctionBlock> {
    template <typename Resolve>
    static std::vector<model::FunctionBlock> Apply(std::span<const trace::TraceRecord> records, Resolve resolve) {
        const std::vector<model::ReconstructedInstruction> instructions =
            Transform<model::ReconstructedInstruction>(records, resolve);

        std::vector<model::FunctionBlock> functions;

        for (const model::ReconstructedInstruction &instruction : instructions) {
            if (!instruction.executed) {
                continue;
            }

            const uint64_t address = instruction.insn.address;

            // ReconstructedInstruction doesn't carry a SourceLocation (see
            // its own doc comment) - re-querying resolve() here is a cheap
            // precomputed-table lookup, not the live DWARF work this
            // engine's whole precompute redesign exists to avoid.
            const model::InstructionInfo *info = resolve(address);
            model::SourceLocation location = info != nullptr ? info->location : model::SourceLocation{};

            const bool need_new_line_block = functions.empty() || functions.back().line_blocks.empty() ||
                                              functions.back().line_blocks.back().location != location ||
                                              detail::ForceNewLineBlock(location);

            if (need_new_line_block) {
                const bool need_new_function_block =
                    functions.empty() ||
                    !detail::SameFunction(functions.back().line_blocks.back().location, location);

                if (need_new_function_block) {
                    model::FunctionBlock function_block;
                    function_block.function_name =
                        location.frames.empty() ? std::string() : location.frames.back().function;
                    function_block.entry_addr = address;
                    functions.push_back(std::move(function_block));
                }

                model::LineBlock line_block;
                line_block.location = std::move(location);
                line_block.start_addr = address;
                line_block.end_addr = address + instruction.insn.size;
                line_block.instr_count = 1;
                line_block.trace_index = instruction.trace_index;
                line_block.trace_id = instruction.trace_id;
                functions.back().line_blocks.push_back(std::move(line_block));
            } else {
                model::LineBlock &line_block = functions.back().line_blocks.back();
                line_block.end_addr = address + instruction.insn.size;
                line_block.instr_count += 1;
            }
        }

        return functions;
    }
};

}  // namespace xform

#endif  // TRAILER_TRACE_TRANSFORM_FUNCTION_BLOCK_TRANSFORM_HPP_
