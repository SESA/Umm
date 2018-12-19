//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "UmInstance.h"
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

void umm::UmInstance::Fire() {
  kassert(timer_set);
  timer_set = false;
  if(context_ == nullptr) // Nothing to restore to..? 
    return;
  auto now = ebbrt::clock::Wall::Now();
  // If we've passed the time_blocked period then unblock the execution 
  if (time_wait != ebbrt::clock::Wall::time_point() && now >= time_wait) {
    time_wait = ebbrt::clock::Wall::time_point(); // clear the time
  }
  ebbrt::event_manager->ActivateContext(std::move(*context_));
}

void umm::UmInstance::Block(ebbrt::clock::Wall::time_point wake_time) {
  kassert(!timer_set);
  auto now = ebbrt::clock::Wall::Now();
  if (now >= wake_time) {
    // No need to block, the wake time has already passed
    return; 
  }
  time_wait  = wake_time;
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(wake_time - now);
  ebbrt::timer->Start(*this, duration, /* repeat = */ false);
  timer_set = true;
  context_ = new ebbrt::EventManager::EventContext();
  // This will block until the context is restored
  ebbrt::event_manager->SaveContext(*context_);
}

void umm::UmInstance::DisableTimers() {
  if (timer_set) {
    ebbrt::timer->Stop(*this);
  }
  time_wait = ebbrt::clock::Wall::time_point(); // clear timer
  timer_set = false;
}

umm::UmInstance::~UmInstance() {
  DisableTimers();
  kprintf_force(RED "Deleting UMI #%d\n" RESET,id_);
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
