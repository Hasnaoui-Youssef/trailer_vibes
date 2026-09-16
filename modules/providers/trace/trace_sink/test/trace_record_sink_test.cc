// Covers TraceRecordSink's Cycle Count / Timestamp attachment: plain
// attach-to-preceding-anchor, TraceOn/NoSync skipped as anchors,
// Timestamp Marker deferral (and its oldest-marker-ignored and
// discontinuity-invalidation rules), the no-anchor-yet drop case, and the
// cumulative vs. non-cumulative cycle count field split.

#include <cstdlib>
#include <iostream>
#include <string>

#include <opencsd.h>

#include "trace_sink/trace_record_sink.hpp"

namespace {

int Fail(const std::string &message) {
    std::cerr << "trace_record_sink_test: " << message << "\n";
    return EXIT_FAILURE;
}

OcsdTraceElement InstrRange(ocsd_vaddr_t start, ocsd_vaddr_t end) {
    OcsdTraceElement elem(OCSD_GEN_TRC_ELEM_INSTR_RANGE);
    elem.setAddrRange(start, end, 1);
    elem.setLastInstrInfo(true, OCSD_INSTR_OTHER, OCSD_S_INSTR_NONE, 2);
    return elem;
}

OcsdTraceElement TraceOn() { return OcsdTraceElement(OCSD_GEN_TRC_ELEM_TRACE_ON); }

OcsdTraceElement NoSync() {
    OcsdTraceElement elem(OCSD_GEN_TRC_ELEM_NO_SYNC);
    elem.setUnSyncEOTReason(UNSYNC_OVERFLOW);
    return elem;
}

OcsdTraceElement CycleCount(uint32_t count) {
    OcsdTraceElement elem(OCSD_GEN_TRC_ELEM_CYCLE_COUNT);
    elem.setCycleCount(count);
    return elem;
}

OcsdTraceElement Timestamp(uint64_t ts) {
    OcsdTraceElement elem(OCSD_GEN_TRC_ELEM_TIMESTAMP);
    elem.setTS(ts);
    return elem;
}

OcsdTraceElement TimestampWithCycleCount(uint64_t ts, uint32_t count) {
    OcsdTraceElement elem = Timestamp(ts);
    elem.setCycleCount(count);
    return elem;
}

OcsdTraceElement TsMarker() {
    OcsdTraceElement elem(OCSD_GEN_TRC_ELEM_SYNC_MARKER);
    elem.setSyncMarker(trace_marker_payload_t{ELEM_MARKER_TS, 0});
    return elem;
}

int Feed(trace::TraceRecordSink &sink, const OcsdTraceElement &elem) {
    return sink.TraceElemIn(0, 1, elem) == OCSD_RESP_CONT ? EXIT_SUCCESS : EXIT_FAILURE;
}

int CheckPlainAttachAndFieldSplit() {
    trace::TraceRecordSink sink;
    Feed(sink, InstrRange(0x1000, 0x1002));
    Feed(sink, CycleCount(42));
    Feed(sink, TimestampWithCycleCount(0x1122334455, 7));

    const std::vector<trace::TraceRecord> records = sink.TakeRecords();
    if (records.size() != 1) {
        return Fail("expected 1 retained record, got " + std::to_string(records.size()));
    }
    const trace::TraceRecord &rec = records[0];
    if (!rec.cycle_count || *rec.cycle_count != 42) {
        return Fail("expected cumulative cycle_count 42 attached to the instruction range");
    }
    if (!rec.timestamp || *rec.timestamp != 0x1122334455) {
        return Fail("expected timestamp attached to the instruction range");
    }
    if (!rec.timestamp_cycle_count || *rec.timestamp_cycle_count != 7) {
        return Fail("expected the timestamp-embedded cycle count in timestamp_cycle_count, not cycle_count");
    }
    if (*rec.cycle_count != 42) {
        return Fail("timestamp-embedded cycle count must not overwrite the cumulative cycle_count");
    }
    return EXIT_SUCCESS;
}

int CheckTraceOnAndNoSyncAreNotAnchors() {
    trace::TraceRecordSink sink;
    Feed(sink, InstrRange(0x2000, 0x2002));
    Feed(sink, TraceOn());
    Feed(sink, CycleCount(9));

    const std::vector<trace::TraceRecord> records = sink.TakeRecords();
    if (records.size() != 2) {
        return Fail("expected 2 retained records, got " + std::to_string(records.size()));
    }
    if (records[0].kind != trace::TraceRecordKind::kInstructionRange || !records[0].cycle_count ||
        *records[0].cycle_count != 9) {
        return Fail("expected the cycle count to skip past TraceOn and land on the instruction range");
    }
    if (records[1].cycle_count) {
        return Fail("TraceOn must not receive a cycle count");
    }
    return EXIT_SUCCESS;
}

int CheckTimestampMarkerDefersToTheCapturedAnchor() {
    trace::TraceRecordSink sink;
    Feed(sink, InstrRange(0x3000, 0x3002));  // anchor A
    Feed(sink, TsMarker());
    Feed(sink, InstrRange(0x3002, 0x3004));  // anchor B, generated after the marker
    Feed(sink, Timestamp(0xAABBCC));

    const std::vector<trace::TraceRecord> records = sink.TakeRecords();
    if (records.size() != 2) {
        return Fail("expected 2 retained records, got " + std::to_string(records.size()));
    }
    if (!records[0].timestamp || *records[0].timestamp != 0xAABBCC) {
        return Fail("expected the deferred timestamp on anchor A, the one live when the marker was seen");
    }
    if (records[1].timestamp) {
        return Fail("anchor B must not receive the timestamp meant for the marked anchor A");
    }
    return EXIT_SUCCESS;
}

int CheckOldestMarkerIsIgnored() {
    trace::TraceRecordSink sink;
    Feed(sink, InstrRange(0x4000, 0x4002));  // anchor A
    Feed(sink, TsMarker());
    Feed(sink, InstrRange(0x4002, 0x4004));  // anchor B
    Feed(sink, TsMarker());
    Feed(sink, InstrRange(0x4004, 0x4006));  // anchor C
    Feed(sink, Timestamp(0x99));

    const std::vector<trace::TraceRecord> records = sink.TakeRecords();
    if (records[0].timestamp || records[2].timestamp) {
        return Fail("only the newest marker's anchor should receive the timestamp");
    }
    if (!records[1].timestamp || *records[1].timestamp != 0x99) {
        return Fail("expected the timestamp on anchor B, the newest marker's anchor");
    }
    return EXIT_SUCCESS;
}

int CheckDiscontinuityInvalidatesPendingMarker() {
    trace::TraceRecordSink sink;
    Feed(sink, InstrRange(0x5000, 0x5002));  // anchor A
    Feed(sink, TsMarker());
    Feed(sink, NoSync());
    Feed(sink, InstrRange(0x5002, 0x5004));  // anchor B
    Feed(sink, Timestamp(0x77));

    const std::vector<trace::TraceRecord> records = sink.TakeRecords();
    if (records[0].timestamp) {
        return Fail("a discontinuity after the marker must invalidate it");
    }
    if (!records[2].timestamp || *records[2].timestamp != 0x77) {
        return Fail("expected the timestamp to fall back to the current anchor after the discontinuity");
    }
    return EXIT_SUCCESS;
}

int CheckNoAnchorYetIsDropped() {
    trace::TraceRecordSink sink;
    Feed(sink, CycleCount(1));
    Feed(sink, Timestamp(1));

    if (!sink.TakeRecords().empty()) {
        return Fail("expected no retained records with no anchor available");
    }
    const std::map<ocsd_gen_trc_elem_t, uint64_t> &dropped = sink.dropped_element_counts();
    if (dropped.at(OCSD_GEN_TRC_ELEM_CYCLE_COUNT) != 1 || dropped.at(OCSD_GEN_TRC_ELEM_TIMESTAMP) != 1) {
        return Fail("expected both elements counted as dropped");
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main() {
    if (CheckPlainAttachAndFieldSplit() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (CheckTraceOnAndNoSyncAreNotAnchors() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (CheckTimestampMarkerDefersToTheCapturedAnchor() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (CheckOldestMarkerIsIgnored() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (CheckDiscontinuityInvalidatesPendingMarker() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (CheckNoAnchorYetIsDropped() != EXIT_SUCCESS) return EXIT_FAILURE;

    std::cerr << "trace_record_sink_test: OK\n";
    return EXIT_SUCCESS;
}
