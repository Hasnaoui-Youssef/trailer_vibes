#ifndef TRAILER_TRACE_MODEL_EXCEPTION_EVENT_HPP_
#define TRAILER_TRACE_MODEL_EXCEPTION_EVENT_HPP_

#include <cstdint>

namespace model {

// An exception entry or return in the executed-instruction stream:
// instruction_index is the position, in that stream, right before the event.
// Produced by TraceTransform<ExceptionEvent> from TraceRecord
// kException/kExceptionReturn elements.
struct ExceptionEvent {
    uint64_t instruction_index;
    uint32_t exception_number;  // valid on entry only; 0 on return
    bool is_return;
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_EXCEPTION_EVENT_HPP_
