#ifndef TRAILER_DAP_DEBUG_SERVICE_FACTORY_HPP_
#define TRAILER_DAP_DEBUG_SERVICE_FACTORY_HPP_

#include <memory>

namespace dap {
class Orchestrator;
}  // namespace dap

namespace dap {

// Opaque RAII owner of the DebugService instance and every request handler
// registered on its behalf (see dap::Orchestrator::RegisterHandler) -
// constructing one wires everything up; destroying it tears everything
// down. The concrete DebugService type (and every LLDB SB API member it
// owns) stays private to this module's src/ - callers only ever see this
// opaque handle, never DebugService itself.
class DebugServiceSession {
public:
    virtual ~DebugServiceSession() = default;
};

// Constructs a DebugService bound to `orchestrator`, builds every one of its
// request handlers, and registers each with `orchestrator` by command name.
// `orchestrator` must outlive the returned session.
std::unique_ptr<DebugServiceSession> CreateDebugServiceSession(dap::Orchestrator &orchestrator);

}  // namespace dap

#endif  // TRAILER_DAP_DEBUG_SERVICE_FACTORY_HPP_
