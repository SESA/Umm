//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "UmInstance.h"

umm::UmInstance::UmInstance(umm::UmState sv) : sv_(sv) {
  // Set the instruction pointer to the execution entrypoint
  ef_.rip = sv.entry_;
}

void umm::UmInstance::SetArguments(const uint64_t argc,
                                   const char *argv[]) {
  ef_.rdi = argc;
  if(argv)
    ef_.rsi = (uint64_t)argv;
}

uintptr_t umm::UmInstance::GetBackingPageAddress(uintptr_t vaddr) {

  auto vp_start_addr = Pfn::Down(vaddr).ToAddr();

  /* Allocate new physical page for the faulted region */
  Pfn backing_page = ebbrt::page_allocator->Alloc();
  auto bp_start_addr = backing_page.ToAddr();

  auto reg = sv_.GetRegionOfAddr(vaddr);

  // Check for a backing source 
  if (reg.data != nullptr) {
    // Copy backing data onto the allocated page
    const void *elf_src_addr = reg.data + reg.GetOffset(vp_start_addr);
    std::memcpy((void *)bp_start_addr, elf_src_addr, kPageSize);
  }

  // Zero bss pages
  if (reg.name == ".bss") {
    std::memset((void *)bp_start_addr, 0, kPageSize);
  }

  return bp_start_addr;
}
