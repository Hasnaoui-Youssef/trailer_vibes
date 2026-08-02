#ifndef TRAILER_PROVIDERS_LLDB_PROVIDER_HPP_
#define TRAILER_PROVIDERS_LLDB_PROVIDER_HPP_

#include <mutex>
#include <type_traits>

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBMutex.h"
#include "lldb/API/SBTarget.h"

namespace providers {

class LldbProvider {
public:
    LldbProvider() = default;

    LldbProvider(const LldbProvider &) = delete;
    LldbProvider &operator=(const LldbProvider &) = delete;

    lldb::SBDebugger debugger;
    lldb::SBTarget target;

    // Every LLDB SB API call must happen inside this - it serializes against
    // ExecutionController's event thread, which touches the same
    // SBTarget/SBProcess/SBThread objects concurrently. fn takes no
    // arguments; it reaches target/debugger via its own capture. The
    // underlying mutex is recursive, so calling WithTarget again from
    // within fn is safe.
    template <typename Fn>
    auto WithTarget(Fn &&fn) -> std::invoke_result_t<Fn> {
        lldb::SBMutex lock = target.GetAPIMutex();
        std::lock_guard<lldb::SBMutex> guard(lock);
        return fn();
    }
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_LLDB_PROVIDER_HPP_
