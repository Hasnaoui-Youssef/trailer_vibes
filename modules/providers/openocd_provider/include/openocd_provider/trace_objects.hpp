#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_TRACE_OBJECTS_HPP_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_TRACE_OBJECTS_HPP_

#include <cstdint>
#include <string>

namespace providers {

enum class TmcMode { kCircular, kSwFifo, kHwFifo };
enum class TmcState { kDisabled, kStopped, kDisabling, kStopping, kRunning };

struct TmcObject {
    std::string name;
    bool initialised = false;
    TmcMode mode = TmcMode::kCircular;
    TmcState state = TmcState::kDisabled;
    uint64_t ap_num = 0;
    uint32_t base = 0;
    uint32_t ram_size_words = 0;
};

struct Etmv4Object {
    std::string name;
    bool initialised = false;
    bool enabled = false;
    bool trace_requested = false;
    uint64_t ap_num = 0;
    uint32_t base = 0;
    uint32_t traceid = 0;
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_TRACE_OBJECTS_HPP_
