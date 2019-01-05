//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "UmInstance.h"
#include "umm-internal.h"

#include "UmPgTblMgr.h" // User page HACK XXX

#include "../ext/solo5/kernel/ebbrt/ukvm_guest.h"

void hackSetPgUsr(uintptr_t vaddr, int bytes){
  // HACK: Setting the whole page user accessible. Totally unacceptable. XXX

  // Make sure on single.
  kassert(vaddr >> 21 == (vaddr + bytes) >> 21);


  // print path to page
  umm::lin_addr la; la.raw = vaddr;
  umm::simple_pte *cr3 = umm::UmPgTblMgmt::getPML4Root();
  // umm::UmPgTblMgmt::dumpAllPTEsWalkLamb(la, cr3, PML4_LEVEL);

  // set user  this flushes.
  umm::UmPgTblMgmt::setUserAllPTEsWalkLamb(la, cr3, PML4_LEVEL);

  // print path to page
  // umm::UmPgTblMgmt::dumpAllPTEsWalkLamb(la, cr3, PML4_LEVEL);

}

void umm::UmInstance::SetArguments(const uint64_t argc,
                                   const char *argv[]) {
  // NOTE: Should we be referencing this type?
  // Shallow copy of boot info.
  bi = malloc(sizeof(ukvm_boot_info));
  // printf(RED "Boot info at %p\n" RESET, bi);
  hackSetPgUsr((uintptr_t)bi, sizeof(ukvm_boot_info));

  auto kvm_args = (ukvm_boot_info *) argc;
  std::memcpy(bi, (const void *)kvm_args, sizeof(ukvm_boot_info));
  {
    auto bi_v = (ukvm_boot_info*)bi;
    // Make buffer for a deep copy.
    // Need to add 1 for null.
    char *tmp = (char *) malloc(strlen(bi_v->cmdline) + 1);
    // printf(RED "cmdline at %p\n" RESET, tmp);
    hackSetPgUsr((uintptr_t)tmp, strlen(bi_v->cmdline) + 1);

    // Do the copy.
    strcpy(tmp, bi_v->cmdline);
    // Swing ptr intentionally dropping old.
    bi_v->cmdline = tmp;
  }

  sv_.ef.rdi = (uint64_t) bi;
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
  kbugon(backing_page == Pfn::None());
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
  kbugon(backing_page == Pfn::None());
  page_count++;
  auto bp_start_addr = backing_page.ToAddr();

  // Copy data on that page. Remember vaddr is still old write protected page.
  // kprintf_force(MAGENTA "Copy dst %p, src %p!\n" RESET, bp_start_addr, vaddr);
  std::memcpy((void *)bp_start_addr, (const void *)vaddr, kPageSize);

  return bp_start_addr;
}
