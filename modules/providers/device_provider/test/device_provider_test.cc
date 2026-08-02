#include <cstdlib>
#include <iostream>
#include <string>

#include "device_provider/device_index.hpp"
#include "device_xml/svd_loader.hpp"

#include "../src/rzone_parser.hpp"

namespace {

int g_failures = 0;

void Fail(const std::string &message) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

void TestDeviceIndex() {
    const std::optional<providers::device::DeviceIndex> index =
        providers::device::DeviceIndex::Load(TRAILER_DEVICE_INDEX_PATH);
    if (!index) {
        Fail("DeviceIndex::Load failed to open " TRAILER_DEVICE_INDEX_PATH);
        return;
    }

    const std::optional<providers::device::DeviceIndexEntry> board = index->Find("STM32H7S3L8Hx");
    if (!board) {
        Fail("STM32H7S3L8Hx not found in device index");
    } else {
        if (board->core != "Cortex-M7") Fail("STM32H7S3L8Hx core mismatch: " + board->core);
        if (board->die != "DIE485") Fail("STM32H7S3L8Hx die mismatch: " + board->die);
        if (board->ram_kb != 620) Fail("STM32H7S3L8Hx ram_kb mismatch");
        if (board->flash_kb != 64) Fail("STM32H7S3L8Hx flash_kb mismatch");
        if (board->core_count != 1) Fail("STM32H7S3L8Hx core_count mismatch");
    }

    // Positional variant selection: STM32H742A(G-I)Ix expands with #Flash ==
    // #options, so index i must pick Flash[i], not always Flash[0].
    const std::optional<providers::device::DeviceIndexEntry> g_variant = index->Find("STM32H742AGIx");
    const std::optional<providers::device::DeviceIndexEntry> i_variant = index->Find("STM32H742AIIx");
    if (!g_variant || g_variant->flash_kb != 1024) Fail("STM32H742AGIx positional flash_kb mismatch");
    if (!i_variant || i_variant->flash_kb != 2048) Fail("STM32H742AIIx positional flash_kb mismatch");

    // _DUAL discriminator: core_count must reflect the number of <Core>
    // elements, which is what picks the _DUAL Rzone file downstream.
    const std::optional<providers::device::DeviceIndexEntry> dual = index->Find("STM32H745ZITx");
    const std::optional<providers::device::DeviceIndexEntry> single = index->Find("STM32H743ZITx");
    if (!dual || dual->core_count != 2) Fail("STM32H745ZITx core_count mismatch");
    if (!single || single->core_count != 1) Fail("STM32H743ZITx core_count mismatch");
}

void TestRzoneMemoryMap() {
    const std::optional<providers::device::MemoryMap> map =
        providers::device::LoadRzoneMemoryMap(TRAILER_RZONE_DIR, "DIE485", 620, 64, 1);
    if (!map) {
        Fail("LoadRzoneMemoryMap failed for DIE485");
        return;
    }

    if (map->Regions().size() != 20) Fail("DIE485 region count mismatch: " + std::to_string(map->Regions().size()));
    if (!map->IsRam(0x24000000)) Fail("0x24000000 (AXI SRAM) should be RAM");
    if (!map->IsRom(0x08000000)) Fail("0x08000000 (Flash) should be ROM");
    if (map->IsRam(0x08000000)) Fail("0x08000000 (Flash) should not be RAM");
    if (map->Find(0xFFFF0000).has_value()) Fail("unmapped address should not resolve");

    const std::optional<providers::device::MemoryRegion> itcm = map->Find(0x00000000);
    if (!itcm || itcm->name != "RAM_ITCM") Fail("RAM_ITCM lookup mismatch");

    const std::optional<providers::device::MemoryRegion> xspi2 = map->Find(0x70000000);
    if (!xspi2 || xspi2->kind != providers::device::MemoryKind::kExternal) Fail("XSPI2 should be kExternal");
}

void TestRzoneDualSelection() {
    const std::optional<providers::device::MemoryMap> single =
        providers::device::LoadRzoneMemoryMap(TRAILER_RZONE_DIR, "DIE450", 864, 2048, 1);
    const std::optional<providers::device::MemoryMap> dual =
        providers::device::LoadRzoneMemoryMap(TRAILER_RZONE_DIR, "DIE450", 864, 2048, 2);
    if (!single || !dual) {
        Fail("LoadRzoneMemoryMap failed for DIE450 single/dual");
        return;
    }
    if (single->Regions().size() == dual->Regions().size()) {
        Fail("DIE450 single vs _DUAL region counts should differ (picked the wrong file)");
    }
}

void TestRzoneUncoveredDie() {
    const std::optional<providers::device::MemoryMap> map =
        providers::device::LoadRzoneMemoryMap(TRAILER_RZONE_DIR, "DIE999", 1, 1, 1);
    if (map.has_value()) Fail("uncovered DIE should yield no memory map");
}

void TestCoreSvdLoad() {
    const std::expected<device_xml::Device, std::string> device =
        device_xml::LoadSvd(std::string(TRAILER_CORES_DIR) + "/Cortex-M7.svd");
    if (!device) {
        Fail("Cortex-M7.svd failed to load: " + device.error());
        return;
    }
    if (device->peripherals.size() != 8) Fail("Cortex-M7 peripheral count mismatch: " + std::to_string(device->peripherals.size()));

    bool found_cache = false;
    for (const device_xml::Peripheral &peripheral : device->peripherals) {
        if (peripheral.name == "Cache") {
            found_cache = true;
            if (peripheral.base_address != 0xE000ED78) Fail("Cache base address mismatch");
        }
    }
    if (!found_cache) Fail("Cache peripheral not found in Cortex-M7.svd");
}

}  // namespace

int main() {
    TestDeviceIndex();
    TestRzoneMemoryMap();
    TestRzoneDualSelection();
    TestRzoneUncoveredDie();
    TestCoreSvdLoad();

    if (g_failures > 0) {
        std::cerr << g_failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "all checks passed\n";
    return EXIT_SUCCESS;
}
