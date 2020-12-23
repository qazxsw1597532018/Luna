#include <Luna/vmm/emulate.hpp>

#include <Luna/misc/format.hpp>

constexpr uint64_t get_mask(uint8_t s) {
    switch (s) {
        case 1: return 0xFF;
        case 2: return 0xFFFF;
        case 4: return 0xFFFF'FFFF;
        case 8: return 0xFFFF'FFFF'FFFF'FFFF;
        default: PANIC("Unknown size");
    }
}

uint64_t& get_r64(vm::RegisterState& regs, vm::emulate::r64 r) {
    using namespace vm::emulate;
    switch (r) {
        case r64::Rax: return regs.rax;
        case r64::Rcx: return regs.rcx;
        case r64::Rdx: return regs.rdx;
        case r64::Rbx: return regs.rbx;
        case r64::Rsp: return regs.rsp;
        case r64::Rbp: return regs.rbp;
        case r64::Rsi: return regs.rsi;
        case r64::Rdi: return regs.rdi;
        default: PANIC("Unknown reg");
    }
}

vm::RegisterState::Segment& get_sreg(vm::RegisterState& regs, vm::emulate::sreg r) {
    using namespace vm::emulate;
    switch (r) {
        case sreg::Es: return regs.es;
        case sreg::Cs: return regs.cs;
        case sreg::Ss: return regs.ss;
        case sreg::Ds: return regs.ds;
        case sreg::Fs: return regs.fs;
        case sreg::Gs: return regs.gs;
        default: PANIC("Unknown reg");
    }
}



uint64_t read_r64(vm::RegisterState& regs, vm::emulate::r64 r, uint8_t s) {
    return get_r64(regs, r) & get_mask(s);
}

void write_r64(vm::RegisterState& regs, vm::emulate::r64 r, uint64_t v, uint8_t s) {
    auto* reg = &get_r64(regs, r);

    switch (s) {
        case 1: *(uint8_t*)reg = (uint8_t)v; break;
        case 2: *(uint16_t*)reg = (uint16_t)v; break;
        case 4: *reg = (uint32_t)v; break; // Zero-extend in 64bit mode
        case 8: *reg = v; break;
        default: PANIC("Unknown size");
    }
}

struct Modrm {
    uint8_t mod, reg, rm;
};

Modrm parse_modrm(uint8_t v) {
    return Modrm{.mod = (uint8_t)((v >> 6) & 0b11), .reg = (uint8_t)((v >> 3) & 0b111), .rm = (uint8_t)(v & 0b111)};
}



void vm::emulate::emulate_instruction(uint8_t instruction[max_x86_instruction_size], vm::RegisterState& regs, vm::AbstractMMIODriver* driver) {
    uint8_t default_operand_size = regs.cs.attrib.db ? 4 : 2;
    uint8_t other_operand_size = regs.cs.attrib.db ? 2 : 4;
    uint8_t address_size = default_operand_size, operand_size = default_operand_size;
    auto* segment = &get_sreg(regs, sreg::Ds);

    uint8_t i = 0;
    bool done = false;
    while(!done) {
        auto op = instruction[i];

        switch (op) {
        case 0x26: segment = &get_sreg(regs, sreg::Es); break; // ES segment override
        case 0x2E: segment = &get_sreg(regs, sreg::Cs); break; // CS segment override
        case 0x36: segment = &get_sreg(regs, sreg::Ss); break; // SS segment override
        case 0x3E: segment = &get_sreg(regs, sreg::Ds); break; // DS segment override
        case 0x64: segment = &get_sreg(regs, sreg::Fs); break; // FS segment override
        case 0x65: segment = &get_sreg(regs, sreg::Gs); break; // GS segment override

        case 0x66: operand_size = other_operand_size; break; // Operand Size Override
        case 0x67: address_size = other_operand_size; break; // Address Size Override
        
        case 0x89: { // MOV r/m{16, 32}, r{16, 32}
            auto mod = parse_modrm(instruction[++i]);
            
            if(mod.mod == 0) {
                if(mod.rm == 0b100 || mod.rm == 0b101) {
                    PANIC("TODO");
                } else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    auto v = read_r64(regs, (vm::emulate::r64)mod.reg, operand_size);

                    driver->mmio_write(segment->base + src, v, operand_size); // TODO: Don't assume ds
                }
            } else {
                print("Unknown MODR/M: {:#x}\n", (uint16_t)mod.mod);
                PANIC("Unknown");
            }

            done = true;
            break;
        }

        case 0x8A: { // MOV r8, r/m8
            auto mod = parse_modrm(instruction[++i]);
            
            if(mod.mod == 0) {
                if(mod.rm == 0b100 || mod.rm == 0b101) {
                    PANIC("TODO");
                } else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    auto v = driver->mmio_read(segment->base + src, 1); // TODO: Don't assume ds
                    write_r64(regs, (vm::emulate::r64)mod.reg, v, 1);
                }
            } else {
                print("Unknown MODR/M: {:#x}\n", (uint16_t)mod.mod);
                PANIC("Unknown");
            }

            done = true;
            break;
        }

        case 0x8B: { // MOV r{16, 32}, r/m{16, 32}
            auto mod = parse_modrm(instruction[++i]);
            
            if(mod.mod == 0) {
                if(mod.rm == 0b100 || mod.rm == 0b101) {
                    PANIC("TODO");
                } else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    auto v = driver->mmio_read(segment->base + src, operand_size); // TODO: Don't assume ds
                    write_r64(regs, (vm::emulate::r64)mod.reg, v, operand_size);
                }
            } else {
                print("Unknown MODR/M: {:#x}\n", (uint16_t)mod.mod);
                PANIC("Unknown");
            }

            done = true;
            break;
        }

        
        default:
            print("vm: Unknown instruction byte: ");
            for(size_t j = 0; j < max_x86_instruction_size; j++) {
                if(i == j)
                    print("[{:x}] ", (uint16_t)instruction[j]);
                else
                    print("{:x} ", (uint16_t)instruction[j]);
            }
            print("\n");

            PANIC("Unknown instruction");
            break;
        }
        i++;
    }

    regs.rip += i;
}