#ifndef TRAILER_DAP_SESSION_HPP_
#define TRAILER_DAP_SESSION_HPP_

#include <memory>

namespace dap {
class Orchestrator;
}  // namespace dap

namespace dap {

class Session {
public:
    virtual ~Session() = default;
};

std::unique_ptr<Session> CreateSession(dap::Orchestrator &orchestrator);

}  // namespace dap

#endif  // TRAILER_DAP_SESSION_HPP_
