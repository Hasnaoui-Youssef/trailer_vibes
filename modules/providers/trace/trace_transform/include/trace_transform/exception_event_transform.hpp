#ifndef TRAILER_TRACE_TRANSFORM_EXCEPTION_EVENT_TRANSFORM_HPP_
#define TRAILER_TRACE_TRANSFORM_EXCEPTION_EVENT_TRANSFORM_HPP_

#include <cstdint>
#include <span>
#include <vector>

#include "trace_model/exception_event.hpp"
#include "trace_sink/trace_record.hpp"
#include "trace_transform/transform.hpp"

namespace xform {

// The engine's TraceRecord -> exception-entry/return projection, indexed
// against the same executed-instruction count TraceTransform<TraceGap> uses,
// so callers can correlate an exception boundary to a position in the
// executed-instruction stream.
template <>
struct TraceTransform<model::ExceptionEvent> {
    static std::vector<model::ExceptionEvent> Apply(std::span<const trace::TraceRecord> records) {
        std::vector<model::ExceptionEvent> events;
        uint64_t executed_count = 0;

        for (const trace::TraceRecord &record : records) {
            switch (record.kind) {
                case trace::TraceRecordKind::kInstructionRange: {
                    const uint32_t executed_in_range =
                        record.last_instr_executed ? record.num_instr : record.num_instr - 1;
                    executed_count += executed_in_range;
                    break;
                }
                case trace::TraceRecordKind::kException:
                    events.push_back(model::ExceptionEvent{executed_count, record.exception_number, false});
                    break;
                case trace::TraceRecordKind::kExceptionReturn:
                    events.push_back(model::ExceptionEvent{executed_count, 0, true});
                    break;
                default:
                    break;
            }
        }

        return events;
    }
};

}  // namespace xform

#endif  // TRAILER_TRACE_TRANSFORM_EXCEPTION_EVENT_TRANSFORM_HPP_
