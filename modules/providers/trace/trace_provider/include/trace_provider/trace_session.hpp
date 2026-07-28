#ifndef TRAILER_TRACE_PROVIDER_TRACE_SESSION_HPP_
#define TRAILER_TRACE_PROVIDER_TRACE_SESSION_HPP_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "trace_model/function_block.hpp"
#include "trace_model/instruction_info.hpp"
#include "trace_model/instruction_trace_decode_config.hpp"
#include "trace_model/load_segment.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_model/trace_gap.hpp"

namespace trace_provider {

struct DecodedIncrement {
    std::vector<model::ReconstructedInstruction> instructions;
    std::vector<model::FunctionBlock> function_blocks;
    std::vector<model::TraceGap> gaps;
};

using ResolveInstruction = std::function<const model::InstructionInfo *(uint64_t)>;

// Owns the OpenCSD-decoded trace_sink::TraceRecord stream (never exposed:
// see trace_sink/trace_record.hpp's <opencsd.h> include, which is why this
// class exists rather than a free Decode() function). Each Append() runs a
// fresh decode over just that capture's bytes and returns only what it
// added - the fsync barrier plus reset-on-4x-fsync makes every capture
// self-contained, so this is correct semantics, not a missed optimization.
class TraceSession {
public:
    TraceSession(model::InstructionTraceDecodeConfig base_config, std::vector<model::LoadSegment> segments,
                 ResolveInstruction resolve);
    TraceSession(TraceSession &&) noexcept;
    TraceSession &operator=(TraceSession &&) noexcept;
    ~TraceSession();

    std::expected<DecodedIncrement, std::string> Append(std::span<const std::byte> capture);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace trace_provider

#endif  // TRAILER_TRACE_PROVIDER_TRACE_SESSION_HPP_
