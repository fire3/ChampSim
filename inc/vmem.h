#ifndef VMEM_H
#define VMEM_H

#include <deque>
#include <map>

#define VMEM_RAND_FACTOR 91827349653
// reserve 1MB of space
#define VMEM_RESERVE_CAPACITY 1048576

#define PTE_BYTES 8

#define __ALIGN_KERNEL(x, a) __ALIGN_KERNEL_MASK(x, (a)-1)
#define __ALIGN_KERNEL_MASK(x, mask) (((x) + (mask)) & ~(mask))

#define ALIGN(x, a) __ALIGN_KERNEL((x), (a))

#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

//Virtual Address: 57 bit (9+9+9+9+9+12), rest MSB bits will be used to generate a unique VA per CPU.
//PTL5->PTL4->PTL3->PTL2->PTL1->PFN

class VirtualMemory {
    private:
	std::map<std::pair<uint32_t, uint64_t>, uint64_t> vpage_to_ppage_map;
	std::map<std::tuple<uint32_t, uint64_t, uint32_t>, uint64_t> page_table;

	uint64_t next_pte_page;
	uint64_t pmem_size;

    public:
	uint64_t code_phys_start, heap_phys_start, mmap_phys_start,
		stack_phys_start;
	uint64_t code_phys_end, heap_phys_end, mmap_phys_end, stack_phys_end;
	uint64_t code_virt_start, heap_virt_start, mmap_virt_start,
		stack_virt_start;
	uint64_t code_virt_end, heap_virt_end, mmap_virt_end, stack_virt_end;
        uint64_t ptable_start;
	uint64_t ptable_size; // Page Permission  Table size. = pmem_size/PAGE_SIZE Bytes

	const uint32_t pt_levels;
	const uint32_t page_size; // Size of a PTE page
	std::deque<uint64_t> ppage_free_list;

	// capacity and pg_size are measured in bytes, and capacity must be a multiple of pg_size
	VirtualMemory(uint64_t capacity, uint64_t pg_size,
		      uint32_t page_table_levels, uint64_t random_seed);
	uint64_t shamt(uint32_t level) const;
	uint64_t get_offset(uint64_t vaddr, uint32_t level) const;
	uint64_t va_to_pa(uint32_t cpu_num, uint64_t vaddr);
	uint64_t pcache_va_to_pa(uint32_t cpu_num, uint64_t vaddr);
        uint64_t va_to_ptable_pa(uint64_t vaddr);
        uint64_t pa_to_ptable_pa(uint64_t paddr);
	uint64_t get_pte_pa(uint32_t cpu_num, uint64_t vaddr, uint32_t level);
	void setup_pcache();
};

#endif
