#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_MEMORY_SELECTOR_HPP_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_MEMORY_SELECTOR_HPP_

#include <string>

namespace providers {

// Empty target_name means "whatever OpenOCD's current target is". A target
// name (e.g. "stm32h7x.ap0" vs "stm32h7x.cpu0") is required to reach memory
// behind a specific AP, since routing is per-target, not per-AP - each
// target already owns exactly one AP (see tcl/target/stm32h7rx.cfg).
struct MemorySelector {
    std::string target_name;
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_MEMORY_SELECTOR_HPP_
