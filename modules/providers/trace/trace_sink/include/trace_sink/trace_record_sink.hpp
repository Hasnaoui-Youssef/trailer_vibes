#ifndef TRAILER_TRACE_SINK_TRACE_RECORD_SINK_HPP_
#define TRAILER_TRACE_SINK_TRACE_RECORD_SINK_HPP_

#include <vector>

#include <opencsd.h>
#include "trace_sink/trace_record.hpp"

namespace trace {

// Attachment point for a DecodeTree's generic trace element output
// (DecodeTree::setGenTraceElemOutI). Collects instruction-range and
// exception entry/exit elements into a flat vector of TraceRecord and
// discards every other generic element type.
class TraceRecordSink : public ITrcGenElemIn {
public:
    TraceRecordSink() = default;
    ~TraceRecordSink() override = default;

    ocsd_datapath_resp_t TraceElemIn(const ocsd_trc_index_t index_sop,
                                      const uint8_t trc_chan_id,
                                      const OcsdTraceElement &elem) override;

    const std::vector<TraceRecord> &records() const { return records_; }

    // Hands ownership of the accumulated records to the caller and leaves
    // the sink empty, ready to be reused for another decode pass.
    std::vector<TraceRecord> TakeRecords() { return std::move(records_); }

private:
    std::vector<TraceRecord> records_;
};

}  // namespace trace

#endif  // TRAILER_TRACE_SINK_TRACE_RECORD_SINK_HPP_
