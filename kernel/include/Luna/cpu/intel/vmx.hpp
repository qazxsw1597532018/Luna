#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/regs.hpp>

#include <Luna/cpu/intel/ept.hpp>

#include <Luna/vmm/vm.hpp>

namespace vmx {
    enum class PinBasedControls : uint32_t {
        ExtInt = (1 << 0),
        NMI = (1 << 3),
        VNMI = (1 << 5),
        VMXPreempt = (1 << 6),
        PostedIRQs = (1 << 7)
    };

    enum class ProcBasedControls : uint32_t {  
        VMExitOnHlt = (1 << 7),
        VMExitOnPIO = (1 << 24),
        SecondaryControlsEnable = (1u << 31)
    };

    enum class ProcBasedControls2 : uint32_t {
        EPTEnable = (1 << 1),
        UnrestrictedGuest = (1 << 7),
        VMExitOnDescriptor = (1 << 2)
    };

    enum class VMExitControls : uint32_t {
        LongMode = (1 << 9),
        SaveIA32PAT = (1 << 18),
        LoadIA32PAT = (1 << 19),
        SaveIA32EFER = (1 << 20),
        LoadIA32EFER = (1 << 21),
    };
    
    enum class VMEntryControls : uint32_t {
        LoadIA32PAT = (1 << 14),
        LoadIA32EFER = (1 << 15)
    };

    enum class VMExitReasons : uint32_t {
        EPTViolation = 48
    };

    union [[gnu::packed]] EPTViolationQualification {
        struct {
            uint64_t r : 1;
            uint64_t w : 1;
            uint64_t x : 1;
            uint64_t page_r : 1;
            uint64_t page_w : 1;
            uint64_t page_x : 1;
            uint64_t gva_translated : 1;
        };
        uint64_t raw;
    };

    constexpr uint64_t vm_exit_host_addr_space_size = 0x200;

    constexpr uint64_t vmcs_link_pointer = 0x2800;

    constexpr uint64_t pin_based_vm_exec_controls = 0x4000;
    constexpr uint64_t proc_based_vm_exec_controls = 0x4002;
    constexpr uint64_t proc_based_vm_exec_controls2 = 0x401E;
    constexpr uint64_t exception_bitmap = 0x4004;
    constexpr uint64_t vm_exit_control = 0x400C;
    constexpr uint64_t vm_entry_control = 0x4012;

    constexpr uint64_t host_cr0 = 0x6c00;
    constexpr uint64_t host_cr3 = 0x6c02;
    constexpr uint64_t host_cr4 = 0x6c04;

    constexpr uint64_t host_es_sel = 0xC00;
    constexpr uint64_t host_cs_sel = 0xC02;
    constexpr uint64_t host_ss_sel = 0xC04;
    constexpr uint64_t host_ds_sel = 0xC06;
    constexpr uint64_t host_fs_sel = 0xC08;
    constexpr uint64_t host_gs_sel = 0xC0A;
    constexpr uint64_t host_tr_sel = 0xC0C;

    constexpr uint64_t host_fs_base = 0x6C06;
    constexpr uint64_t host_gs_base = 0x6C08;
    constexpr uint64_t host_tr_base = 0x6C0A;
    constexpr uint64_t host_gdtr_base = 0x6C0C;
    constexpr uint64_t host_idtr_base = 0x6C0E;

    constexpr uint64_t host_pat_full = 0x2C00;
    constexpr uint64_t host_efer_full = 0x2C02;

    constexpr uint64_t guest_es_selector = 0x800;
    constexpr uint64_t guest_cs_selector = 0x802;
    constexpr uint64_t guest_ss_selector = 0x804;
    constexpr uint64_t guest_ds_selector = 0x806;
    constexpr uint64_t guest_fs_selector = 0x808;
    constexpr uint64_t guest_gs_selector = 0x80A;
    constexpr uint64_t guest_ldtr_selector = 0x80C;
    constexpr uint64_t guest_tr_selector = 0x80E;
    
    constexpr uint64_t guest_es_limit = 0x4800;
    constexpr uint64_t guest_cs_limit = 0x4802;
    constexpr uint64_t guest_ss_limit = 0x4804;
    constexpr uint64_t guest_ds_limit = 0x4806;
    constexpr uint64_t guest_fs_limit = 0x4808;
    constexpr uint64_t guest_gs_limit = 0x480A;
    constexpr uint64_t guest_ldtr_limit = 0x480C;
    constexpr uint64_t guest_tr_limit = 0x480E;
    constexpr uint64_t guest_gdtr_limit = 0x4810;
    constexpr uint64_t guest_idtr_limit = 0x4812;

    constexpr uint64_t guest_es_base = 0x6806;
    constexpr uint64_t guest_cs_base = 0x6808;
    constexpr uint64_t guest_ss_base = 0x680A;
    constexpr uint64_t guest_ds_base = 0x680C;
    constexpr uint64_t guest_fs_base = 0x680E;
    constexpr uint64_t guest_gs_base = 0x6810;
    constexpr uint64_t guest_ldtr_base = 0x6812;
    constexpr uint64_t guest_tr_base = 0x6814;
    constexpr uint64_t guest_gdtr_base = 0x6816;
    constexpr uint64_t guest_idtr_base = 0x6818;

    constexpr uint64_t guest_es_access_right = 0x4814;
    constexpr uint64_t guest_cs_access_right = 0x4816;
    constexpr uint64_t guest_ss_access_right = 0x4818;
    constexpr uint64_t guest_ds_access_right = 0x481A;
    constexpr uint64_t guest_fs_access_right = 0x481C;
    constexpr uint64_t guest_gs_access_right = 0x481E;
    constexpr uint64_t guest_ldtr_access_right = 0x4820;
    constexpr uint64_t guest_tr_access_right = 0x4822;

    constexpr uint64_t guest_interruptibility_state = 0x4824;
    constexpr uint64_t guest_activity_state = 0x4826;
    constexpr uint64_t guest_smbase = 0x4828;

    constexpr uint64_t guest_intr_status = 0x810;
    constexpr uint64_t guest_pml_index = 0x812;

    constexpr uint64_t guest_cr0 = 0x6800;
    constexpr uint64_t guest_cr3 = 0x6802;
    constexpr uint64_t guest_cr4 = 0x6804;
    constexpr uint64_t guest_dr7 = 0x681A;
    constexpr uint64_t guest_rsp = 0x681C;
    constexpr uint64_t guest_rip = 0x681E;
    constexpr uint64_t guest_rflags = 0x6820;

    constexpr uint64_t guest_efer_full = 0x2806;
    

    constexpr uint64_t ept_control = 0x201A;
    constexpr uint64_t ept_violation_addr = 0x2400;
    constexpr uint64_t ept_violation_flags = 0x6400;
    
    constexpr uint64_t vm_instruction_error = 0x4400;
    constexpr uint64_t vm_exit_reason = 0x4402;

    void init();

    // ACCESSED FROM ASSEMBLY, DO NOT CHANGE WITHOUT CHANGING vmx_low.asm
    struct [[gnu::packed]] GprState {
        uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp;
        uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    };

    struct Vm : public vm::AbstractVm {
        Vm();
        void run();

        void get_regs(vm::RegisterState& regs) const;
        void set_regs(const vm::RegisterState& regs);

        void map(uintptr_t hpa, uintptr_t gpa, uint64_t flags) {
            guest_page.map(hpa, gpa, flags);
        }

        ept::context guest_page;

        private:
        void vmclear();
        void vmptrld() const;
        void write(uint64_t field, uint64_t value);
        uint64_t read(uint64_t field) const;


        uintptr_t vmcs_pa;
        uintptr_t vmcs;

        simd::Context host_simd, guest_simd;
        GprState guest_gprs;
    };
} // namespace vmx
