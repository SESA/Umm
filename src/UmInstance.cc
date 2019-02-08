//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "UmInstance.h"
#include "UmManager.h"
#include "UmProxy.h"
#include "umm-internal.h"

#include "UmPgTblMgr.h" // User page HACK XXX

#include "../ext/solo5/kernel/ebbrt/ukvm_guest.h"

// TOGGLE DEBUG PRINT  
#define DEBUG_PRINT_UMI  0

/** XXX: Takes a virtual address and length and marks the pages USER */ 
// TODO: Not this..
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

ebbrt::Future<umm::UmSV*> umm::UmInstance::SetCheckpoint(uintptr_t vaddr){
  kassert(snap_addr == 0);
  snap_addr = vaddr;
  snap_p= new ebbrt::Promise<umm::UmSV*>();
  return snap_p->GetFuture();
}

void umm::UmInstance::RegisterPort(uint16_t port){
   umm::proxy->RegisterInternalPort(Id(), port);
   src_ports_.emplace_back(port); 
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


void umm::UmInstance::Block(size_t ns){
  if (timer_set) {
    kabort("Instance attempted to block with a timer already set");
  }
  if (!ns) {
    return;
  }
  // Set timer and deactivate
  auto now = ebbrt::clock::Wall::Now();
  time_wait = now + std::chrono::nanoseconds(ns);
  enable_timer(now);
#if DEBUG_PRINT_UMI
  kprintf(RED "C%dU%d:B<%u> " RESET, (size_t)ebbrt::Cpu::GetMine(),
                Id(), (ns / 1000));
#endif
  if( yield_flag_ )
  {
    umm::manager->SignalYield(Id());
  }
  Deactivate();
  // Woke up!
  disable_timer();
}

void umm::UmInstance::Activate(){
  kassert(!active_);
  kassert(context_);
#if DEBUG_PRINT_UMI
  kprintf_force("C%dU%d:SIG_UP " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
#endif
  active_ = true;
  ebbrt::event_manager->ActivateContext(std::move(*context_));
}

void umm::UmInstance::Deactivate() {
  kassert(active_);
  active_ = false;
  context_ = new ebbrt::EventManager::EventContext();
#if DEBUG_PRINT_UMI
  kprintf_force( "C%dU%d:DWN " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
#endif
  /* Instance is about to blocked */ 
  ebbrt::event_manager->SaveContext(*context_);
  /* ... */
  /* Now we are re-activated. Check with the core to see if we can resume */
  if (umm::manager->RequestActivation(Id()) == false) {
    // We're raced between activating this instance and halting it/scheduling it out 
#if DEBUG_PRINT_UMI
    kprintf_force(CYAN "C%dU%d:UP? " RESET,
            (size_t)ebbrt::Cpu::GetMine(), Id());
#endif
    resume_flag_ = true;
    Deactivate();  // This possibly blocks forever
  }
#if DEBUG_PRINT_UMI
  kprintf_force( "C%dU%d:UP " RESET, (size_t)ebbrt::Cpu::GetMine(),
          Id());
#endif
  resume_flag_ = false;
  kbugon(umm::manager->ActiveInstanceId() != Id());
}

bool umm::UmInstance::Yieldable() { return (yield_flag_ && !resume_flag_); }

void umm::UmInstance::EnableYield() {
  if(!yield_flag_){
#if DEBUG_PRINT_UMI
    kprintf_force(CYAN "C%dU%d:YON " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
#endif
    yield_flag_ = true;
  }
  //XXX: what about resume_flag_?
}

void umm::UmInstance::DisableYield() {
  if(yield_flag_){
#if DEBUG_PRINT_UMI
    kprintf_force(CYAN "C%dU%d:YOFF " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
#endif
    yield_flag_ = false;
  }
}

void umm::UmInstance::enable_timer(ebbrt::clock::Wall::time_point now) {
  if (timer_set || (now >= time_wait)) {
    return;
  }
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(time_wait - now);
  ebbrt::timer->Start(*this, duration, /* repeat = */ false);
  timer_set = true;
}

void umm::UmInstance::disable_timer() {
  if (timer_set) {
    ebbrt::timer->Stop(*this);
  }
  timer_set = false;
  time_wait = ebbrt::clock::Wall::time_point(); // clear timer
}

void umm::UmInstance::Fire() {
  kassert(timer_set);
  timer_set = false;
#if DEBUG_PRINT_UMI
  kprintf_force(YELLOW "U%d:F " RESET, Id());
#endif
  auto now = ebbrt::clock::Wall::Now();
  // If we reached the time_blocked period then unblock the execution
  if (time_wait != ebbrt::clock::Wall::time_point() && now >= time_wait) {
    time_wait = ebbrt::clock::Wall::time_point(); // clear the time
    /* Request activation */
    if (umm::manager->RequestActivation(Id())) {
      Activate();
    }
  }
}

void umm::UmInstance::Print() {
  kprintf_force("Number of pages allocated: %d\n", page_count);
  sv_.Print();
}

uintptr_t umm::UmInstance::GetBackingPage(uintptr_t v_pg_start, bool cow) {
  umm::Region& reg = sv_.GetRegionOfAddr(v_pg_start);
  {
    reg.count++;
    // TODO(jmcadden): Support large pages per-region
    kassert(reg.page_order == 0);
  }

  /* Allocate new physical page for the faulted region */
  uintptr_t bp_start_addr;
  uintptr_t vp_start_addr;
  {
    Pfn backing_page = ebbrt::page_allocator->Alloc();
    kbugon(backing_page == Pfn::None());
    page_count++;

    bp_start_addr = backing_page.ToAddr();
    vp_start_addr = Pfn::Down(v_pg_start).ToAddr();
  }

  if(cow){
    // Copy on write case.
    std::memcpy((void *)bp_start_addr, (const void *)v_pg_start, kPageSize);
    return bp_start_addr;
  }

  // Check for a backing source
  if (reg.data != nullptr) {
    unsigned char *elf_src_addr = reg.data + reg.GetOffset(vp_start_addr);
    // Copy backing data onto the allocated page
    std::memcpy((void *)bp_start_addr, (const void *)elf_src_addr, kPageSize);
  } else if (reg.name == ".bss") {
    // Zero bss pages
    std::memset((void *)bp_start_addr, 0, kPageSize);
  }

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
