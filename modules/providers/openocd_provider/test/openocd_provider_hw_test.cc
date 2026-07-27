#include "openocd_provider/openocd_provider.hpp"

#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

namespace {

int Fail(const std::string& message) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    return 1;
}

void DumpMismatch(const char* label_a, const std::vector<std::byte>& a, const char* label_b,
                   const std::vector<std::byte>& b) {
    std::fprintf(stderr, "%s: ", label_a);
    for (auto byte : a) std::fprintf(stderr, "%02x ", static_cast<unsigned>(byte));
    std::fprintf(stderr, "\n%s: ", label_b);
    for (auto byte : b) std::fprintf(stderr, "%02x ", static_cast<unsigned>(byte));
    std::fprintf(stderr, "\n");
}

}  // namespace

// AP0 (mem_ap) and AP1 (cpu0) turned out to present genuinely different
// views of "the same" address on this chip - confirmed independently via
// OpenOCD's own trusted 'mdw' Tcl command, not just this provider's
// target_read_buffer path. So the real, falsifiable property AP-scoped
// routing has to prove isn't "both APs agree" (they don't) - it's that the
// selector actually reaches a distinct, stable, AP-specific view rather
// than being a no-op that always reads through whatever the current target
// happens to be.
int main() {
    providers::OpenOcdConfig config;
    config.script_search_dirs.push_back(OPENOCD_TCL_DIR);
    config.config_files.push_back(OPENOCD_BOARD_CFG);
    config.init_at_startup = true;

    auto provider = providers::OpenOcdProvider::Create(config);
    if (!provider) return Fail("Create() failed: " + provider.error());

    auto reset_halt = provider->RunTclCommand("reset halt");
    if (!reset_halt) return Fail("reset halt: " + reset_halt.error());

    constexpr uint64_t kAddress = 0x24000000;  // AXI SRAM.
    constexpr uint32_t kSize = 64;

    auto via_ap0 = provider->ReadMemory({.target_name = "stm32h7x.ap0"}, kAddress, kSize);
    if (!via_ap0) return Fail("ReadMemory via stm32h7x.ap0: " + via_ap0.error());

    auto via_cpu0 = provider->ReadMemory({.target_name = "stm32h7x.cpu0"}, kAddress, kSize);
    if (!via_cpu0) return Fail("ReadMemory via stm32h7x.cpu0: " + via_cpu0.error());

    if (via_ap0->size() != kSize || via_cpu0->size() != kSize) return Fail("short read");

    if (*via_ap0 == *via_cpu0) {
        DumpMismatch("ap0", *via_ap0, "cpu0", *via_cpu0);
        return Fail("stm32h7x.ap0 and stm32h7x.cpu0 unexpectedly agree - AP selection may be a no-op");
    }

    // The config selects stm32h7x.cpu0 as the current target ('targets
    // $_CHIPNAME.cpu0'), so an empty selector should resolve to exactly the
    // same view as the explicit cpu0 read above.
    auto via_default = provider->ReadMemory({}, kAddress, kSize);
    if (!via_default) return Fail("ReadMemory via the default target: " + via_default.error());
    if (*via_default != *via_cpu0) {
        DumpMismatch("default", *via_default, "cpu0", *via_cpu0);
        return Fail("default-target read does not match the explicit stm32h7x.cpu0 read");
    }

    // Stability: a second read through the same AP must return the same
    // bytes, ruling out "ap0 just returns whatever garbage was on the bus".
    auto via_ap0_again = provider->ReadMemory({.target_name = "stm32h7x.ap0"}, kAddress, kSize);
    if (!via_ap0_again) return Fail("second ReadMemory via stm32h7x.ap0: " + via_ap0_again.error());
    if (*via_ap0_again != *via_ap0) {
        DumpMismatch("ap0 (1st)", *via_ap0, "ap0 (2nd)", *via_ap0_again);
        return Fail("stm32h7x.ap0 is not stable across two consecutive reads");
    }

    auto via_bogus = provider->ReadMemory({.target_name = "no_such_target"}, kAddress, kSize);
    if (via_bogus) return Fail("ReadMemory via a nonexistent target name should have failed");

    std::fprintf(stderr,
                 "PASS: stm32h7x.ap0 and stm32h7x.cpu0 give distinct, stable, AP-specific views of "
                 "0x%llx; the default selector matches the current target; an unresolvable target "
                 "name is correctly rejected\n",
                 static_cast<unsigned long long>(kAddress));

    // Ad-hoc, not part of the pass/fail contract above: hold the session
    // open so an external GDB RSP client can be pointed at the same
    // in-process server for a manual cross-check.
    if (std::getenv("OPENOCD_PROVIDER_HW_TEST_HOLD_SECONDS")) {
        int hold_seconds = std::atoi(std::getenv("OPENOCD_PROVIDER_HW_TEST_HOLD_SECONDS"));
        std::fprintf(stderr, "holding for %d seconds for an external RSP client...\n", hold_seconds);
        std::this_thread::sleep_for(std::chrono::seconds(hold_seconds));
    }

    return 0;
}
