#include "openocd_provider/openocd_provider.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

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

// AP0 (mem_ap) and AP1 (cpu0) turned out to present genuinely different
// views of "the same" address on this chip - confirmed independently via
// OpenOCD's own trusted 'mdw' Tcl command, not just this provider's
// target_read_buffer path. So the real, falsifiable property AP-scoped
// routing has to prove isn't "both APs agree" (they don't) - it's that the
// selector actually reaches a distinct, stable, AP-specific view rather
// than being a no-op that always reads through whatever the current target
// happens to be.
int TestApSelection(providers::OpenOcdProvider& provider) {
    constexpr uint64_t kAddress = 0x24000000;  // AXI SRAM.
    constexpr uint32_t kSize = 64;

    auto via_ap0 = provider.ReadMemory({.target_name = "stm32h7x.ap0"}, kAddress, kSize);
    if (!via_ap0) return Fail("ReadMemory via stm32h7x.ap0: " + via_ap0.error());

    auto via_cpu0 = provider.ReadMemory({.target_name = "stm32h7x.cpu0"}, kAddress, kSize);
    if (!via_cpu0) return Fail("ReadMemory via stm32h7x.cpu0: " + via_cpu0.error());

    if (via_ap0->size() != kSize || via_cpu0->size() != kSize) return Fail("short read");

    if (*via_ap0 == *via_cpu0) {
        DumpMismatch("ap0", *via_ap0, "cpu0", *via_cpu0);
        return Fail("stm32h7x.ap0 and stm32h7x.cpu0 unexpectedly agree - AP selection may be a no-op");
    }

    // The config selects stm32h7x.cpu0 as the current target ('targets
    // $_CHIPNAME.cpu0'), so an empty selector should resolve to exactly the
    // same view as the explicit cpu0 read above.
    auto via_default = provider.ReadMemory({}, kAddress, kSize);
    if (!via_default) return Fail("ReadMemory via the default target: " + via_default.error());
    if (*via_default != *via_cpu0) {
        DumpMismatch("default", *via_default, "cpu0", *via_cpu0);
        return Fail("default-target read does not match the explicit stm32h7x.cpu0 read");
    }

    // Stability: a second read through the same AP must return the same
    // bytes, ruling out "ap0 just returns whatever garbage was on the bus".
    auto via_ap0_again = provider.ReadMemory({.target_name = "stm32h7x.ap0"}, kAddress, kSize);
    if (!via_ap0_again) return Fail("second ReadMemory via stm32h7x.ap0: " + via_ap0_again.error());
    if (*via_ap0_again != *via_ap0) {
        DumpMismatch("ap0 (1st)", *via_ap0, "ap0 (2nd)", *via_ap0_again);
        return Fail("stm32h7x.ap0 is not stable across two consecutive reads");
    }

    auto via_bogus = provider.ReadMemory({.target_name = "no_such_target"}, kAddress, kSize);
    if (via_bogus) return Fail("ReadMemory via a nonexistent target name should have failed");

    std::fprintf(stderr,
                 "PASS: stm32h7x.ap0 and stm32h7x.cpu0 give distinct, stable, AP-specific views of "
                 "0x%llx; the default selector matches the current target; an unresolvable target "
                 "name is correctly rejected\n",
                 static_cast<unsigned long long>(kAddress));
    return 0;
}

int TestCoreNameAndRegisters(providers::OpenOcdProvider& provider) {
    auto core_name = provider.GetCoreName();
    if (!core_name) return Fail("GetCoreName: " + core_name.error());
    if (*core_name != "Cortex-M7") return Fail("GetCoreName: expected 'Cortex-M7', got '" + *core_name + "'");

    std::ifstream json_file(ETM_REGS_JSON_PATH);
    if (!json_file) return Fail("cannot open " ETM_REGS_JSON_PATH);
    nlohmann::json expected;
    json_file >> expected;

    auto regs = provider.ReadETMv4Registers("stm32h7x.etm");
    if (!regs) return Fail("ReadETMv4Registers: " + regs.error());

    bool ok = true;
    auto check = [&](const char* field, uint32_t actual) {
        const uint32_t exp = expected.at(field).get<uint32_t>();
        if (exp != actual) {
            std::fprintf(stderr, "FAIL: %s mismatch vs test_resources/etm_regs.json: expected 0x%x, got 0x%x\n",
                         field, exp, actual);
            ok = false;
        }
    };
    check("TRCCONFIGR", regs->trcconfigr);
    check("TRCTRACEIDR", regs->trctraceidr);
    check("TRCIDR0", regs->trcidr0);
    check("TRCIDR1", regs->trcidr1);
    check("TRCIDR2", regs->trcidr2);
    check("TRCIDR3", regs->trcidr3);
    check("TRCIDR4", regs->trcidr4);
    check("TRCIDR5", regs->trcidr5);
    check("TRCIDR6", regs->trcidr6);
    check("TRCIDR7", regs->trcidr7);
    check("TRCIDR8", regs->trcidr8);
    check("TRCIDR9", regs->trcidr9);
    check("TRCIDR10", regs->trcidr10);
    check("TRCIDR11", regs->trcidr11);
    check("TRCIDR12", regs->trcidr12);
    check("TRCIDR13", regs->trcidr13);
    check("TRCAUTHSTATUS", regs->trcauthstatus);
    if (!ok) return Fail("ETMv4 register mismatch");

    auto missing = provider.ReadETMv4Registers("no_such_source");
    if (missing) return Fail("ReadETMv4Registers on a nonexistent source should have failed");

    std::fprintf(stderr, "PASS: GetCoreName() == 'Cortex-M7', ReadETMv4Registers matches the known-good "
                          "oracle, an unresolvable source name is correctly rejected\n");
    return 0;
}

const uint8_t kFsyncBarrier[16] = {
    0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F,
};

int TestTraceCapture(providers::OpenOcdProvider& provider) {
    std::mutex mutex;
    std::vector<std::pair<bool, std::vector<std::byte>>> events;

    auto subscribed = provider.SubscribeTrace("stm32h7x.etf", [&](std::span<const std::byte> data, bool is_barrier) {
        std::lock_guard<std::mutex> lock(mutex);
        events.emplace_back(is_barrier, std::vector<std::byte>(data.begin(), data.end()));
    });
    if (!subscribed) return Fail("SubscribeTrace: " + subscribed.error());

    if (auto enable_sink = provider.EnableTrace("stm32h7x.etf"); !enable_sink)
        return Fail("EnableTrace(sink): " + enable_sink.error());
    if (auto enable_source = provider.EnableTrace("stm32h7x.etm"); !enable_source)
        return Fail("EnableTrace(source): " + enable_source.error());

    if (auto resume = provider.RunTclCommand("resume"); !resume) return Fail("resume: " + resume.error());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (auto halt = provider.RunTclCommand("halt"); !halt) return Fail("halt: " + halt.error());

    if (auto disable_source = provider.DisableTrace("stm32h7x.etm"); !disable_source)
        return Fail("DisableTrace(source): " + disable_source.error());
    if (auto disable_sink = provider.DisableTrace("stm32h7x.etf"); !disable_sink)
        return Fail("DisableTrace(sink): " + disable_sink.error());

    auto unsubscribed = provider.UnsubscribeTrace("stm32h7x.etf");
    if (!unsubscribed) return Fail("UnsubscribeTrace: " + unsubscribed.error());

    std::lock_guard<std::mutex> lock(mutex);
    if (events.size() != 2) return Fail("expected exactly 2 callback invocations, got " + std::to_string(events.size()));

    const auto& [barrier_flag, barrier_data] = events[0];
    if (!barrier_flag) return Fail("first callback invocation should have is_barrier == true");
    if (barrier_data.size() != sizeof(kFsyncBarrier) ||
        std::memcmp(barrier_data.data(), kFsyncBarrier, sizeof(kFsyncBarrier)) != 0) {
        return Fail("first callback's data does not match the fsync barrier pattern");
    }

    const auto& [data_flag, data_bytes] = events[1];
    if (data_flag) return Fail("second callback invocation should have is_barrier == false");
    if (data_bytes.empty()) return Fail("second callback's capture data is empty");

    std::fprintf(stderr,
                 "PASS: trace capture callback fired exactly twice per halt - fsync barrier (16 bytes) "
                 "then %zu byte(s) of capture data\n",
                 data_bytes.size());
    return 0;
}

}  // namespace

int main() {
    providers::OpenOcdConfig config;
    config.script_search_dirs.push_back(OPENOCD_TCL_DIR);
    config.config_files.push_back(OPENOCD_BOARD_CFG);
    config.init_at_startup = true;

    auto provider = providers::OpenOcdProvider::Create(config);
    if (!provider) return Fail("Create() failed: " + provider.error());

    auto reset_halt = provider->RunTclCommand("reset halt");
    if (!reset_halt) return Fail("reset halt: " + reset_halt.error());

    if (int rc = TestApSelection(*provider); rc != 0) return rc;
    if (int rc = TestCoreNameAndRegisters(*provider); rc != 0) return rc;
    if (int rc = TestTraceCapture(*provider); rc != 0) return rc;

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
