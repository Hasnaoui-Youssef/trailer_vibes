#include "trace_sink/trace_record_sink.hpp"

namespace trace {

namespace {

TraceRecord MakeInstructionRange(const OcsdTraceElement &elem) {
    TraceRecord record{};
    record.kind = TraceRecordKind::kInstructionRange;
    record.isa = elem.isa;
    record.start_addr = elem.st_addr;
    record.end_addr = elem.en_addr;
    record.has_end_addr = true;
    record.num_instr = elem.num_instr_range;
    record.last_instr_type = elem.last_i_type;
    record.last_instr_subtype = elem.last_i_subtype;
    record.last_instr_executed = elem.last_instr_exec != 0;
    return record;
}

TraceRecord MakeException(const OcsdTraceElement &elem) {
    TraceRecord record{};
    record.kind = TraceRecordKind::kException;
    record.isa = elem.isa;
    record.start_addr = elem.st_addr;
    record.end_addr = elem.en_addr;
    record.has_end_addr = elem.excep_ret_addr != 0;
    record.exception_number = elem.exception_number;
    return record;
}

TraceRecord MakeExceptionReturn(const OcsdTraceElement &elem) {
    TraceRecord record{};
    record.kind = TraceRecordKind::kExceptionReturn;
    record.isa = elem.isa;
    record.start_addr = elem.st_addr;
    record.has_end_addr = false;
    return record;
}

TraceRecord MakeTraceOn(const OcsdTraceElement &elem) {
    TraceRecord record{};
    record.kind = TraceRecordKind::kTraceOn;
    record.trace_on_reason = elem.trace_on_reason;
    return record;
}

TraceRecord MakeNoSync(const OcsdTraceElement &elem) {
    TraceRecord record{};
    record.kind = TraceRecordKind::kNoSync;
    record.no_sync_reason = elem.unsync_eot_info;
    return record;
}

}  // namespace

ocsd_datapath_resp_t TraceRecordSink::TraceElemIn(const ocsd_trc_index_t index_sop,
                                                   const uint8_t trc_chan_id,
                                                   const OcsdTraceElement &elem) {
    TraceRecord record;

    switch (elem.getType()) {
        case OCSD_GEN_TRC_ELEM_INSTR_RANGE:
            record = MakeInstructionRange(elem);
            break;
        case OCSD_GEN_TRC_ELEM_EXCEPTION:
            record = MakeException(elem);
            break;
        case OCSD_GEN_TRC_ELEM_EXCEPTION_RET:
            record = MakeExceptionReturn(elem);
            break;
        case OCSD_GEN_TRC_ELEM_TRACE_ON:
            record = MakeTraceOn(elem);
            break;
        case OCSD_GEN_TRC_ELEM_NO_SYNC:
            record = MakeNoSync(elem);
            break;
        default:
            return OCSD_RESP_CONT;
    }

    record.index_sop = index_sop;
    record.trace_id = trc_chan_id;
    records_.push_back(record);

    return OCSD_RESP_CONT;
}

}  // namespace trace
