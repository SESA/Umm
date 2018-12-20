//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "UmInstance.h"
#include "UmManager.h"
#include "umm-internal.h"

// TODO: this feels bad.
#include "../ext/solo5/kernel/ebbrt/ukvm_guest.h"

void umm::UmInstance::SetArguments(const uint64_t argc,
                                   const char *argv[]) {
  // NOTE: Should we be referencing this type?
  // Shallow copy of boot info.
  bi = malloc(sizeof(ukvm_boot_info));
  auto kvm_args = (ukvm_boot_info *) argc;
  std::memcpy(bi, (const void *)kvm_args, sizeof(ukvm_boot_info));
  {
    auto bi_v = (ukvm_boot_info*)bi;
    // Make buffer for a deep copy.
    // Need to add 1 for null.
    char *tmp = (char *) malloc(strlen(bi_v->cmdline) + 1);
    // Do the copy.
    strcpy(tmp, bi_v->cmdline);
    // Swing ptr intentionally dropping old.
    bi_v->cmdline = tmp;
  }

  sv_.ef.rdi = (uint64_t) bi;
  if (argv)
    sv_.ef.rsi = (uint64_t)argv;
}

ebbrt::Future<umm::UmSV*> umm::UmInstance::SetCheckpoint(uintptr_t vaddr){
  kassert(snap_addr == 0);
  snap_addr = vaddr;
  snap_p= new ebbrt::Promise<umm::UmSV*>();
  return snap_p->GetFuture();
}

void umm::UmInstance::WritePacket(std::unique_ptr<ebbrt::IOBuf> buf){
  umi_recv_queue_.emplace(std::move(buf));
}

std::unique_ptr<ebbrt::IOBuf> umm::UmInstance::ReadPacket(){
  if(!HasData())
    return nullptr;
  auto buf = std::move(umi_recv_queue_.front());
  umi_recv_queue_.pop();
  return std::move(buf);
}

void umm::UmInstance::Activate(){
  kassert(!active_);
  kassert(context_);
  // ebbrt::event_manager->ActivateContextSync(std::move(*context_));
  active_ = true;
  ebbrt::event_manager->ActivateContext(std::move(*context_));
}

void umm::UmInstance::Deactivate() {
  kassert(active_);
  active_ = false;
  context_ = new ebbrt::EventManager::EventContext();
  kprintf(YELLOW "%u" RESET, id_);
  ebbrt::event_manager->SaveContext(*context_);
  kprintf(GREEN "%u" RESET, id_);
}

void umm::UmInstance::EnableYield() {
  if(!yield_flag_){
    kprintf(CYAN "UMI #%d yield enabled\n" RESET, id_);
    yield_flag_ = true;
  }
}

void umm::UmInstance::DisableYield() {
  if(yield_flag_){
    kprintf(CYAN "UMI #%d yield disabled\n" RESET, id_);
    yield_flag_ = false;
  }
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
  // kprintf_force(MAGENTA "Copy dst %p, src %p!" RESET, bp_start_addr, vaddr);
  std::memcpy((void *)bp_start_addr, (const void *)vaddr, kPageSize);

  return bp_start_addr;
}

void umm::UmInstance::logFault(x86_64::PgFaultErrorCode ec){
  pfc.pgFaults++;
  // Write or Read?
  if(ec.WR){
    if(ec.P){
      // If present, COW
      pfc.cowFaults++;
    }else{
      pfc.wrFaults++;
    }
  } else {
    pfc.rdFaults++;
  }
}

void umm::UmInstance::PgFtCtrs::dump_ctrs(){
  kprintf_force("total: %lu\n", pgFaults);
  kprintf_force("rd:    %lu\n", rdFaults);
  kprintf_force("wr:    %lu\n", wrFaults);
  kprintf_force("cow:   %lu\n", cowFaults);
}
