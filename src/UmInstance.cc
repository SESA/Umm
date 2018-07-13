//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "UmInstance.h"

void umm::UmInstance::SetArguments(const uint64_t argc,
                                   const char *argv[]) {
  sv_.ef.rdi = argc;
  if (argv)
    sv_.ef.rsi = (uint64_t)argv;
}

void umm::UmInstance::Print() {
  kprintf("Number of pages allocated: %d\n", page_count);
  sv_.Print();
}

uintptr_t umm::UmInstance::GetBackingPage(uintptr_t vaddr) {
  auto vp_start_addr = Pfn::Down(vaddr).ToAddr();
  umm::UmSV::Region& reg = sv_.GetRegionOfAddr(vaddr);
  reg.count++;

  // TODO(jmcadden): Support large pages per-region
  kassert(reg.page_order == 0);

  /* Allocate new physical page for the faulted region */
  Pfn backing_page = ebbrt::page_allocator->Alloc();
  page_count++;
  auto bp_start_addr = backing_page.ToAddr();

  // Log the virtual addresses of the *writable* faulted page
  if(reg.writable){
     sv_.faulted_pages_.push_back(vp_start_addr);
  }

  // XXX: KLUDGE ALERT...

  // Check for a backing source
  if (reg.data != nullptr) {
    // XXX: Here be Dragons...
    unsigned char *elf_src_addr;
    if (reg.name == "usr") {
      elf_src_addr = reg.data;
    } else {
      elf_src_addr = reg.data + reg.GetOffset(vp_start_addr);
    }
    // Copy backing data onto the allocated page
    std::memcpy((void *)bp_start_addr, (const void *)elf_src_addr, kPageSize);
  } else if (reg.name == ".bss") {
    // Zero bss pages
    std::memset((void *)bp_start_addr, 0, kPageSize);
  }

  return bp_start_addr;
}
