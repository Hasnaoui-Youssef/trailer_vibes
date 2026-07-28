#ifndef TRAILER_TRACE_DECODER_TRACE_DECODER_HPP_
#define TRAILER_TRACE_DECODER_TRACE_DECODER_HPP_

#include <expected>
#include <span>
#include <string>
#include <vector>

#include "trace_model/instruction_trace_decode_config.hpp"
#include "trace_model/load_segment.hpp"
#include "trace_sink/trace_record.hpp"

namespace decode {

// Handles OpenCSD internally
class TraceDecoder {
public:
    std::expected<std::vector<trace::TraceRecord>, std::string> Decode(
        const model::InstructionTraceDecodeConfig &config, std::span<const model::LoadSegment> segments) const;
};

}  // namespace decode

#endif  // TRAILER_TRACE_DECODER_TRACE_DECODER_HPP_
