//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "UmInstance.h"

umm::UmInstance::UmInstance(umm::UmState sv) : sv_(sv) {}

void umm::UmInstance::SetArguments(const uint64_t argc,
                                   const char *argv[]) {
  // Maybe this should be part of UmState
  sv_.ef.rdi = argc;
  if(argv)
    sv_.ef.rsi = (uint64_t)argv;
}

void umm::UmInstance::Print() {
  kprintf("Number of pages allocated: %d\n", page_count);
  sv_.Print();
}

umm::UmState umm::UmInstance::Snapshot(ebbrt::idt::ExceptionFrame *ef){
  UmState ret;
  //ret.ef = sv_.ef;  // ???
  for( auto const &umi_reg : sv_.region_list_ ){
    kprintf(BLUE "Checkpointing %s region\n", umi_reg.name.c_str());
    UmState::Region reg = umi_reg;
    //if(umi_reg.writable){
    //  /** Capture the pages mapped for the active instance */
    //  // TODO(jmcadden): Capture only written pages 
    //  reg.data = (unsigned char*)calloc(1, umi_reg.length);
    //  std::memcpy((void *)reg.data, (const void *)umi_reg.start,
    //              umi_reg.length);
    //}
    ret.AddRegion(reg);
  }
  return ret;
}

uintptr_t umm::UmInstance::GetBackingPage(uintptr_t vaddr) {
  auto vp_start_addr = Pfn::Down(vaddr).ToAddr();
  auto reg = sv_.GetRegionOfAddr(vaddr);

  // TODO(jmcadden): Support large pages per-region
  kassert(reg.page_order == 0);

  /* Allocate new physical page for the faulted region */
  Pfn backing_page = ebbrt::page_allocator->Alloc();
  page_count++;
  auto bp_start_addr = backing_page.ToAddr();


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
