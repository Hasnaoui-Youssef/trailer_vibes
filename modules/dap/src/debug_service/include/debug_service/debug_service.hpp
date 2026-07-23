//===-- debug_service.hpp -------------------------------------------------===//
//
// Forked from LLVM's lldb-dap DAP.h (Apache-2.0 WITH LLVM-exception) - see
// project-dap-layer-fork-strategy memory.
// The MainLoop/DAPTransport/message-queue machinery is dropped entirely -
// superseded by the Orchestrator, which already owns the read loop and
// Transport (see orchestrator.hpp). Send()/SendJSON() are adapted to route
// through the Orchestrator instead of a directly-owned transport.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_DAP_DEBUG_SERVICE_DEBUG_SERVICE_HPP_
#define TRAILER_DAP_DEBUG_SERVICE_DEBUG_SERVICE_HPP_

#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "dap/service.hpp"
#include "debug_service/dap_forward.hpp"
#include "debug_service/dap_log.hpp"
#include "debug_service/exception_breakpoint.hpp"
#include "debug_service/function_breakpoint.hpp"
#include "debug_service/instruction_breakpoint.hpp"
#include "debug_service/source_breakpoint.hpp"
#include "debug_service/variables.hpp"
#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFormat.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBMutex.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Threading.h"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#define NO_TYPENAME "<no-type>"

namespace dap::debug_service {

using SourceBreakpointMap = std::map<std::pair<uint32_t, uint32_t>, SourceBreakpoint>;
using FunctionBreakpointMap = llvm::StringMap<FunctionBreakpoint>;
using InstructionBreakpointMap = llvm::DenseMap<lldb::addr_t, InstructionBreakpoint>;

using AdapterFeature = protocol::AdapterFeature;
using ClientFeature = protocol::ClientFeature;

enum class OutputType { Console, Important, Stdout, Stderr, Telemetry };

constexpr uint64_t OutputBufferSize = (1u << 12);

enum DAPBroadcasterBits {
    eBroadcastBitStopEventThread = 1u << 0,
};

enum class ReplMode { Variable = 0, Command, Auto };

// The forked lldb-dap surface: owns the LLDB SB API state (SBDebugger,
// SBTarget, breakpoint/thread bookkeeping) and the ~40 (still growing)
// request handlers that operate on it. Implements dap::Service so the
// Orchestrator can route DAP requests to it without knowing anything about
// LLDB.
class DebugService final : public dap::Service {
public:
    friend class BaseRequestHandler;

    Log log;

    /// Configuration specified by the launch or attach commands.
    protocol::Configuration configuration;

    /// The debugger instance for this session.
    lldb::SBDebugger debugger;

    /// The target instance for this session.
    lldb::SBTarget target;

    lldb::SBBroadcaster broadcaster;
    Variables variables;
    FunctionBreakpointMap function_breakpoints;
    InstructionBreakpointMap instruction_breakpoints;
    std::vector<ExceptionBreakpoint> exception_breakpoints;
    llvm::once_flag init_exception_breakpoints_flag;

    /// Map step in target id to list of function targets that user can choose.
    llvm::DenseMap<lldb::addr_t, std::string> step_in_targets;

    /// A copy of the last LaunchRequest so we can reuse its arguments if we
    /// get a RestartRequest. Restarting an AttachRequest is not supported.
    std::optional<protocol::LaunchRequestArguments> last_launch_request;

    /// The focused thread for this session.
    lldb::tid_t focus_tid = LLDB_INVALID_THREAD_ID;

    llvm::once_flag terminated_event_flag;
    bool stop_at_entry = false;
    bool is_attach = false;

    lldb::pid_t restarting_process_id = LLDB_INVALID_PROCESS_ID;

    /// Whether we have received the ConfigurationDone request.
    bool configuration_done = false;

    bool waiting_for_run_in_terminal = false;

    /// Keep track of the last stop thread index IDs as threads won't go away
    /// unless we send a "thread" event to indicate the thread exited.
    llvm::DenseSet<lldb::tid_t> thread_ids;

    std::mutex call_mutex;
    llvm::SmallDenseMap<int64_t, std::unique_ptr<ResponseHandler>> inflight_reverse_requests;
    ReplMode repl_mode = ReplMode::Auto;
    lldb::SBFormat frame_format;
    lldb::SBFormat thread_format;
    llvm::unique_function<void()> on_configuration_done;

    std::string last_nonempty_var_expression;

    /// The set of features supported by the connected client.
    llvm::DenseSet<ClientFeature> clientFeatures;

    bool no_lldbinit = false;
    bool sourceInitFile = true;

    /// The initial thread list upon attaching.
    std::vector<protocol::Thread> initial_thread_list;

    std::mutex modules_mutex;
    llvm::StringSet<> modules;

    static constexpr uint32_t k_number_of_assembly_lines_for_nodebug = 32;

    explicit DebugService(Orchestrator &orchestrator);

    ~DebugService() override;

    DebugService(const DebugService &) = delete;
    void operator=(const DebugService &) = delete;

    // dap::Service
    std::vector<std::string> SupportedCommands() const override;
    void HandleRequest(const protocol::Request &request) override;

    ExceptionBreakpoint *GetExceptionBreakpoint(llvm::StringRef filter);
    ExceptionBreakpoint *GetExceptionBreakpoint(lldb::break_id_t bp_id);

    /// Configures the debug adapter for launching/attaching.
    void SetConfiguration(const protocol::Configuration &config, bool is_attach);

    void ConfigureSourceMaps();

    /// Assigns the next global seq number (via the Orchestrator) and writes
    /// the message through it - the Orchestrator remains the sole owner of
    /// the wire.
    void SendJSON(const llvm::json::Value &json);
    protocol::Id Send(const protocol::Message &message);

    void SendOutput(OutputType o, llvm::StringRef output);
    void __attribute__((format(printf, 3, 4))) SendFormattedOutput(OutputType o, const char *format, ...);

    int32_t CreateSourceReference(lldb::addr_t address);
    std::optional<lldb::addr_t> GetSourceReferenceAddress(int32_t reference);

    ExceptionBreakpoint *GetExceptionBPFromStopReason(lldb::SBThread &thread);

    lldb::SBThread GetLLDBThread(lldb::tid_t id);
    lldb::SBThread GetLLDBThread(const llvm::json::Object &arguments);

    lldb::SBFrame GetLLDBFrame(uint64_t frame_id);
    lldb::SBFrame GetLLDBFrame(const llvm::json::Object &arguments);

    void PopulateExceptionBreakpoints();

    ReplMode DetectReplMode(lldb::SBFrame &frame, std::string &expression, bool partial_expression);

    std::optional<protocol::Source> ResolveSource(const lldb::SBFrame &frame);
    std::optional<protocol::Source> ResolveSource(lldb::SBAddress address);
    std::optional<protocol::Source> ResolveAssemblySource(lldb::SBAddress address);

    bool RunLLDBCommands(llvm::StringRef prefix, llvm::ArrayRef<std::string> commands);

    llvm::Error RunAttachCommands(llvm::ArrayRef<std::string> attach_commands);
    llvm::Error RunLaunchCommands(llvm::ArrayRef<std::string> launch_commands);
    llvm::Error RunPreInitCommands();
    llvm::Error RunInitCommands();
    llvm::Error RunPreRunCommands();
    void RunPostRunCommands();
    void RunStopCommands();
    void RunExitCommands();
    void RunTerminateCommands();

    lldb::SBTarget CreateTarget(lldb::SBError &error);
    void SetTarget(lldb::SBTarget target);

    llvm::Error Disconnect();
    llvm::Error Disconnect(bool terminateDebuggee);

    void SendTerminatedEvent();

    template <typename Handler>
    void SendReverseRequest(llvm::StringRef command, llvm::json::Value arguments) {
        protocol::Id id = Send(protocol::Request{command.str(), std::move(arguments)});
        std::lock_guard<std::mutex> locker(call_mutex);
        inflight_reverse_requests[id] = std::make_unique<Handler>(command, id);
    }

    protocol::Capabilities GetCapabilities();
    protocol::Capabilities GetCustomCapabilities();

    void WillContinue() { variables.Clear(); }

    lldb::SBError WaitForProcessToStop(std::chrono::seconds seconds);

    void SetFrameFormat(llvm::StringRef format);
    void SetThreadFormat(llvm::StringRef format);

    bool IsCancelled(const protocol::Request &);
    void ClearCancelRequest(const protocol::CancelArguments &);

    lldb::SBMutex GetAPIMutex() const { return target.GetAPIMutex(); }

    llvm::StringRef GetClientName() const { return m_client_name; }

    void StartEventThread();
    /// Signals the event thread to stop and joins it. Safe to call more
    /// than once (e.g. from Disconnect() and then again from the
    /// destructor) - a no-op if the thread isn't running.
    void StopEventHandlers();

    llvm::Error InitializeDebugger();
    void StartEventThreads();

    /// Sets the given protocol `breakpoints` in the given `source`, while
    /// removing any existing breakpoints in the given source if they are
    /// not in `breakpoints`. \return the breakpoints that were set.
    std::vector<protocol::Breakpoint> SetSourceBreakpoints(
        const protocol::Source &source,
        const std::optional<std::vector<protocol::SourceBreakpoint>> &breakpoints);

private:
    std::vector<protocol::Breakpoint> SetSourceBreakpoints(
        const protocol::Source &source,
        const std::optional<std::vector<protocol::SourceBreakpoint>> &breakpoints,
        SourceBreakpointMap &existing_breakpoints);

    void RegisterRequests();
    template <typename Handler>
    void RegisterRequest() {
        request_handlers[Handler::GetCommand()] = std::make_unique<Handler>(*this);
    }
    llvm::StringMap<std::unique_ptr<BaseRequestHandler>> request_handlers;

    Orchestrator &m_orchestrator;
    llvm::StringRef m_client_name = "trailer-dap";
    std::thread m_event_thread;

    std::vector<lldb::addr_t> m_source_references;
    std::mutex m_source_references_mutex;

    std::mutex m_cancelled_requests_mutex;
    llvm::SmallSet<int64_t, 4> m_cancelled_requests;

    std::mutex m_active_request_mutex;
    const protocol::Request *m_active_request = nullptr;

    llvm::StringMap<SourceBreakpointMap> m_source_breakpoints;
    llvm::DenseMap<int64_t, SourceBreakpointMap> m_source_assembly_breakpoints;
};

}  // namespace dap::debug_service

#endif  // TRAILER_DAP_DEBUG_SERVICE_DEBUG_SERVICE_HPP_
