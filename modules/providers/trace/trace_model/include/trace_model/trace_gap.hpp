#ifndef TRAILER_TRACE_MODEL_TRACE_GAP_HPP_
#define TRAILER_TRACE_MODEL_TRACE_GAP_HPP_

#include <cstdint>

namespace model {

enum class GapReason : uint8_t { kCaptureBoundary, kTraceOn, kOverflow, kNoSync };

// A discontinuity in the executed-instruction stream: instruction_index is
// the position, in that stream, right before the gap. Produced by
// TraceTransform<TraceGap> from TraceRecord kTraceOn/kNoSync elements, plus
// one kCaptureBoundary gap TraceSession::Append prepends per capture after
// the first (see trace_provider).
struct TraceGap {
    uint64_t instruction_index;
    GapReason reason;
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_TRACE_GAP_HPP_
