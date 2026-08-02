#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "device_xml/svd_loader.hpp"

namespace {

int g_failures = 0;

void Fail(const std::string &message) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

void PrintMemoryUsage(const char *label) {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0 || line.rfind("VmRSS:", 0) == 0) {
            std::cout << label << " " << line << "\n";
        }
    }
}

const device_xml::Peripheral *FindPeripheral(const device_xml::Device &device, const std::string &name) {
    for (const device_xml::Peripheral &peripheral : device.peripherals) {
        if (peripheral.name == name) return &peripheral;
    }
    return nullptr;
}

const device_xml::Register *FindRegister(const device_xml::Peripheral &peripheral, const std::string &name) {
    for (const device_xml::Register &reg : peripheral.registers) {
        if (reg.name == name) return &reg;
    }
    return nullptr;
}

}  // namespace

int main() {
    PrintMemoryUsage("before-parse");

    const std::expected<device_xml::Device, std::string> parsed = device_xml::LoadSvd(TRAILER_DEVICE_SVD_PATH);
    if (!parsed) {
        Fail("stm32h7s.svd failed to load: " + parsed.error());
        return EXIT_FAILURE;
    }
    const device_xml::Device &device = *parsed;

    PrintMemoryUsage("after-parse");

    // 83 <peripheral> elements declare their own registers; 22 more use
    // derivedFrom (e.g. ADC2 from ADC1) and inherit theirs - both are real
    // peripherals with their own base address.
    if (device.peripherals.size() != 105) {
        Fail("peripheral count mismatch: " + std::to_string(device.peripherals.size()));
    }

    std::size_t register_count = 0;
    std::size_t field_count = 0;
    std::size_t enum_count = 0;
    std::size_t interrupt_count = 0;
    std::size_t clear_register_count = 0;
    std::size_t zero_width_fields = 0;

    for (const device_xml::Peripheral &peripheral : device.peripherals) {
        interrupt_count += peripheral.interrupts.size();
        for (const device_xml::Register &reg : peripheral.registers) {
            ++register_count;
            if (reg.read_action == device_xml::ReadActionKind::kClear) ++clear_register_count;
            for (const device_xml::RegisterField &field : reg.fields) {
                ++field_count;
                enum_count += field.enumerated_values.size();
                if (field.bit_width == 0) ++zero_width_fields;
            }
        }
    }

    if (register_count != 5902) Fail("register count mismatch: " + std::to_string(register_count));
    if (field_count != 25136) Fail("field count mismatch: " + std::to_string(field_count));
    if (enum_count != 30490) Fail("enumerated value count mismatch: " + std::to_string(enum_count));
    // XSPI2 (derivedFrom XSPI1) declares no <interrupt> of its own, so it
    // inherits XSPI1's 2 - the raw file only has 149 <interrupt> elements.
    if (interrupt_count != 151) Fail("interrupt count mismatch: " + std::to_string(interrupt_count));
    if (clear_register_count != 10) Fail("readAction=clear register count mismatch: " + std::to_string(clear_register_count));
    if (zero_width_fields != 0) Fail("fields with zero bit_width: " + std::to_string(zero_width_fields));

    const device_xml::Peripheral *adc1 = FindPeripheral(device, "ADC1");
    if (!adc1) {
        Fail("ADC1 not found");
    } else {
        if (adc1->base_address != 0x40022000) Fail("ADC1 base address mismatch");
        const device_xml::Register *isr = FindRegister(*adc1, "ADC_ISR");
        if (!isr) {
            Fail("ADC1::ADC_ISR not found");
        } else {
            if (isr->address_offset != 0x00) Fail("ADC_ISR address_offset mismatch");
            bool found_adrdy = false;
            for (const device_xml::RegisterField &field : isr->fields) {
                if (field.name == "ADRDY") {
                    found_adrdy = true;
                    if (field.bit_offset != 0) Fail("ADRDY bit_offset mismatch");
                    if (field.bit_width != 1) Fail("ADRDY bit_width mismatch");
                    if (field.enumerated_values.size() != 2) Fail("ADRDY enumerated value count mismatch");
                }
            }
            if (!found_adrdy) Fail("ADC_ISR::ADRDY field not found");
        }
    }

    const device_xml::Peripheral *adc2 = FindPeripheral(device, "ADC2");
    if (!adc2) {
        Fail("ADC2 not found (derivedFrom resolution failed)");
    } else if (adc1) {
        if (adc2->base_address == adc1->base_address) Fail("ADC2 base address should differ from ADC1's");
        if (adc2->address_block.size == 0) Fail("ADC2 address_block should be inherited, not zeroed");
        if (adc2->registers.size() != adc1->registers.size()) {
            Fail("ADC2 register count should match inherited ADC1 register count, got " +
                 std::to_string(adc2->registers.size()) + " vs " + std::to_string(adc1->registers.size()));
        }
    }

    const device_xml::Peripheral *eth = FindPeripheral(device, "ETH");
    if (!eth) {
        Fail("ETH not found");
    } else {
        const device_xml::Register *isr = FindRegister(*eth, "ETH_MACISR");
        if (!isr) {
            Fail("ETH::ETH_MACISR not found");
        } else if (isr->ReadSafe()) {
            Fail("ETH_MACISR should not be ReadSafe (readAction=clear)");
        }
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "all checks passed\n";
    return EXIT_SUCCESS;
}
