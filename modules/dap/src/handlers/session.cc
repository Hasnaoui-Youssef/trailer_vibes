#include "dap/session.hpp"

#include "core/components/disassembly_manager.hpp"
#include "core/debug_context.hpp"
#include "core/event_bus.hpp"
#include "dap/dap_log.hpp"
#include "dap/orchestrator.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol_support.hpp"
#include "dap/json_utils.hpp"
#include "disassembler/program_disassembler.hpp"
#include "handlers/capabilities.hpp"
#include "handlers/register_handlers.hpp"
#include "llvm/Support/Base64.h"
#include "llvm/Support/JSON.h"
#include <mutex>
#include <variant>

namespace dap {

namespace {

std::mutex g_log_mutex;

llvm::json::Array ToJSON(const std::vector<model::InlineFrame> &frames) {
    llvm::json::Array result;
    for (const model::InlineFrame &frame : frames) {
        result.push_back(llvm::json::Object{
            {"function", frame.function}, {"file", frame.file}, {"line", frame.line}, {"column", frame.column}});
    }
    return result;
}

llvm::json::Array ResolvedFrames(uint64_t address, const disasm::ProgramDisassembler *program) {
    if (!program)
        return {};
    const model::InstructionInfo *info = program->InstructionInfoAt(address);
    if (!info)
        return {};
    return ToJSON(info->location.frames);
}

llvm::json::Value ToJSON(const model::ReconstructedInstruction &instruction,
                          const disasm::ProgramDisassembler *program) {
    const model::DecodedInstruction &insn = instruction.insn;
    return llvm::json::Object{
        {"address", EncodeMemoryReference(insn.address)},
        {"bytes", insn.bytes},
        {"mnemonic", insn.mnemonic},
        {"operands", insn.operands},
        {"traceIndex", instruction.trace_index},
        {"traceId", instruction.trace_id},
        {"frames", ResolvedFrames(insn.address, program)},
    };
}

llvm::json::Value ToJSON(const model::LineBlock &block, const disasm::ProgramDisassembler *program) {
    return llvm::json::Object{
        {"startAddress", EncodeMemoryReference(block.start_addr)},
        {"endAddress", EncodeMemoryReference(block.end_addr)},
        {"instructionCount", block.instr_count},
        {"frames", ResolvedFrames(block.start_addr, program)},
        {"traceIndex", block.trace_index},
        {"traceId", block.trace_id},
    };
}

llvm::json::Value ToJSON(const model::FunctionBlock &block, const disasm::ProgramDisassembler *program) {
    llvm::json::Array line_blocks;
    for (const model::LineBlock &line_block : block.line_blocks)
        line_blocks.push_back(ToJSON(line_block, program));
    return llvm::json::Object{
        {"functionName", block.function_name},
        {"entryAddress", EncodeMemoryReference(block.entry_addr)},
        {"lineBlocks", std::move(line_blocks)},
    };
}

const char *ToString(model::GapReason reason) {
    switch (reason) {
    case model::GapReason::kCaptureBoundary: return "captureBoundary";
    case model::GapReason::kTraceOn: return "traceOn";
    case model::GapReason::kOverflow: return "overflow";
    case model::GapReason::kNoSync: return "noSync";
    }
    return "unknown";
}

llvm::json::Value ToJSON(const model::TraceGap &gap) {
    return llvm::json::Object{{"instructionIndex", gap.instruction_index}, {"reason", ToString(gap.reason)}};
}

class SessionImpl final : public Session {
public:
    explicit SessionImpl(Orchestrator &orchestrator)
        : orchestrator_(orchestrator),
          log_(llvm::errs(), g_log_mutex),
          handlers_(RegisterDebugHandlers(orchestrator, context_)) {
        WireContext();
    }

private:
    void WireContext() {
        // Binding disconnect to stop the Orchestrator
        context_.SetRequestStop([this] { orchestrator_.RequestStop(); });
        context_.SetLogDiagnostic([this](std::string message) { DAP_LOG(log_, "{0}", message); });
        context_.SetClientName([this] { return orchestrator_.ClientName(); });
        context_.SetClientFeatureEnabled(
            [this](dap::protocol::ClientFeature feature) { return orchestrator_.ClientFeatureEnabled(feature); });
        context_.SetGetCustomCapabilities([this] { return AssembleCustomCapabilities(orchestrator_); });

        context_.Events().Subscribe([this](const core::DomainEvent &event) {
            std::visit([this](const auto &domain_event) { HandleDomainEvent(domain_event); }, event);
        });
    }

    void HandleDomainEvent(const core::OutputEvent &event) {
        if (event.text.empty())
            return;

        const char *category = nullptr;
        switch (event.category) {
        case core::OutputCategory::Console: category = "console"; break;
        case core::OutputCategory::Important: category = "important"; break;
        case core::OutputCategory::Stdout: category = "stdout"; break;
        case core::OutputCategory::Stderr: category = "stderr"; break;
        case core::OutputCategory::Telemetry: category = "telemetry"; break;
        }

        llvm::StringRef output = event.text;
        size_t idx = 0;
        do {
            size_t end = output.find('\n', idx);
            if (end == llvm::StringRef::npos)
                end = output.size() - 1;
            llvm::json::Object body;
            body.try_emplace("category", category);
            EmplaceSafeString(body, "output", output.slice(idx, end + 1).str());
            orchestrator_.Send(dap::protocol::Event{"output", llvm::json::Value(std::move(body))});
            idx = end + 1;
        } while (idx < output.size());
    }

    template <typename EventBody>
    void SendTypedEvent(llvm::StringRef name, EventBody body) {
        orchestrator_.Send(dap::protocol::Event{name.str(), std::move(body)});
    }

    void HandleDomainEvent(const core::StoppedEvent &event) { SendTypedEvent("stopped", event.body); }
    void HandleDomainEvent(const core::ContinuedEvent &event) { SendTypedEvent("continued", event.body); }
    void HandleDomainEvent(const core::ExitedEvent &event) { SendTypedEvent("exited", event.body); }
    void HandleDomainEvent(const core::ThreadExitedEvent &event) { SendTypedEvent("thread", event.body); }
    void HandleDomainEvent(const core::ProcessEvent &event) { SendTypedEvent("process", event.body); }
    void HandleDomainEvent(const core::CapabilitiesEvent &event) { SendTypedEvent("capabilities", event.body); }
    void HandleDomainEvent(const core::InvalidatedEvent &event) { SendTypedEvent("invalidated", event.body); }
    void HandleDomainEvent(const core::MemoryEvent &event) { SendTypedEvent("memory", event.body); }
    void HandleDomainEvent(const core::ModuleEvent &event) { SendTypedEvent("module", event.body); }
    void HandleDomainEvent(const core::BreakpointEvent &event) { SendTypedEvent("breakpoint", event.body); }

    void HandleDomainEvent(const core::ResetEvent &event) {
        const char *phase = event.phase == core::ResetPhase::Started ? "started" : "complete";
        orchestrator_.Send(dap::protocol::Event{"trailerReset", llvm::json::Object{{"phase", phase}}});
    }

    void HandleDomainEvent(const core::TraceStatusEvent &event) { SendTypedEvent("trailerTraceStatus", event.body); }

    void HandleDomainEvent(const core::WatchDataEvent &event) {
        orchestrator_.Send(dap::protocol::Event{
            "trailerWatchData",
            llvm::json::Object{
                {"watchId", event.watch_id},
                {"address", EncodeMemoryReference(event.address)},
                {"data", llvm::encodeBase64(event.data)},
                {"sequence", event.sequence},
            }});
    }

    void HandleDomainEvent(const core::WatchStateEvent &event) {
        llvm::json::Object body{
            {"watchId", event.watch_id},
            {"state", event.active ? "active" : "error"},
        };
        if (!event.detail.empty())
            body["detail"] = event.detail;
        orchestrator_.Send(dap::protocol::Event{"trailerWatchState", llvm::json::Value(std::move(body))});
    }

    void HandleDomainEvent(const core::TraceDataEvent &event) {
        llvm::Expected<const disasm::ProgramDisassembler &> program = context_.Disassembly().Program();
        const disasm::ProgramDisassembler *program_ptr = nullptr;
        if (program) {
            program_ptr = &*program;
        } else {
            DAP_LOG(log_, "trailerTraceData: source frames unavailable: {0}", llvm::toString(program.takeError()));
        }

        llvm::json::Array instructions;
        for (const model::ReconstructedInstruction &instruction : event.instructions)
            instructions.push_back(ToJSON(instruction, program_ptr));

        llvm::json::Array function_blocks;
        for (const model::FunctionBlock &block : event.function_blocks)
            function_blocks.push_back(ToJSON(block, program_ptr));

        llvm::json::Array gaps;
        for (const model::TraceGap &gap : event.gaps)
            gaps.push_back(ToJSON(gap));

        orchestrator_.Send(dap::protocol::Event{
            "trailerTraceData",
            llvm::json::Object{
                {"firstInstructionIndex", event.first_instruction_index},
                {"firstFunctionBlockIndex", event.first_function_block_index},
                {"instructions", std::move(instructions)},
                {"functionBlocks", std::move(function_blocks)},
                {"gaps", std::move(gaps)},
            }});
    }

    void HandleDomainEvent(const core::TerminatedEvent &event) {
        dap::protocol::Event evt{"terminated"};
        if (!event.statistics_json.empty()) {
            if (llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(event.statistics_json)) {
                evt.body = llvm::json::Object{{"$__lldb_statistics", std::move(*parsed)}};
            } else {
                DAP_LOG(log_, "failed to re-parse terminated-event statistics: {0}",
                        llvm::toString(parsed.takeError()));
            }
        }
        orchestrator_.Send(evt);
    }

    // Declaration order matters: context_ must be constructed before handlers_, fix me?
    Orchestrator &orchestrator_;
    Log log_;
    core::DebugContext context_;
    std::vector<std::unique_ptr<BaseRequestHandler>> handlers_;
};

}  // namespace

std::unique_ptr<Session> CreateSession(dap::Orchestrator &orchestrator) {
    return std::make_unique<SessionImpl>(orchestrator);
}

}  // namespace dap
