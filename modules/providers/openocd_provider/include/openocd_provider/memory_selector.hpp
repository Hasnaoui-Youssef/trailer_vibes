#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_MEMORY_SELECTOR_HPP_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_MEMORY_SELECTOR_HPP_

#include <string>

namespace providers {

// Empty target_name means OpenOCD's current target.
struct MemorySelector {
    std::string target_name;
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_MEMORY_SELECTOR_HPP_
