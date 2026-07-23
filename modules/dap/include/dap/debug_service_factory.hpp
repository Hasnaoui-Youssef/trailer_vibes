#ifndef TRAILER_DAP_DEBUG_SERVICE_FACTORY_HPP_
#define TRAILER_DAP_DEBUG_SERVICE_FACTORY_HPP_

#include <memory>

#include "dap/service.hpp"

namespace dap {
class Orchestrator;
}  // namespace dap

namespace dap::debug_service {

// Constructs the forked lldb-dap surface as a dap::Service, ready to
// register with an Orchestrator. The concrete DebugService type (and every
// LLDB SB API member it owns) stays private to this module's src/ - callers
// only ever see it through the Service interface.
std::unique_ptr<dap::Service> CreateDebugService(dap::Orchestrator &orchestrator);

}  // namespace dap::debug_service

#endif  // TRAILER_DAP_DEBUG_SERVICE_FACTORY_HPP_
