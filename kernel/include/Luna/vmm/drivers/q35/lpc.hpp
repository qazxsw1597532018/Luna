#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

#include <Luna/vmm/drivers/pci/pci.hpp>
#include <Luna/vmm/drivers/q35/acpi.hpp>

namespace vm::q35::lpc {
    constexpr uint8_t cap_base = 0xE0;

    constexpr uint8_t pmbase = 0x40;
    constexpr uint8_t acpi_cntl = 0x44;

    constexpr uint8_t pirq_a_base = 0x60;
    constexpr uint8_t pirq_a_len = 0x4;

    constexpr uint8_t pirq_b_base = 0x68;
    constexpr uint8_t pirq_b_len = 0x4;

    constexpr uint8_t root_complex_base = 0xF0;

    struct Driver : public vm::pci::AbstractPCIDriver {
        Driver(vm::Vm* vm, vm::q35::acpi::Driver* acpi_dev): vm{vm}, acpi_dev{acpi_dev} {
            space.header.vendor_id = 0x8086;
            space.header.device_id = 0x2918;

            space.header.command = (1 << 2) | (1 << 1) | (1 << 0);
            space.header.status = 0x210;

            space.header.revision = 2;

            space.header.class_id = 6;
            space.header.subclass = 1; // PCI-to-ISA Bridge
            space.header.prog_if = 0;

            space.header.header_type = (1 << 7); // Multifunction

            space.header.capabilities = cap_base;

            space.data8[cap_base] = 9; // Vendor Specific
            space.data8[cap_base + 1] = 0; // No Other Caps
            space.data8[cap_base + 2] = 0xC;
            space.data8[cap_base + 3] = 0x10; // Feature Detection Cap
            space.data32[(cap_base + 4) / 4] = 0; // Feature low dword, no fancy features supported
            space.data32[(cap_base + 8) / 4] = 0; // Feature high dword

            space.data32[pmbase / 4] = 1; // Bit0 is hardwired to 1 to indicate PIO space
            space.data8[acpi_cntl] = 0;

            space.data8[pirq_a_base] = 0x80; // Default PIRQ Values
            space.data8[pirq_a_base + 1] = 0x80;
            space.data8[pirq_a_base + 2] = 0x80;
            space.data8[pirq_a_base + 3] = 0x80;
            space.data8[pirq_b_base] = 0x80;
            space.data8[pirq_b_base + 1] = 0x80;
            space.data8[pirq_b_base + 2] = 0x80;
            space.data8[pirq_b_base + 3] = 0x80;

            space.data32[root_complex_base / 4] = 0;
        }

        void register_pci_driver(vm::pci::HostBridge* bus) {
            vm::pci::DeviceID id{.raw = 0};
            id.bus = 0;
            id.slot = 31;
            id.func = 0;
            bus->register_pci_driver(id, this);
        }

        void pci_write([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint32_t value, uint8_t size) {
            auto do_write = [&] {
                switch (size) {
                    case 1: space.data8[reg] = value; break;
                    case 2: space.data16[reg / 2] = value; break;
                    case 4: space.data32[reg / 4] = value; break;
                    default: PANIC("Unknown PCI Access size");
                }
            };
            
            if(ranges_overlap(reg, size, 0, sizeof(pci::ConfigSpaceHeader)))
                pci_update(reg, size, value);
            else if(ranges_overlap(reg, size, pirq_a_base, pirq_a_len) || ranges_overlap(reg, size, pirq_b_base, pirq_b_len)) {
                do_write();
                pirq_update();
            } else if(ranges_overlap(reg, size, pmbase, 4)) {
                do_write();
                pmbase_update();
            } else if(ranges_overlap(reg, size, acpi_cntl, 1)) {
                do_write();
                acpi_cntl_update();
            } else if(ranges_overlap(reg, size, root_complex_base, 4)) {
                do_write();
                root_complex_base_update();
            } else
                print("q35::lpc: Unhandled PCI write, reg: {:#x}, value: {:#x}\n", reg, value);
        }

        uint32_t pci_read([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint8_t size) {
            uint32_t ret = 0;
            switch (size) {
                case 1: ret = space.data8[reg]; break;
                case 2: ret = space.data16[reg / 2]; break;
                case 4: ret = space.data32[reg / 4]; break;
                default: PANIC("Unknown PCI Access size");
            }

            if(ranges_overlap(reg, size, 0, sizeof(pci::ConfigSpaceHeader)))
                ; // Nothing special to do here
            else
                print("q35::lpc: Unhandled PCI read, reg: {:#x}, size: {:#x}\n", reg, (uint16_t)size);

            return ret;
        }

        // TODO: Abstract this to common class
        void pci_update(uint16_t reg, uint8_t size, uint32_t value) {
            // TODO: This is horrible and broken and horrible
            auto handle_bar = [&](uint16_t bar) {
                if(reg != bar)
                    return false;
                
                ASSERT(size == 4); // Please don't tell me anyone does unaligned BAR r/w
                if(value == 0xFFFF'FFFF) // Do stupid size thing
                    space.data32[reg] = 0xFFFF'FFFF; // We don't decode any bits
                else
                    space.data32[reg] = value;

                return true;
            };

            if(handle_bar(0x10)) // BAR0
                return;
            if(handle_bar(0x14)) // BAR1
                return;
            if(handle_bar(0x18)) // BAR2
                return;
            if(handle_bar(0x1C)) // BAR3
                return;
            if(handle_bar(0x20)) // BAR4
                return;
            if(handle_bar(0x24)) // BAR5
                return;
            if(handle_bar(0x30)) // Expansion ROM Bar
                return;

            switch (size) {
                case 1: space.data8[reg] = value; break;
                case 2: space.data16[reg / 2] = value; break;
                case 4: space.data32[reg / 4] = value; break;
                default: PANIC("Unknown PCI Access size");
            }
        }


        void pirq_update() {
            // TODO: Handle PIRQ Writes somehow
        }

        void pmbase_update() {
            space.data32[pmbase / 4] |= 1; // Bit0 is hardwired to 1

            acpi_pmbase = space.data32[pmbase / 4] & ~1;
        }

        void acpi_cntl_update() {
            constexpr uint8_t sci_map[] = {
                [0b000] = 9,
                [0b001] = 10,
                [0b010] = 11,
                [0b011] = 0xFF,
                [0b100] = 20,
                [0b101] = 21,
                [0b110] = 0xFF,
                [0b111] = 0xFF,
            };

            acpi_enable = (space.data8[acpi_cntl] >> 7) & 1;
            sci = sci_map[space.data8[acpi_cntl] & 0x7];

            print("q35::lpc: SCI: {} ACPI Decode: {} at IO: {:#x}\n", sci, acpi_enable, acpi_pmbase);
            acpi_dev->update(acpi_enable, acpi_pmbase);
        }

        void root_complex_base_update() {
            root_complex_enable = (space.data32[root_complex_base / 4] & 1);
            root_complex_addr = (space.data32[root_complex_base / 4] >> 13) << 13;

            print("q35::lpc: Root Complex: {}, Base: {:#x}, TODO\n", root_complex_enable, root_complex_addr);
        }


        bool acpi_enable;
        uint16_t acpi_pmbase;
        uint8_t sci;

        bool root_complex_enable;
        uint32_t root_complex_addr;

        pci::ConfigSpace space;
        vm::Vm* vm;
        vm::q35::acpi::Driver* acpi_dev;
    };
} // namespace vm::q35::lpc
