#include "vmem.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <random>

#include "champsim.h"
#include "util.h"

extern uint8_t use_pcache;

extern uint64_t code_size, heap_size, mmap_size, stack_size;

VirtualMemory::VirtualMemory(uint64_t capacity, uint64_t pg_size,
			     uint32_t page_table_levels, uint64_t random_seed)
	: pt_levels(page_table_levels), page_size(pg_size),
	  ppage_free_list((capacity - VMEM_RESERVE_CAPACITY) / PAGE_SIZE,
			  PAGE_SIZE)
{
	assert(capacity % PAGE_SIZE == 0);
	assert(pg_size == (1ul << lg2(pg_size)) && pg_size > 1024);

        pmem_size = capacity;
	// populate the free list
	ppage_free_list.front() = VMEM_RESERVE_CAPACITY;
	std::partial_sum(std::cbegin(ppage_free_list),
			 std::cend(ppage_free_list),
			 std::begin(ppage_free_list));

	// then shuffle it
	std::shuffle(std::begin(ppage_free_list), std::end(ppage_free_list),
		     std::mt19937_64{ random_seed });

	next_pte_page = ppage_free_list.front();
	ppage_free_list.pop_front();

}

uint64_t VirtualMemory::va_to_ptable_pa(uint64_t vaddr)
{
        uint64_t paddr;
        paddr = pcache_va_to_pa(0, vaddr);
        return ptable_start + (paddr >> LOG2_PAGE_SIZE);

}

uint64_t VirtualMemory::pa_to_ptable_pa(uint64_t paddr)
{
        return ptable_start + (paddr >> LOG2_PAGE_SIZE);
}

void VirtualMemory::setup_pcache()
{
	if (use_pcache) {
                ptable_start = 0x2000;
                ptable_size = pmem_size / PAGE_SIZE;

		code_virt_start = 0x0;
		code_virt_end = code_size;
		code_phys_start = PAGE_ALIGN(ptable_size);
		code_phys_end = code_phys_start + code_size;

		heap_virt_start = 0x555555554000;
		heap_virt_end = heap_virt_start + heap_size;
		heap_phys_start = PAGE_ALIGN(code_phys_end);
		heap_phys_end = heap_phys_start + heap_size;

		// mmap allocation is top-down for x86 by default
		mmap_virt_end = 0x7ffff8000000;
		mmap_virt_start = mmap_virt_end - mmap_size;
		mmap_phys_start = PAGE_ALIGN(heap_phys_end);
		mmap_phys_end = mmap_phys_start + mmap_size;

		stack_virt_end = 0x7ffffffff000;
		stack_virt_start = stack_virt_end - stack_size;
		stack_phys_start = PAGE_ALIGN(mmap_phys_end);
		stack_phys_end = stack_phys_start + stack_size;

		if (stack_phys_end > pmem_size) {
			printf("Not enough memory for Pcache mode. Try small sizes of code/heap/mmap/stack.\n");
			exit(1);
		}
		printf("Code [%#lx-%#lx] - [%#lx-%#lx]\n", code_virt_start,
		       code_virt_end, code_phys_start, code_phys_end);
		printf("Heap [%#lx-%#lx] - [%#lx-%#lx]\n", heap_virt_start,
		       heap_virt_end, heap_phys_start, heap_phys_end);
		printf("Mmap [%#lx-%#lx] - [%#lx-%#lx]\n", mmap_virt_start,
		       mmap_virt_end, mmap_phys_start, mmap_phys_end);
		printf("Stack [%#lx-%#lx] - [%#lx-%#lx]\n", stack_virt_start,
		       stack_virt_end, stack_phys_start, stack_phys_end);
	}

}

uint64_t VirtualMemory::shamt(uint32_t level) const
{
	return LOG2_PAGE_SIZE + lg2(page_size / PTE_BYTES) * (level);
}

uint64_t VirtualMemory::get_offset(uint64_t vaddr, uint32_t level) const
{
	return (vaddr >> shamt(level)) & bitmask(lg2(page_size / PTE_BYTES));
}

uint64_t VirtualMemory::pcache_va_to_pa(uint32_t cpu_num, uint64_t vaddr)
{
	if (vaddr >= code_virt_start && vaddr < code_virt_end) {
		return code_phys_start + (vaddr - code_virt_start);
	} else if (vaddr >= heap_virt_start && vaddr < heap_virt_end) {
		return heap_phys_start + (vaddr - heap_virt_start);
	} else if (vaddr >= mmap_virt_start && vaddr < mmap_virt_end) {
		return mmap_phys_start + (vaddr - mmap_virt_start);
	} else if (vaddr >= stack_virt_start && vaddr < stack_virt_end) {
		return stack_phys_start + (vaddr - stack_virt_start);
	} else {
                printf("Not enough memory for %#lx\n", vaddr);
                printf("Code [%#lx-%#lx] - [%#lx-%#lx]\n",code_virt_start, code_virt_end, code_phys_start, code_phys_end);
                printf("Heap [%#lx-%#lx] - [%#lx-%#lx]\n",heap_virt_start, heap_virt_end, heap_phys_start, heap_phys_end);
                printf("Mmap [%#lx-%#lx] - [%#lx-%#lx]\n",mmap_virt_start, mmap_virt_end, mmap_phys_start, mmap_phys_end);
                printf("Stack [%#lx-%#lx] - [%#lx-%#lx]\n",stack_virt_start, stack_virt_end, stack_phys_start, stack_phys_end);
                exit(1);
        }
}

uint64_t VirtualMemory::va_to_pa(uint32_t cpu_num, uint64_t vaddr)
{
	auto [ppage, fault] = vpage_to_ppage_map.insert(
		{ { cpu_num, vaddr >> LOG2_PAGE_SIZE },
		  ppage_free_list.front() });

	// this vpage doesn't yet have a ppage mapping
	if (fault)
		ppage_free_list.pop_front();

	return splice_bits(ppage->second, vaddr, LOG2_PAGE_SIZE);
}

uint64_t VirtualMemory::get_pte_pa(uint32_t cpu_num, uint64_t vaddr,
				   uint32_t level)
{
	std::tuple key{ cpu_num, vaddr >> shamt(level + 1), level };
	auto [ppage, fault] = page_table.insert({ key, next_pte_page });

	// this PTE doesn't yet have a mapping
	if (fault) {
		next_pte_page += page_size;
		if (next_pte_page % PAGE_SIZE) {
			next_pte_page = ppage_free_list.front();
			ppage_free_list.pop_front();
		}
	}

	return splice_bits(ppage->second, get_offset(vaddr, level) * PTE_BYTES,
			   lg2(page_size));
}
