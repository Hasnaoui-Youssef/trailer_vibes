#ifndef TRAILER_TRACE_SINK_TRACE_RECORD_SINK_HPP_
#define TRAILER_TRACE_SINK_TRACE_RECORD_SINK_HPP_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
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

    // Occurrence count per dropped (non-retained) OpenCSD element kind.
    const std::map<ocsd_gen_trc_elem_t, uint64_t> &dropped_element_counts() const { return dropped_elements_; }

private:
    std::vector<TraceRecord> records_;
    std::map<ocsd_gen_trc_elem_t, uint64_t> dropped_elements_;

    // Index into records_ of the most recently retained instruction-range /
    // exception / exception-return record - the only kinds ETMv4 allows a
    // Cycle Count or Timestamp element to attach to.
    std::optional<size_t> last_anchor_index_;

    // Set by a Timestamp Marker element, cleared by the Timestamp element it
    // announces (or by an intervening discontinuity).
    std::optional<size_t> pending_ts_marker_index_;
};

}  // namespace trace

#endif  // TRAILER_TRACE_SINK_TRACE_RECORD_SINK_HPP_
