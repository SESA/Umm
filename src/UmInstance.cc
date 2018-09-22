//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "UmInstance.h"
#include "umm-internal.h"

void umm::UmInstance::SetArguments(const uint64_t argc,
                                   const char *argv[]) {
  // NOTE: Should we be referencing this type?
  // Shallow copy of boot info.
  bi = *(ukvm_boot_info *) argc;

  {
    // Make buffer for a deep copy.
    // Need to add 1 for null.
    char *tmp = (char *) malloc(strlen(bi.cmdline) + 1);
    // Do the copy.
    strcpy(tmp, bi.cmdline);
    // Swing ptr intentionally dropping old.
    bi.cmdline = tmp;
  }

  sv_.ef.rdi = (uint64_t) &bi;
  if (argv)
    sv_.ef.rsi = (uint64_t)argv;
}

void umm::UmInstance::Print() {
  kprintf("Number of pages allocated: %d\n", page_count);
  sv_.Print();
}

uintptr_t umm::UmInstance::GetBackingPage(uintptr_t vaddr) {
  auto vp_start_addr = Pfn::Down(vaddr).ToAddr();
  umm::Region& reg = sv_.GetRegionOfAddr(vaddr);
  reg.count++;

  // TODO(jmcadden): Support large pages per-region
  kassert(reg.page_order == 0);

  /* Allocate new physical page for the faulted region */
  Pfn backing_page = ebbrt::page_allocator->Alloc();
  page_count++;
  auto bp_start_addr = backing_page.ToAddr();

  // Check for a backing source
  if (reg.data != nullptr) {
    unsigned char *elf_src_addr;
    elf_src_addr = reg.data + reg.GetOffset(vp_start_addr);
    // Copy backing data onto the allocated page
    std::memcpy((void *)bp_start_addr, (const void *)elf_src_addr, kPageSize);
  } else if (reg.name == ".bss") {
    //   printf("Allocating a bss page\n");
    // Zero bss pages
    std::memset((void *)bp_start_addr, 0, kPageSize);
  }

  return bp_start_addr;
}

uintptr_t umm::UmInstance::GetBackingPageCOW(uintptr_t vaddr) {
  umm::Region& reg = sv_.GetRegionOfAddr(vaddr);
  reg.count++;

  // TODO(jmcadden): Support large pages per-region
  kassert(reg.page_order == 0);

  /* Allocate new physical page for the faulted region */
  Pfn backing_page = ebbrt::page_allocator->Alloc();
  page_count++;
  auto bp_start_addr = backing_page.ToAddr();

  // Copy data on that page. Remember vaddr is still old write protected page.
  kprintf_force(MAGENTA "Copy dst %p, src %p!" RESET, bp_start_addr, vaddr);
  std::memcpy((void *)bp_start_addr, (const void *)vaddr, kPageSize);

  return bp_start_addr;
}
