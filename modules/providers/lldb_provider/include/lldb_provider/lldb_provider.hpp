#ifndef TRAILER_PROVIDERS_LLDB_PROVIDER_HPP_
#define TRAILER_PROVIDERS_LLDB_PROVIDER_HPP_

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

    lldb::SBMutex GetAPIMutex() const { return target.GetAPIMutex(); }
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_LLDB_PROVIDER_HPP_
