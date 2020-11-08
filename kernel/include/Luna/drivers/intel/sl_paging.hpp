#pragma once

#include <Luna/common.hpp>
#include <Luna/mm/pmm.hpp>

namespace sl_paging
{
    struct [[gnu::packed]] page_entry {
        uint64_t r : 1;
        uint64_t w : 1;
        uint64_t x : 1;
        
        uint64_t ext_mem_type : 3;
        uint64_t ignore_pat : 1;
        uint64_t reserved : 1;
        uint64_t accessed : 1;
        uint64_t dirty : 1;
        uint64_t reserved_0 : 1;
        uint64_t snoop : 1;
        uint64_t frame : 40;
        uint64_t reserved_1 : 10;
        uint64_t transient_mapping : 1;
        uint64_t reserved_2 : 1;
    };
    static_assert(sizeof(page_entry) == sizeof(uint64_t));

    struct [[gnu::packed]] page_table {
        page_entry entries[512];

        page_entry& operator[](size_t i){
            return entries[i];
        }
    };
    static_assert(sizeof(page_table) == pmm::block_size);

    class context {
        public:
        context(uint8_t levels);
        ~context();

        void map(uintptr_t pa, uintptr_t iova, uint64_t flags);
        uintptr_t get_root_pa() const;

        private:
        uint8_t levels;
        uintptr_t root_pa;
    };
} // namespace sl_paging