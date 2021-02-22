#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

#include <Luna/cpu/intel/vmx.hpp>
#include <Luna/cpu/amd/svm.hpp>

#include <Luna/vmm/emulate.hpp>

void vm::init() {
    if(vmx::is_supported()) {
        get_cpu().cpu.vm.vendor = CpuVendor::Intel;

        vmx::init();
    } else if(svm::is_supported()) {
        get_cpu().cpu.vm.vendor = CpuVendor::AMD;

        svm::init();
    } else
        PANIC("Unknown virtualization vendor");
}

vm::VCPU::VCPU(vm::Vm* vm, uint8_t id): vm{vm}, lapic{id} {
    uint64_t cr0_constraint = 0, cr4_constraint = 0, efer_constraint = 0;
    switch (get_cpu().cpu.vm.vendor) {
        case CpuVendor::Intel:
            vcpu = new vmx::Vm{vm->mm, this};

            cr0_constraint = vmx::get_cr0_constraint();
            cr4_constraint = vmx::get_cr4_constraint();
            break;
        case CpuVendor::AMD:
            vcpu = new svm::Vm{vm->mm, this};

            cr0_constraint = svm::get_cr0_constraint();
            efer_constraint = svm::get_efer_constraint();
            break;
        default:
            PANIC("Unknown virtualization vendor");
    }

    vm::RegisterState regs{};

    regs.cs = {.selector = 0xF000, .base = 0xFFFF'0000, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};

    regs.ds = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};
    regs.es = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};
    regs.ss = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};
    regs.fs = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};
    regs.gs = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};

    regs.ldtr = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 2, .present = 1}};
    regs.tr = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 3, .present = 1}};

    regs.idtr = {.base = 0, .limit = 0xFFFF};
    regs.gdtr = {.base = 0, .limit = 0xFFFF};

    regs.dr6 = 0xFFFF0FF0;
    regs.dr7 = 0x400;
    regs.rsp = 0;

    regs.rip = 0xFFF0;
    regs.rflags = (1 << 1);

    regs.cr0 = (cr0_constraint & ~((1 << 0) | (1 << 31))); // Clear PE and PG;
    regs.cr4 = cr4_constraint;

    regs.cr3 = 0;
    regs.efer = efer_constraint;

    vcpu->set_regs(regs);

    auto& simd = vcpu->get_guest_simd_context();
    simd.data()->fcw = 0x40;
    simd.data()->mxcsr = 0x1F80;

    // MSR init
    apicbase = 0xFEE0'0000 | (1 << 11) | ((id == 0) << 8); // xAPIC enable, If id == 0 set BSP bit too
    lapic.update_apicbase(apicbase);
}
        
void vm::VCPU::get_regs(vm::RegisterState& regs) const { vcpu->get_regs(regs); }
void vm::VCPU::set_regs(const vm::RegisterState& regs) { vcpu->set_regs(regs); }
void vm::VCPU::set(VmCap cap, bool value) { vcpu->set(cap, value); }

bool vm::VCPU::run() {
    while(true) {
        vm::RegisterState regs{};
        vm::VmExit exit{};

        if(!vcpu->run(exit))
            return false;

        switch (exit.reason) {
        case VmExit::Reason::Vmcall: { // For now a VMMCALL is just an exit
            return true;
        }

        case VmExit::Reason::MMUViolation: {
            get_regs(regs);

            auto grip = regs.cs.base + regs.rip;

            auto emulate_mmio = [&](AbstractMMIODriver* driver) {
                ASSERT((grip + 15) < align_up(grip, pmm::block_size)); // TODO: Support page boundary instructions
                    
                auto hpa = vm->mm->get_phys(grip);
                ASSERT(hpa); // Assert that the page is actually mapped
                auto* host_buf = (uint8_t*)(hpa + phys_mem_map);

                uint8_t instruction[max_x86_instruction_size];
                memcpy(instruction, host_buf, 15);

                vm::emulate::emulate_instruction(instruction, regs, driver);
                set_regs(regs);
            };

            if((exit.mmu.gpa & ~0xFFF) == (apicbase & ~0xFFF)) {
                emulate_mmio(&lapic);
                goto did_mmio;
            }
            
            for(const auto [base, driver] : vm->mmio_map) {
                if(exit.mmu.gpa >= base && exit.mmu.gpa <= (base + driver.second))  {
                    // Access is in an MMIO region
                    emulate_mmio(driver.first);
                    goto did_mmio;
                }
            }

            // No MMIO region, so a page violation
            print("vm: MMU Violation\n");
            print("    gRIP: {:#x}, gPA: {:#x}\n", grip, exit.mmu.gpa);
            print("    Access: {:s}{:s}{:s}, {:s}\n", exit.mmu.access.r ? "R" : "", exit.mmu.access.w ? "W" : "", exit.mmu.access.x ? "X" : "", exit.mmu.access.user ? "User" : "Supervisor");
            if(exit.mmu.page.present)
                print("    Page: {:s}{:s}{:s}, {:s}\n", exit.mmu.page.r ? "R" : "", exit.mmu.page.w ? "W" : "", exit.mmu.page.x ? "X" : "", exit.mmu.page.user ? "User" : "Supervisor");
            else
                print("    Page: Not present\n");
            if(exit.mmu.reserved_bits_set)
                print("    Reserved bits set\n");
            return false;

            did_mmio:
            break;
        }

        case VmExit::Reason::PIO: {
            get_regs(regs); 

            ASSERT(!exit.pio.rep); // TODO
            ASSERT(!exit.pio.string);

            auto reg_clear = [&]<typename T>(T& value) {
                switch(exit.pio.size) {
                    case 1: value &= 0xFF; break;
                    case 2: value &= 0xFFFF; break;
                    case 4: value &= 0xFFFF'FFFF; break;
                    default: PANIC("Unknown PIO Size");
                }
            };

            if(!vm->pio_map.contains(exit.pio.port)) {
                print("vcpu: Unhandled PIO Access to port {:#x}\n", exit.pio.port);

                if(!exit.pio.write) {
                    switch(exit.pio.size) {
                        case 1: regs.rax &= ~0xFF; break;
                        case 2: regs.rax &= ~0xFFFF; break;
                        case 4: regs.rax &= ~0xFFFF'FFFF; break;
                        default: PANIC("Unknown PIO Size");
                    }
                }
                
                set_regs(regs);
                break;
            }

            auto* driver = vm->pio_map[exit.pio.port];

            if(exit.pio.write) {
                auto value = regs.rax;
                reg_clear(value);

                driver->pio_write(exit.pio.port, value, exit.pio.size);
            } else {
                auto value = driver->pio_read(exit.pio.port, exit.pio.size);

                switch(exit.pio.size) {
                    case 1: regs.rax &= ~0xFF; break;
                    case 2: regs.rax &= ~0xFFFF; break;
                    case 4: regs.rax &= ~0xFFFF'FFFF; break;
                    default: PANIC("Unknown PIO Size");
                }
                reg_clear(value);

                regs.rax |= value;

                set_regs(regs);
            }

            break;
        }

        case VmExit::Reason::CPUID: {
            get_regs(regs);

            auto write_low32 = [&](uint64_t& reg, uint32_t val) { reg &= ~0xFFFF'FFFF; reg |= val; };

            auto leaf = regs.rax & 0xFFFF'FFFF;
            auto subleaf = regs.rcx & 0xFFFF'FFFF;

            constexpr uint32_t luna_sig = 0x616E754C; // Luna in ASCII
            
            auto passthrough = [&]() {
                uint32_t a, b, c, d;
                ASSERT(cpu::cpuid(leaf, subleaf, a, b, c, d));

                write_low32(regs.rax, a);
                write_low32(regs.rbx, b);
                write_low32(regs.rcx, c);
                write_low32(regs.rdx, d);
            };

            auto os_support_bit = [&](uint64_t& reg, uint8_t cr4_bit, uint8_t bit) {
                reg &= ~(1 << bit);

                bool os = (regs.cr4 >> cr4_bit) & 1;
                reg |= (os << bit);
            };

            if(leaf == 0) {
                passthrough();
            } else if(leaf == 1) {
                passthrough();

                regs.rcx |= (1 << 31); // Set Hypervisor Present bit

                os_support_bit(regs.rdx, 9, 24);
                os_support_bit(regs.rcx, 18, 27); // Only set OSXSAVE bit if actually enabled by OS
            } else if(leaf == 0x4000'0000) {
                write_low32(regs.rax, 0);
                write_low32(regs.rbx, luna_sig);
                write_low32(regs.rcx, luna_sig);
                write_low32(regs.rdx, luna_sig);
            } else if(leaf == 0x8000'0000) {
                passthrough();
            } else if(leaf == 0x8000'0001) {
                passthrough();
                os_support_bit(regs.rdx, 9, 24);
            } else if(leaf == 0x8000'0008) {
                passthrough(); // TODO: Do we want this to be passthrough?
                write_low32(regs.rcx, 0); // Clear out core info
            } else {
                print("vcpu: Unhandled CPUID: {:#x}:{}\n", leaf, subleaf);
            }

            set_regs(regs);
            break;
        }

        case VmExit::Reason::MSR: {
            get_regs(regs);
            auto index = regs.rcx & 0xFFFF'FFFF;
            auto value = (regs.rax & 0xFFFF'FFFF) | (regs.rdx << 32);

            auto write_low32 = [&](uint64_t& reg, uint32_t val) { reg &= ~0xFFFF'FFFF; reg |= val; };

            if(index == msr::ia32_tsc) {
                if(exit.msr.write)
                    tsc = value;
                else
                    value = tsc;
            } else if(index == msr::ia32_mtrr_cap) {
                if(exit.msr.write)
                    vcpu->inject_int(AbstractVm::InjectType::Exception, 13, true, 0); // Inject #GP(0)

                value = (1 << 10) | (1 << 8) | 8; // WC valid, Fixed MTRRs valid, 8 Variable MTRRs
            } else if(index == msr::ia32_apic_base) {
                if(exit.msr.write) {
                    apicbase = value;
                    lapic.update_apicbase(apicbase);
                } else {
                    value = apicbase;
                }
            } else if(index >= 0x200 && index <= 0x2FF) {
                update_mtrr(exit.msr.write, index, value);
            } else {
                if(exit.msr.write) {
                    print("vcpu: Unhandled wrmsr({:#x}, {:#x})\n", index, value);
                } else {
                    print("vcpu: Unhandled rdmsr({:#x})\n", index);
                    value = 0;
                }
            }
            
            if(!exit.msr.write) {
                write_low32(regs.rax, value & 0xFFFF'FFFF);
                write_low32(regs.rdx, value >> 32);

                set_regs(regs);
            }

            break;
        }
        
        default:
            print("vcpu: Exit due to {:s}\n", exit.reason_to_string(exit.reason));
            if(exit.instruction_len != 0) {
                print("         Opcode: ");
                for(size_t i = 0; i < exit.instruction_len; i++)
                    print("{:#x} ", (uint64_t)exit.instruction[i]);
                print("\n");
            }
            break;
        }
    } 
    return true;
}

void vm::VCPU::update_mtrr(bool write, uint32_t index, uint64_t& value) {
    auto update = [this](){
        // We can mostly just ignore MTRRs and whatever guests want for paging, as we force WB
        // However when VT-d doesn't support snooping, it's needed to mark pages as UC, when passing through devices
        // AMD-Vi always supports snooping, and this is always forced on, so no such thing is needed

        // Debug code for printing MTRRs
        /*print("vm::mtrr: Update, Enable: {}, Fixed Enable: {}, Default Type: {}\n", mtrr.enable, mtrr.fixed_enable, (uint16_t)mtrr.default_type);

        for(size_t i = 0; i < 8; i++) {
            auto var = mtrr.var[i];

            if(!(var.mask & 0x800)) // Valid
                continue;

            var.mask &= ~0x800;

            auto physical_bits = 36;
            {
                uint32_t a, b, c, d;
                if(cpu::cpuid(0x8000'0008, a, b, c, d))
                    physical_bits = a & 0xFF;
            }
            auto size = -var.mask & ((1ull << physical_bits) - 1);
            auto start = var.base & ~0xFFF;
            auto type = var.base & 0xFF;

            print("vm::mtrr: Var{}: {:#x} -> {:#x} => Type: {}\n", i, start, start + size - 1, type);
        }

        if(mtrr.fixed_enable) {
            print("vm::mtrr: fix64K_00000: {:#x}\n", mtrr.fix[0]);

            print("vm::mtrr: fix16K_80000: {:#x}\n", mtrr.fix[1]);
            print("vm::mtrr: fix16K_A0000: {:#x}\n", mtrr.fix[2]);

            print("vm::mtrr: fix4K_C0000: {:#x}\n", mtrr.fix[3]);
            print("vm::mtrr: fix4K_C8000: {:#x}\n", mtrr.fix[4]);
            print("vm::mtrr: fix4K_D0000: {:#x}\n", mtrr.fix[5]);
            print("vm::mtrr: fix4K_D8000: {:#x}\n", mtrr.fix[6]);
            print("vm::mtrr: fix4K_E0000: {:#x}\n", mtrr.fix[7]);
            print("vm::mtrr: fix4K_E8000: {:#x}\n", mtrr.fix[8]);
            print("vm::mtrr: fix4K_F0000: {:#x}\n", mtrr.fix[9]);
            print("vm::mtrr: fix4K_F8000: {:#x}\n", mtrr.fix[10]);
        }*/
    };

    if(index == msr::ia32_mtrr_def_type) {
        if(write) {
            mtrr.cmd = value;

            mtrr.enable = (value >> 11) & 1;
            mtrr.fixed_enable = (value >> 10) & 1;
            mtrr.default_type = value & 0xFF;

            update();
        } else {
            value = mtrr.cmd;
        }
    } else if(index >= msr::ia32_mtrr_physbase0 && index <= msr::ia32_mtrr_physmask7) {
        size_t i = ((index - msr::ia32_mtrr_physbase0) & ~1) / 2;
        size_t mask = index & 1;

        if(write) {
            if(mask)
                mtrr.var[i].mask = value;
            else
                mtrr.var[i].base = value;

            update();
        } else {
            if(mask)
                value = mtrr.var[i].mask;
            else
                value = mtrr.var[i].base;
        }
    } else if(index == msr::ia32_mtrr_fix64K_00000) {
        if(write) {
            mtrr.fix[0] = value;
            update();
        } else {
            value = mtrr.fix[0];
        }
    } else if(index == msr::ia32_mtrr_fix16K_80000 || index == msr::ia32_mtrr_fix16K_A0000) {
        size_t i = (index - msr::ia32_mtrr_fix16K_80000) + 1;
        if(write) {
            mtrr.fix[i] = value;
            update();
        } else {
            value = mtrr.fix[i];
        }
    } else if(index >= msr::ia32_mtrr_fix4K_C0000 && index <= msr::ia32_mtrr_fix4K_F8000) {
        size_t i = (index - msr::ia32_mtrr_fix4K_C0000) + 3;
        if(write) {
            mtrr.fix[i] = value;
            update();
        } else {
            value = mtrr.fix[i];
        }
    } else {
        print("vm::mtrr: Unknown MTRR MSR {:#x}\n", index);
    }
}

vm::Vm::Vm(uint8_t n_cpus) {
    switch (get_cpu().cpu.vm.vendor) {
        case CpuVendor::Intel:
            mm = vmx::create_ept();
            break;
        case CpuVendor::AMD:
            mm = svm::create_npt();
            break;
        default:
            PANIC("Unknown virtualization vendor");
    }


    ASSERT(n_cpus > 0); // Make sure there's at least 1 VCPU
    for(uint8_t i = 0; i < n_cpus; i++)
        cpus.emplace_back(this, i);
}