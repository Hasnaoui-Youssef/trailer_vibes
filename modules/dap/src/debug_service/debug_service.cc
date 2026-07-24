//===-- debug_service.cc ---------------------------------------------------===//
//
// Forked from LLVM's lldb-dap DAP.cpp (Apache-2.0 WITH LLVM-exception) - see
// debug_service.hpp for what was trimmed and why, and
// project-dap-layer-fork-strategy memory for the overall fork strategy.
//
//===----------------------------------------------------------------------===//

#include "debug_service/debug_service.hpp"

#include "core/components/exception_breakpoint.hpp"
#include "core/components/execution_controller.hpp"
#include "core/components/target_manager.hpp"
#include "dap/orchestrator.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_events.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "debug_service/json_utils.hpp"
#include "debug_service/lldb_utils.hpp"
#include "dap/response_handler.hpp"

#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBFile.h"
#include "lldb/API/SBLanguageRuntime.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBMutex.h"
#include "lldb/API/SBProcess.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <utility>
#include <variant>

using namespace dap::protocol;

namespace {

std::mutex g_log_mutex;

}  // namespace

namespace dap {

DebugService::DebugService(Orchestrator &orchestrator, providers::LldbProvider &lldb_provider,
                            core::DebugContext &context)
    : log(llvm::errs(), g_log_mutex),
      debugger(lldb_provider.debugger),
      target(lldb_provider.target),
      m_orchestrator(orchestrator),
      m_lldb_provider(lldb_provider),
      m_context(context) {}

DebugService::~DebugService() { m_context.Execution().StopEventHandlers(); }

void DebugService::SendJSON(const llvm::json::Value &json) {
    // FIXME: Instead of parsing the output message from JSON, pass the
    // `Message` as parameter to `SendJSON` - matches the FIXME already in
    // upstream lldb-dap.
    Message message;
    llvm::json::Path::Root root;
    if (!fromJSON(json, message, root)) {
        std::cerr << "debug_service: encoding failed: " << llvm::toString(root.getError()) << "\n";
        return;
    }
    Send(message);
}

protocol::Id DebugService::Send(const protocol::Message &message) {
    std::lock_guard<std::mutex> guard(call_mutex);
    Message msg = std::visit(
        [this](auto &&m) -> Message {
            if (m.seq == kCalculateSeq) {
                m.seq = m_orchestrator.NextSeq();
            }
            assert(m.seq > 0 && "message sequence must be greater than zero.");
            return m;
        },
        Message(message));

    if (const protocol::Event *event = std::get_if<protocol::Event>(&msg)) {
        m_orchestrator.SendMessage(toJSON(*event));
        return event->seq;
    }
    if (const Request *req = std::get_if<Request>(&msg)) {
        m_orchestrator.SendMessage(toJSON(*req));
        return req->seq;
    }
    if (const Response *resp = std::get_if<Response>(&msg)) {
        // If the debugger was interrupted, convert this response into a
        // 'cancelled' response because we might have a partial result.
        if (debugger.InterruptRequested()) {
            protocol::Response cancelled{
                /*request_seq=*/resp->request_seq,
                /*command=*/resp->command,
                /*success=*/false,
                /*message=*/eResponseMessageCancelled,
                /*body=*/std::nullopt,
                /*seq=*/resp->seq,
            };
            m_orchestrator.SendMessage(toJSON(cancelled));
        } else {
            m_orchestrator.SendMessage(toJSON(*resp));
        }
        return resp->seq;
    }

    llvm_unreachable("Unexpected message type");
}

void DebugService::SendOutput(OutputType o, const llvm::StringRef output) {
    if (output.empty()) {
        return;
    }

    const char *category = nullptr;
    switch (o) {
        case OutputType::Console:
            category = "console";
            break;
        case OutputType::Important:
            category = "important";
            break;
        case OutputType::Stdout:
            category = "stdout";
            break;
        case OutputType::Stderr:
            category = "stderr";
            break;
        case OutputType::Telemetry:
            category = "telemetry";
            break;
    }

    // Send each line of output as an individual event, including the
    // newline if present.
    size_t idx = 0;
    do {
        size_t end = output.find('\n', idx);
        if (end == llvm::StringRef::npos) {
            end = output.size() - 1;
        }
        llvm::json::Object event(CreateEventObject("output"));
        llvm::json::Object body;
        body.try_emplace("category", category);
        EmplaceSafeString(body, "output", output.slice(idx, end + 1).str());
        event.try_emplace("body", std::move(body));
        SendJSON(llvm::json::Value(std::move(event)));
        idx = end + 1;
    } while (idx < output.size());
}

void __attribute__((format(printf, 3, 4))) DebugService::SendFormattedOutput(OutputType o, const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    int actual_length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    SendOutput(o, llvm::StringRef(buffer, std::min<int>(actual_length, sizeof(buffer))));
}


void DebugService::SendTerminatedEvent() {
    llvm::call_once(terminated_event_flag, [&] {
        m_context.Session().RunTerminateCommands();
        llvm::json::Object event(CreateTerminatedEventObject(target));
        SendJSON(llvm::json::Value(std::move(event)));
    });
}

bool DebugService::IsCancelled(const protocol::Request &req) {
    std::lock_guard<std::mutex> guard(m_cancelled_requests_mutex);
    return m_cancelled_requests.contains(req.seq);
}

void DebugService::ClearCancelRequest(const CancelArguments &args) {
    std::lock_guard<std::mutex> guard(m_cancelled_requests_mutex);
    if (args.requestId) {
        m_cancelled_requests.erase(*args.requestId);
    }
}

void DebugService::SetFrameFormat(llvm::StringRef format) {
    lldb::SBError error;
    frame_format = lldb::SBFormat(format.str().c_str(), error);
    if (error.Fail()) {
        SendOutput(OutputType::Console,
                   llvm::formatv("The provided frame format '{0}' couldn't be parsed: {1}\n", format,
                                 error.GetCString())
                       .str());
    }
}

void DebugService::SetThreadFormat(llvm::StringRef format) {
    lldb::SBError error;
    thread_format = lldb::SBFormat(format.str().c_str(), error);
    if (error.Fail()) {
        SendOutput(OutputType::Console,
                   llvm::formatv("The provided thread format '{0}' couldn't be parsed: {1}\n", format,
                                 error.GetCString())
                       .str());
    }
}

protocol::Capabilities DebugService::GetCapabilities() {
    protocol::Capabilities capabilities;

    capabilities.supportedFeatures = {
        protocol::eAdapterFeatureLogPoints,
        protocol::eAdapterFeatureSteppingGranularity,
        protocol::eAdapterFeatureValueFormattingOptions,
    };

    // Handlers no longer live inside DebugService (they register with the
    // Orchestrator directly - see handlers/register_handlers.hpp), so their
    // contributed features are aggregated from there instead.
    llvm::SmallDenseSet<AdapterFeature, 1> handler_features = m_orchestrator.AggregatedHandlerFeatures();
    capabilities.supportedFeatures.insert(handler_features.begin(), handler_features.end());

    // Matches the original call site exactly (unconditional, not
    // once_flag-guarded - init_exception_breakpoints_flag exists on
    // BreakpointManager but, as on the old DebugService, is currently
    // unused by this call).
    core::BreakpointManager &breakpoints = m_context.Breakpoints();
    breakpoints.PopulateExceptionBreakpoints();
    std::vector<protocol::ExceptionBreakpointsFilter> filters;
    for (const auto &exc_bp : breakpoints.exception_breakpoints) {
        filters.emplace_back(core::CreateExceptionBreakpointFilter(exc_bp));
    }
    capabilities.exceptionBreakpointFilters = std::move(filters);

    capabilities.completionTriggerCharacters = {".", " ", "\t"};
    capabilities.lldbExtVersion = debugger.GetVersionString();

    return capabilities;
}

protocol::Capabilities DebugService::GetCustomCapabilities() {
    protocol::Capabilities capabilities;
    const llvm::DenseSet<AdapterFeature> all_custom_features = {
        protocol::eAdapterFeatureSupportsModuleSymbolsRequest,
    };
    for (auto &feature : m_orchestrator.AggregatedHandlerFeatures()) {
        if (all_custom_features.contains(feature)) {
            capabilities.supportedFeatures.insert(feature);
        }
    }
    return capabilities;
}

}  // namespace dap
