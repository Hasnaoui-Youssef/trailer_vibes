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

#include "core/components/breakpoint_manager.hpp"
#include "core/components/data_manager.hpp"
#include "core/debug_context.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "debug_service/dap_forward.hpp"
#include "dap/dap_log.hpp"
#include "lldb_provider/lldb_provider.hpp"
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
#include "llvm/Support/JSON.h"
#include "llvm/Support/Threading.h"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#define NO_TYPENAME "<no-type>"

namespace dap {

using AdapterFeature = protocol::AdapterFeature;
using ClientFeature = protocol::ClientFeature;

// ReplMode moved to core/components/data_manager.hpp with DataManager (see
// PROJECT_STATUS.md's Data carve) - re-exported here so existing callers
// keep working unqualified.
using core::ReplMode;

enum class OutputType { Console, Important, Stdout, Stderr, Telemetry };

constexpr uint64_t OutputBufferSize = (1u << 12);

// The forked lldb-dap surface: owns the LLDB SB API state (SBDebugger,
// SBTarget, breakpoint/thread bookkeeping) that the ~40 (still growing)
// request handlers operate on. DebugService itself owns no dispatch table -
// each handler registers itself with the Orchestrator directly (see
// handlers/register_handlers.hpp) - so this class is purely the debug
// session's state and logic, not a dap::Service.
class DebugService final {
public:
    friend class BaseRequestHandler;

    Log log;

    /// The debugger instance for this session. Owned by the LldbProvider
    /// passed in at construction (see m_lldb_provider) - a reference alias
    /// here so every existing `dap.debugger`/bare `debugger` call site
    /// keeps working unchanged.
    lldb::SBDebugger &debugger;

    /// The target instance for this session. Same aliasing as `debugger`.
    lldb::SBTarget &target;

    llvm::once_flag terminated_event_flag;

    std::mutex call_mutex;
    llvm::SmallDenseMap<int64_t, std::unique_ptr<ResponseHandler>> inflight_reverse_requests;
    lldb::SBFormat frame_format;
    lldb::SBFormat thread_format;
    llvm::unique_function<void()> on_configuration_done;

    /// The set of features supported by the connected client.
    llvm::DenseSet<ClientFeature> clientFeatures;

    /// The initial thread list upon attaching.
    std::vector<protocol::Thread> initial_thread_list;

    DebugService(Orchestrator &orchestrator, providers::LldbProvider &lldb_provider,
                 core::DebugContext &context);

    ~DebugService();

    DebugService(const DebugService &) = delete;
    void operator=(const DebugService &) = delete;

    /// Assigns the next global seq number (via the Orchestrator) and writes
    /// the message through it - the Orchestrator remains the sole owner of
    /// the wire.
    void SendJSON(const llvm::json::Value &json);
    protocol::Id Send(const protocol::Message &message);

    void SendOutput(OutputType o, llvm::StringRef output);
    void __attribute__((format(printf, 3, 4))) SendFormattedOutput(OutputType o, const char *format, ...);

    void SendTerminatedEvent();

    template <typename Handler>
    void SendReverseRequest(llvm::StringRef command, llvm::json::Value arguments) {
        protocol::Id id = Send(protocol::Request{command.str(), std::move(arguments)});
        std::lock_guard<std::mutex> locker(call_mutex);
        inflight_reverse_requests[id] = std::make_unique<Handler>(command, id);
    }

    protocol::Capabilities GetCapabilities();
    protocol::Capabilities GetCustomCapabilities();

    void SetFrameFormat(llvm::StringRef format);
    void SetThreadFormat(llvm::StringRef format);

    bool IsCancelled(const protocol::Request &);
    void ClearCancelRequest(const protocol::CancelArguments &);

    lldb::SBMutex GetAPIMutex() const { return target.GetAPIMutex(); }

    llvm::StringRef GetClientName() const { return m_client_name; }

    /// Access to the already-carved components (see PROJECT_STATUS.md) for
    /// the DebugService call sites that still need to reach across into
    /// them (e.g. capabilities assembly, stop-reason resolution).
    core::DebugContext &Context() { return m_context; }

private:
    Orchestrator &m_orchestrator;
    providers::LldbProvider &m_lldb_provider;
    core::DebugContext &m_context;
    llvm::StringRef m_client_name = "trailer-dap";

    std::mutex m_cancelled_requests_mutex;
    llvm::SmallSet<int64_t, 4> m_cancelled_requests;

    std::mutex m_active_request_mutex;
    const protocol::Request *m_active_request = nullptr;
};

}  // namespace dap

#endif  // TRAILER_DAP_DEBUG_SERVICE_DEBUG_SERVICE_HPP_
