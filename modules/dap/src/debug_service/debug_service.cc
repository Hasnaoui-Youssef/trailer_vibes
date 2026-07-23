//===-- debug_service.cc ---------------------------------------------------===//
//
// Forked from LLVM's lldb-dap DAP.cpp (Apache-2.0 WITH LLVM-exception) - see
// debug_service.hpp for what was trimmed and why, and
// project-dap-layer-fork-strategy memory for the overall fork strategy.
//
//===----------------------------------------------------------------------===//

#include "debug_service/debug_service.hpp"

#include "dap/debug_service_factory.hpp"
#include "dap/orchestrator.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_events.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "debug_service/event_helper.hpp"
#include "debug_service/exception_breakpoint.hpp"
#include "debug_service/json_utils.hpp"
#include "debug_service/lldb_utils.hpp"
#include "debug_service/protocol_utils.hpp"
#include "debug_service/request_handler.hpp"
#include "debug_service/response_handler.hpp"

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
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <variant>

using namespace dap::debug_service;
using namespace dap::protocol;

namespace {

/// Return string with first character capitalized.
std::string capitalize(llvm::StringRef str) {
    if (str.empty()) {
        return "";
    }
    return ((llvm::Twine)llvm::toUpper(str[0]) + str.drop_front()).str();
}

std::mutex g_log_mutex;

}  // namespace

namespace dap::debug_service {

DebugService::DebugService(Orchestrator &orchestrator)
    : log(llvm::errs(), g_log_mutex), broadcaster("trailer-dap"), m_orchestrator(orchestrator) {
    RegisterRequests();
}

DebugService::~DebugService() { StopEventHandlers(); }

std::vector<std::string> DebugService::SupportedCommands() const {
    std::vector<std::string> commands;
    commands.reserve(request_handlers.size());
    for (const auto &kv : request_handlers) {
        commands.push_back(kv.first().str());
    }
    return commands;
}

void DebugService::HandleRequest(const protocol::Request &request) {
    {
        std::lock_guard<std::mutex> guard(m_active_request_mutex);
        m_active_request = &request;
        if (debugger.InterruptRequested()) {
            debugger.CancelInterruptRequest();
        }
    }
    llvm::scope_exit cleanup([&]() {
        std::scoped_lock<std::mutex> active_request_lock(m_active_request_mutex);
        m_active_request = nullptr;
    });

    const auto handler_pos = request_handlers.find(request.command);
    if (handler_pos != request_handlers.end()) {
        handler_pos->second->Run(request);
    } else {
        UnknownRequestHandler handler(*this);
        handler.BaseRequestHandler::Run(request);
    }
}

void DebugService::PopulateExceptionBreakpoints() {
    if (lldb::SBDebugger::SupportsLanguage(lldb::eLanguageTypeC_plus_plus)) {
        exception_breakpoints.emplace_back(*this, "cpp_catch", "C++ Catch", lldb::eLanguageTypeC_plus_plus,
                                            eExceptionKindCatch);
        exception_breakpoints.emplace_back(*this, "cpp_throw", "C++ Throw", lldb::eLanguageTypeC_plus_plus,
                                            eExceptionKindThrow);
    }

    // Besides the hardcoded C++ case above, try to find any other languages
    // that support exception breakpoints using the SB API.
    for (int raw_lang = lldb::eLanguageTypeUnknown; raw_lang < lldb::eNumLanguageTypes; ++raw_lang) {
        lldb::LanguageType lang = static_cast<lldb::LanguageType>(raw_lang);

        if (lldb::SBLanguageRuntime::LanguageIsCFamily(lang)) {
            continue;
        }
        if (!lldb::SBDebugger::SupportsLanguage(lang)) {
            continue;
        }

        const char *name = lldb::SBLanguageRuntime::GetNameForLanguageType(lang);
        if (!name) {
            continue;
        }
        std::string raw_lang_name = name;
        std::string capitalized_lang_name = capitalize(name);

        if (lldb::SBLanguageRuntime::SupportsExceptionBreakpointsOnThrow(lang)) {
            const char *raw_throw_keyword = lldb::SBLanguageRuntime::GetThrowKeywordForLanguage(lang);
            std::string throw_keyword = raw_throw_keyword ? raw_throw_keyword : "throw";
            exception_breakpoints.emplace_back(*this, raw_lang_name + "_" + throw_keyword,
                                                capitalized_lang_name + " " + capitalize(throw_keyword), lang,
                                                eExceptionKindThrow);
        }

        if (lldb::SBLanguageRuntime::SupportsExceptionBreakpointsOnCatch(lang)) {
            const char *raw_catch_keyword = lldb::SBLanguageRuntime::GetCatchKeywordForLanguage(lang);
            std::string catch_keyword = raw_catch_keyword ? raw_catch_keyword : "catch";
            exception_breakpoints.emplace_back(*this, raw_lang_name + "_" + catch_keyword,
                                                capitalized_lang_name + " " + capitalize(catch_keyword), lang,
                                                eExceptionKindCatch);
        }
    }
}

ExceptionBreakpoint *DebugService::GetExceptionBreakpoint(llvm::StringRef filter) {
    for (auto &bp : exception_breakpoints) {
        if (bp.GetFilter() == filter) {
            return &bp;
        }
    }
    return nullptr;
}

ExceptionBreakpoint *DebugService::GetExceptionBreakpoint(const lldb::break_id_t bp_id) {
    for (auto &bp : exception_breakpoints) {
        if (bp.GetID() == bp_id) {
            return &bp;
        }
    }
    return nullptr;
}

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

int32_t DebugService::CreateSourceReference(lldb::addr_t address) {
    std::lock_guard<std::mutex> guard(m_source_references_mutex);
    auto iter = llvm::find(m_source_references, address);
    if (iter != m_source_references.end()) {
        return static_cast<int32_t>(std::distance(m_source_references.begin(), iter) + 1);
    }
    m_source_references.emplace_back(address);
    return static_cast<int32_t>(m_source_references.size());
}

std::optional<lldb::addr_t> DebugService::GetSourceReferenceAddress(int32_t reference) {
    std::lock_guard<std::mutex> guard(m_source_references_mutex);
    if (reference <= LLDB_DAP_INVALID_SRC_REF) {
        return std::nullopt;
    }
    if (static_cast<size_t>(reference) > m_source_references.size()) {
        return std::nullopt;
    }
    return m_source_references[reference - 1];
}

ExceptionBreakpoint *DebugService::GetExceptionBPFromStopReason(lldb::SBThread &thread) {
    const auto num = thread.GetStopReasonDataCount();
    ExceptionBreakpoint *exc_bp = nullptr;
    for (size_t i = 0; i < num; i += 2) {
        lldb::break_id_t bp_id = thread.GetStopReasonDataAtIndex(i);
        exc_bp = GetExceptionBreakpoint(bp_id);
        if (exc_bp == nullptr) {
            return nullptr;
        }
    }
    return exc_bp;
}

lldb::SBThread DebugService::GetLLDBThread(lldb::tid_t tid) { return target.GetProcess().GetThreadByID(tid); }

lldb::SBThread DebugService::GetLLDBThread(const llvm::json::Object &arguments) {
    auto tid = GetInteger<int64_t>(arguments, "threadId").value_or(LLDB_INVALID_THREAD_ID);
    return target.GetProcess().GetThreadByID(tid);
}

lldb::SBFrame DebugService::GetLLDBFrame(uint64_t frame_id) {
    if (frame_id == LLDB_DAP_INVALID_FRAME_ID) {
        return lldb::SBFrame();
    }
    lldb::SBProcess process = target.GetProcess();
    lldb::SBThread thread = process.GetThreadByIndexID(GetLLDBThreadIndexID(frame_id));
    return thread.GetFrameAtIndex(GetLLDBFrameID(frame_id));
}

lldb::SBFrame DebugService::GetLLDBFrame(const llvm::json::Object &arguments) {
    const auto frame_id = GetInteger<uint64_t>(arguments, "frameId").value_or(LLDB_DAP_INVALID_FRAME_ID);
    return GetLLDBFrame(frame_id);
}

ReplMode DebugService::DetectReplMode(lldb::SBFrame &frame, std::string &expression, bool partial_expression) {
    if (llvm::StringRef expr_ref = expression; expr_ref.consume_front(configuration.commandEscapePrefix)) {
        expression = expr_ref;
        return ReplMode::Command;
    }

    if (repl_mode != ReplMode::Auto) {
        return repl_mode;
    }

    const auto [first_tok, remaining] = llvm::getToken(expression);
    if (partial_expression && remaining.empty()) {
        return ReplMode::Auto;
    }

    std::string first = first_tok.str();
    const char *first_cstr = first.c_str();
    lldb::SBCommandInterpreter interpreter = debugger.GetCommandInterpreter();
    const bool is_command =
        interpreter.CommandExists(first_cstr) || interpreter.UserCommandExists(first_cstr) || interpreter.AliasExists(first_cstr);
    const bool is_variable = frame.FindVariable(first_cstr).IsValid();

    if (!partial_expression && is_command && is_variable) {
        const std::string warning_msg =
            llvm::formatv("warning: Expression '{}' is both an LLDB command and "
                          "variable. It will be evaluated as a variable. To evaluate "
                          "the expression as an LLDB command, use '{}' as a prefix.\n",
                          first, configuration.commandEscapePrefix);
        SendOutput(OutputType::Console, warning_msg);
    }

    if (is_variable) {
        return ReplMode::Variable;
    }
    return is_command ? ReplMode::Command : ReplMode::Variable;
}

std::optional<protocol::Source> DebugService::ResolveSource(const lldb::SBFrame &frame) {
    if (!frame.IsValid()) {
        return std::nullopt;
    }
    const lldb::SBLineEntry frame_line_entry = frame.GetLineEntry();
    if (DisplayAssemblySource(debugger, frame_line_entry)) {
        return ResolveAssemblySource(frame.GetPCAddress());
    }
    return CreateSource(frame_line_entry.GetFileSpec());
}

std::optional<protocol::Source> DebugService::ResolveSource(lldb::SBAddress address) {
    lldb::SBLineEntry line_entry = GetLineEntryForAddress(target, address);
    if (DisplayAssemblySource(debugger, line_entry)) {
        return ResolveAssemblySource(address);
    }
    if (!line_entry.IsValid()) {
        return std::nullopt;
    }
    return CreateSource(line_entry.GetFileSpec());
}

std::optional<protocol::Source> DebugService::ResolveAssemblySource(lldb::SBAddress address) {
    lldb::SBSymbol symbol = address.GetSymbol();
    lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
    std::string name;
    if (symbol.IsValid()) {
        load_addr = symbol.GetStartAddress().GetLoadAddress(target);
        name = symbol.GetName();
    } else {
        load_addr = address.GetLoadAddress(target);
        name = GetLoadAddressString(load_addr);
    }

    if (load_addr == LLDB_INVALID_ADDRESS) {
        return std::nullopt;
    }

    protocol::Source source;
    source.sourceReference = CreateSourceReference(load_addr);
    lldb::SBModule module = address.GetModule();
    if (module.IsValid()) {
        lldb::SBFileSpec file_spec = module.GetFileSpec();
        if (file_spec.IsValid()) {
            std::string path = GetSBFileSpecPath(file_spec);
            if (!path.empty()) {
                source.path = path + '`' + name;
            }
        }
    }
    source.name = std::move(name);
    source.presentationHint = protocol::Source::eSourcePresentationHintDeemphasize;
    return source;
}

bool DebugService::RunLLDBCommands(llvm::StringRef prefix, llvm::ArrayRef<std::string> commands) {
    bool required_command_failed = false;
    std::string output =
        ::dap::debug_service::RunLLDBCommands(debugger, prefix, commands, required_command_failed,
                                               /*parse_command_directives=*/true, /*echo_commands=*/true);
    SendOutput(OutputType::Console, output);
    return !required_command_failed;
}

namespace {
llvm::Error CreateRunLLDBCommandsErrorMessage(llvm::StringRef category) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        llvm::formatv("Failed to run {0} commands. See the Debug Console for more details.", category).str());
}
}  // namespace

llvm::Error DebugService::RunAttachCommands(llvm::ArrayRef<std::string> attach_commands) {
    if (!RunLLDBCommands("Running attachCommands:", attach_commands)) {
        return CreateRunLLDBCommandsErrorMessage("attach");
    }
    return llvm::Error::success();
}

llvm::Error DebugService::RunLaunchCommands(llvm::ArrayRef<std::string> launch_commands) {
    if (!RunLLDBCommands("Running launchCommands:", launch_commands)) {
        return CreateRunLLDBCommandsErrorMessage("launch");
    }
    return llvm::Error::success();
}

llvm::Error DebugService::RunInitCommands() {
    if (!RunLLDBCommands("Running initCommands:", configuration.initCommands)) {
        return CreateRunLLDBCommandsErrorMessage("initCommands");
    }
    return llvm::Error::success();
}

llvm::Error DebugService::RunPreInitCommands() {
    if (!RunLLDBCommands("Running preInitCommands:", configuration.preInitCommands)) {
        return CreateRunLLDBCommandsErrorMessage("preInitCommands");
    }
    return llvm::Error::success();
}

llvm::Error DebugService::RunPreRunCommands() {
    if (!RunLLDBCommands("Running preRunCommands:", configuration.preRunCommands)) {
        return CreateRunLLDBCommandsErrorMessage("preRunCommands");
    }
    return llvm::Error::success();
}

void DebugService::RunPostRunCommands() { RunLLDBCommands("Running postRunCommands:", configuration.postRunCommands); }
void DebugService::RunStopCommands() { RunLLDBCommands("Running stopCommands:", configuration.stopCommands); }
void DebugService::RunExitCommands() { RunLLDBCommands("Running exitCommands:", configuration.exitCommands); }
void DebugService::RunTerminateCommands() {
    RunLLDBCommands("Running terminateCommands:", configuration.terminateCommands);
}

lldb::SBTarget DebugService::CreateTarget(lldb::SBError &error) {
    return debugger.CreateTarget(
        /*filename=*/configuration.program.data(),
        /*target_triple=*/configuration.targetTriple.data(),
        /*platform_name=*/configuration.platformName.data(),
        /*add_dependent_modules=*/true, error);
}

void DebugService::SetTarget(lldb::SBTarget target) {
    this->target = target;
    if (target.IsValid()) {
        lldb::SBListener listener = this->debugger.GetListener();
        listener.StartListeningForEvents(this->target.GetBroadcaster(),
                                          lldb::SBTarget::eBroadcastBitBreakpointChanged |
                                              lldb::SBTarget::eBroadcastBitModulesLoaded |
                                              lldb::SBTarget::eBroadcastBitModulesUnloaded |
                                              lldb::SBTarget::eBroadcastBitSymbolsLoaded |
                                              lldb::SBTarget::eBroadcastBitSymbolsChanged |
                                              lldb::SBTarget::eBroadcastBitNewTargetCreated);
    }
}

std::vector<protocol::Breakpoint> DebugService::SetSourceBreakpoints(
    const protocol::Source &source,
    const std::optional<std::vector<protocol::SourceBreakpoint>> &breakpoints) {
    std::vector<protocol::Breakpoint> response_breakpoints;
    if (source.sourceReference) {
        // Breakpoint set by assembly source.
        auto &existing_breakpoints = m_source_assembly_breakpoints[*source.sourceReference];
        response_breakpoints = SetSourceBreakpoints(source, breakpoints, existing_breakpoints);
    } else {
        // Breakpoint set by a regular source file.
        const auto path = source.path.value_or("");
        auto &existing_breakpoints = m_source_breakpoints[path];
        response_breakpoints = SetSourceBreakpoints(source, breakpoints, existing_breakpoints);
    }

    return response_breakpoints;
}

std::vector<protocol::Breakpoint> DebugService::SetSourceBreakpoints(
    const protocol::Source &source,
    const std::optional<std::vector<protocol::SourceBreakpoint>> &breakpoints,
    SourceBreakpointMap &existing_breakpoints) {
    std::vector<protocol::Breakpoint> response_breakpoints;

    SourceBreakpointMap request_breakpoints;
    if (breakpoints) {
        for (const auto &bp : *breakpoints) {
            SourceBreakpoint src_bp(*this, bp);
            std::pair<uint32_t, uint32_t> bp_pos(src_bp.GetLine(), src_bp.GetColumn());
            request_breakpoints.try_emplace(bp_pos, src_bp);

            const auto [iv, inserted] = existing_breakpoints.try_emplace(bp_pos, src_bp);
            // We check if this breakpoint already exists to update it.
            if (inserted) {
                if (llvm::Error error = iv->second.SetBreakpoint(source)) {
                    protocol::Breakpoint invalid_breakpoint;
                    invalid_breakpoint.message = llvm::toString(std::move(error));
                    invalid_breakpoint.verified = false;
                    response_breakpoints.push_back(std::move(invalid_breakpoint));
                    existing_breakpoints.erase(iv);
                    continue;
                }
            } else {
                iv->second.UpdateBreakpoint(src_bp);
            }

            protocol::Breakpoint response_breakpoint = iv->second.ToProtocolBreakpoint();

            if (!response_breakpoint.source)
                response_breakpoint.source = source;
            if (!response_breakpoint.line && src_bp.GetLine() != LLDB_INVALID_LINE_NUMBER)
                response_breakpoint.line = src_bp.GetLine();
            if (!response_breakpoint.column && src_bp.GetColumn() != LLDB_INVALID_COLUMN_NUMBER)
                response_breakpoint.column = src_bp.GetColumn();
            response_breakpoints.push_back(std::move(response_breakpoint));
        }
    }

    // Delete any breakpoints in this source file that aren't in the
    // request_bps set. There is no call to remove breakpoints other than
    // calling this function with a smaller or empty "breakpoints" list.
    for (auto it = existing_breakpoints.begin(); it != existing_breakpoints.end();) {
        auto request_pos = request_breakpoints.find(it->first);
        if (request_pos == request_breakpoints.end()) {
            // This breakpoint no longer exists in this source file, delete it
            target.BreakpointDelete(it->second.GetID());
            it = existing_breakpoints.erase(it);
        } else {
            ++it;
        }
    }

    return response_breakpoints;
}

void DebugService::SendTerminatedEvent() {
    llvm::call_once(terminated_event_flag, [&] {
        RunTerminateCommands();
        llvm::json::Object event(CreateTerminatedEventObject(target));
        SendJSON(llvm::json::Value(std::move(event)));
    });
}

llvm::Error DebugService::Disconnect() { return Disconnect(!is_attach); }

llvm::Error DebugService::Disconnect(bool terminateDebuggee) {
    lldb::SBError error;
    lldb::SBProcess process = target.GetProcess();
    switch (process.GetState()) {
        case lldb::eStateInvalid:
        case lldb::eStateUnloaded:
        case lldb::eStateDetached:
        case lldb::eStateExited:
            break;
        case lldb::eStateConnected:
        case lldb::eStateAttaching:
        case lldb::eStateLaunching:
        case lldb::eStateStepping:
        case lldb::eStateCrashed:
        case lldb::eStateSuspended:
        case lldb::eStateStopped:
        case lldb::eStateRunning: {
            ScopeSyncMode scope_sync_mode(debugger);
            error = terminateDebuggee ? process.Kill() : process.Detach();
            break;
        }
    }

    SendTerminatedEvent();
    // Stop the event thread before the Orchestrator's write loop tears down
    // - otherwise it could still be calling SendMessage() on a Transport
    // that's mid-shutdown.
    StopEventHandlers();
    m_orchestrator.RequestStop();
    return ToError(error);
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

lldb::SBError DebugService::WaitForProcessToStop(std::chrono::seconds seconds) {
    lldb::SBError error;
    lldb::SBProcess process = target.GetProcess();
    if (!process.IsValid()) {
        error.SetErrorString("invalid process");
        return error;
    }
    auto timeout_time = std::chrono::steady_clock::now() + seconds;
    while (std::chrono::steady_clock::now() < timeout_time) {
        switch (process.GetState()) {
            case lldb::eStateUnloaded:
            case lldb::eStateAttaching:
            case lldb::eStateConnected:
            case lldb::eStateInvalid:
            case lldb::eStateLaunching:
            case lldb::eStateRunning:
            case lldb::eStateStepping:
            case lldb::eStateSuspended:
                break;
            case lldb::eStateDetached:
                error.SetErrorString("process detached during launch or attach");
                return error;
            case lldb::eStateExited:
                error.SetErrorString("process exited during launch or attach");
                return error;
            case lldb::eStateCrashed:
            case lldb::eStateStopped:
                return lldb::SBError();  // Success!
        }
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    error.SetErrorString(llvm::formatv("process failed to stop within {0}", seconds).str().c_str());
    return error;
}

void DebugService::ConfigureSourceMaps() {
    if (configuration.sourceMap.empty() && configuration.sourcePath.empty()) {
        return;
    }

    std::string sourceMapCommand;
    llvm::raw_string_ostream strm(sourceMapCommand);
    strm << "settings set target.source-map ";
    if (!configuration.sourceMap.empty()) {
        for (const auto &kv : configuration.sourceMap) {
            strm << "\"" << kv.first << "\" \"" << kv.second << "\" ";
        }
    } else if (!configuration.sourcePath.empty()) {
        strm << "\".\" \"" << configuration.sourcePath << "\"";
    }
    RunLLDBCommands("Setting source map:", {sourceMapCommand});
}

void DebugService::SetConfiguration(const protocol::Configuration &config, bool is_attach) {
    configuration = config;
    stop_at_entry = config.stopOnEntry;
    this->is_attach = is_attach;

    if (configuration.customFrameFormat) {
        SetFrameFormat(*configuration.customFrameFormat);
    }
    if (configuration.customThreadFormat) {
        SetThreadFormat(*configuration.customThreadFormat);
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

    for (auto &kv : request_handlers) {
        llvm::SmallDenseSet<AdapterFeature, 1> features = kv.second->GetSupportedFeatures();
        capabilities.supportedFeatures.insert(features.begin(), features.end());
    }

    PopulateExceptionBreakpoints();
    std::vector<protocol::ExceptionBreakpointsFilter> filters;
    for (const auto &exc_bp : exception_breakpoints) {
        filters.emplace_back(CreateExceptionBreakpointFilter(exc_bp));
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
    for (auto &kv : request_handlers) {
        for (auto &feature : kv.second->GetSupportedFeatures()) {
            if (all_custom_features.contains(feature)) {
                capabilities.supportedFeatures.insert(feature);
            }
        }
    }
    return capabilities;
}

void DebugService::StartEventThread() {
    // std::ref: EventThread takes DebugService& - std::thread otherwise
    // decay-copies its arguments, which would try (and fail, it's
    // non-copyable) to copy *this into thread-local storage instead of
    // referencing it.
    m_event_thread = std::thread(EventThread, std::ref(*this));
}

void DebugService::StopEventHandlers() {
    if (m_event_thread.joinable()) {
        broadcaster.BroadcastEventByType(eBroadcastBitStopEventThread);
        m_event_thread.join();
    }
}

void DebugService::StartEventThreads() { StartEventThread(); }

llvm::Error DebugService::InitializeDebugger() {
    debugger = lldb::SBDebugger::Create(/*source_init_files=*/false);

    // Route LLDB's own command-interpreter output to stderr, never stdout -
    // stdout is the DAP channel (see dap_main.cc). Forwarding LLDB's output
    // through as DAP `output` events (like upstream's OutputRedirector does)
    // isn't ported yet.
    debugger.SetOutputFile(lldb::SBFile(stderr, /*transfer_ownership=*/false));
    debugger.SetErrorFile(lldb::SBFile(stderr, /*transfer_ownership=*/false));

    // Disable LLDB's built-in debuginfod client: CreateTarget() otherwise
    // blocks (observed: minutes, not seconds) on an outbound HTTPS lookup
    // to a debuginfod server when a target's debug info is incomplete,
    // which real embedded firmware images with partial/no debug info hit
    // constantly, and which isn't reachable in every network environment
    // this adapter runs in anyway.
    {
        lldb::SBCommandReturnObject result;
        debugger.GetCommandInterpreter().HandleCommand("settings set symbols.enable-external-lookup false", result);
    }

    target = debugger.GetDummyTarget();

    const bool should_source_init_files = !no_lldbinit && sourceInitFile;
    if (should_source_init_files) {
        debugger.SkipLLDBInitFiles(false);
        debugger.SkipAppInitFiles(false);
        lldb::SBCommandReturnObject init;
        auto interp = debugger.GetCommandInterpreter();
        interp.SourceInitFileInGlobalDirectory(init);
        interp.SourceInitFileInHomeDirectory(init);
    }

    if (llvm::Error err = RunPreInitCommands()) {
        return err;
    }

    StartEventThreads();
    return llvm::Error::success();
}

void DebugService::RegisterRequests() {
    RegisterRequest<InitializeRequestHandler>();
    RegisterRequest<AttachRequestHandler>();
    RegisterRequest<ConfigurationDoneRequestHandler>();
    RegisterRequest<ThreadsRequestHandler>();
    RegisterRequest<StackTraceRequestHandler>();
    RegisterRequest<ContinueRequestHandler>();
    RegisterRequest<PauseRequestHandler>();
    RegisterRequest<DisconnectRequestHandler>();

    RegisterRequest<BreakpointLocationsRequestHandler>();
    RegisterRequest<CancelRequestHandler>();
    RegisterRequest<CompileUnitsRequestHandler>();
    RegisterRequest<CompletionsRequestHandler>();
    RegisterRequest<DataBreakpointInfoRequestHandler>();
    RegisterRequest<DisassembleRequestHandler>();
    RegisterRequest<EvaluateRequestHandler>();
    RegisterRequest<ExceptionInfoRequestHandler>();
    RegisterRequest<LaunchRequestHandler>();
    RegisterRequest<LocationsRequestHandler>();
    RegisterRequest<ModulesRequestHandler>();
    RegisterRequest<ModuleSymbolsRequestHandler>();
    RegisterRequest<NextRequestHandler>();
    RegisterRequest<ReadMemoryRequestHandler>();
    RegisterRequest<RestartRequestHandler>();
    RegisterRequest<ScopesRequestHandler>();
    RegisterRequest<SetBreakpointsRequestHandler>();
    RegisterRequest<SetDataBreakpointsRequestHandler>();
    RegisterRequest<SetExceptionBreakpointsRequestHandler>();
    RegisterRequest<SetFunctionBreakpointsRequestHandler>();
    RegisterRequest<SetInstructionBreakpointsRequestHandler>();
    RegisterRequest<SetVariableRequestHandler>();
    RegisterRequest<SourceRequestHandler>();
    RegisterRequest<StepInRequestHandler>();
    RegisterRequest<StepInTargetsRequestHandler>();
    RegisterRequest<StepOutRequestHandler>();
    RegisterRequest<TestGetTargetBreakpointsRequestHandler>();
    RegisterRequest<VariablesRequestHandler>();
    RegisterRequest<WriteMemoryRequestHandler>();
}

std::unique_ptr<dap::Service> CreateDebugService(dap::Orchestrator &orchestrator) {
    return std::make_unique<DebugService>(orchestrator);
}

}  // namespace dap::debug_service
