#ifndef TRAILER_TRACE_TRANSFORM_TRACE_GAP_TRANSFORM_HPP_
#define TRAILER_TRACE_TRANSFORM_TRACE_GAP_TRANSFORM_HPP_

#include <cstdint>
#include <span>
#include <vector>

#include <opencsd.h>

#include "trace_model/trace_gap.hpp"
#include "trace_sink/trace_record.hpp"
#include "trace_transform/transform.hpp"

namespace xform {

// The engine's TraceRecord -> discontinuity projection. Needs no resolve
// callable: kInstructionRange::num_instr/last_instr_executed give the
// running executed-instruction count directly, and kTraceOn/kNoSync
// records already carry their own reason code - exactly the "execution
// address changed with no branch recorded" signal a gap represents.
template <>
struct TraceTransform<model::TraceGap> {
    static std::vector<model::TraceGap> Apply(std::span<const trace::TraceRecord> records) {
        std::vector<model::TraceGap> gaps;
        uint64_t executed_count = 0;

        for (const trace::TraceRecord &record : records) {
            switch (record.kind) {
                case trace::TraceRecordKind::kInstructionRange: {
                    const uint32_t executed_in_range =
                        record.last_instr_executed ? record.num_instr : record.num_instr - 1;
                    executed_count += executed_in_range;
                    break;
                }
                case trace::TraceRecordKind::kTraceOn: {
                    const model::GapReason reason = record.trace_on_reason == TRACE_ON_OVERFLOW
                                                         ? model::GapReason::kOverflow
                                                         : model::GapReason::kTraceOn;
                    gaps.push_back(model::TraceGap{executed_count, reason});
                    break;
                }
                case trace::TraceRecordKind::kNoSync: {
                    const model::GapReason reason = record.no_sync_reason == UNSYNC_OVERFLOW
                                                         ? model::GapReason::kOverflow
                                                         : model::GapReason::kNoSync;
                    gaps.push_back(model::TraceGap{executed_count, reason});
                    break;
                }
                default:
                    break;
            }
        }

        return gaps;
    }
};

}  // namespace xform

#endif  // TRAILER_TRACE_TRANSFORM_TRACE_GAP_TRANSFORM_HPP_
