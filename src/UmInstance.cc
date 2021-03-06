//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "util/x86_64.h"
#include "UmInstance.h"
#include "UmManager.h"
#include "UmProxy.h"
#include "umm-internal.h"

#include "UmPgTblMgr.h" // User page HACK XXX

#include "../ext/solo5/kernel/ebbrt/ukvm_guest.h"

// TOGGLE DEBUG PRINT  
#define DEBUG_PRINT_UMI  0

namespace{
  std::atomic<uint32_t> umi_id_next_{1}; // UMI id counter
}

umm::UmInstance::UmInstance(const umm::UmSV &sv) : sv_(sv) {
  id_ = ++umi_id_next_;
};

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
  // set active?
  umm::manager->SignalResume(Id());
  umi_recv_queue_.emplace(std::move(buf));
}

std::unique_ptr<ebbrt::IOBuf> umm::UmInstance::ReadPacket(size_t len) {
  if(!HasData())
    return nullptr;

  if(umi_recv_queue_.front()->ComputeChainDataLength() <= len){
    auto buf = std::move(umi_recv_queue_.front());
    umi_recv_queue_.pop();
    return std::move(buf);
  }else{
    auto dp = umi_recv_queue_.front()->GetDataPointer();
    auto buf = umm::UmProxy::raw_to_iobuf(dp.Data(), len);
    // Examine the front packet
    kprintf("BUF: solo_len=%d, len=%d, clen=%d cs=%d \n", len, buf->Length(),
            buf->ComputeChainDataLength(), buf->CountChainElements());
    umi_recv_queue_.front()->Advance(len);
    return std::move(buf);
  }

}


void umm::UmInstance::Sleep(size_t ns){
  if (timer_set) {
    kabort("Instance attempted to sleep with a timer already set");
  }
  if (!ns) {
    return;
  }

  // If the instance is active, we'll set a timer before yielding the core. If the
  // instance is inactive, no timer will be set and the UmManager will be signaled
  // to swap out the instance.
  if (IsActive()) {
    // About to go to sleep, set timer and deactivate
    auto now = ebbrt::clock::Wall::Now();
    time_wait = now + std::chrono::nanoseconds(ns);
    enable_timer(now);
#if DEBUG_PRINT_UMI
    kprintf_force(RED "C%dU%d:B<%u> " RESET, (size_t)ebbrt::Cpu::GetMine(), Id(),
            (ns / 1000));
#endif
  }

  //ebbrt::event_manager->SpawnLocal([this]() { umm::manager->Yield(); }, true);
  block_execution();
  // Woke up!
}

void umm::UmInstance::Kick(){
  /* TODO: We should simply "alert" the instance and let it decide
   * whether should happen next: boot, halt, unblock, etc. */

  // For now, try to unblock when kicked
  if (blocked_) {
    unblock_execution();
  }
}

void umm::UmInstance::unblock_execution(){
  kassert(context_);
#if DEBUG_PRINT_UMI
  kprintf_force(CYAN "C%dU%d:SIG_UP " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
#endif
  // WARNING: THIS IS AN ASYNCHRONOUS EVENT
  blocked_ = false;
  disable_timer(); // NOT SURE ABOUT THIS..
  ebbrt::event_manager->ActivateContext(std::move(*context_));
  // Return to caller
}

void umm::UmInstance::block_execution() {
  context_ = new ebbrt::EventManager::EventContext();
#if DEBUG_PRINT_UMI
  kprintf_force( "C%dU%d:DWN " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
#endif
  /* Instance is about to block */
  blocked_ = true; 
  ebbrt::event_manager->SaveContext(*context_); /* ... now blocked ...*/

  // WARNING: ebbrt::event_manager->ActivateContext is an ASYNCHRONOUS
  // operation, so
  // its possible that the state of the world has changed since the call was
  // made to unblock execution.

  /* OK! We're back! */

  context_ = nullptr;

  /* Check with the master to see if we can start */
  if (umm::manager->request_slot_entry(Id()) == false) {
    // Hmm... Looks like we am not able to start
#if DEBUG_PRINT_UMI
  if (timer_set) {
    kprintf_force(RED "C%dU%d:XXXT\n" RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
  }else{
    kprintf_force(RED "C%dU%d:XXX\n" RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
  }
#endif
    block_execution(); // XXX: No timer, it's up to the UmManager to restore us
  } else {
    kassert(umm::manager->ActiveInstanceId() == Id());
#if DEBUG_PRINT_UMI
  if (timer_set) {
    kprintf_force("C%dU%d:UP " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
  }else{
    kprintf_force("C%dU%d:UPT " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
  }
#endif
  }
  return; /* WARNING: Returns back into the slot address space */
}

void umm::UmInstance::SetActive() {
  if(!active_){
#if DEBUG_PRINT_UMI
    kprintf_force(GREEN "C%dU%d:ON " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
#endif
		active_ = true;
  }
}

void umm::UmInstance::SetInactive() {
  if(active_){
#if DEBUG_PRINT_UMI
    kprintf_force(GREEN "C%dU%d:OFF " RESET, (size_t)ebbrt::Cpu::GetMine(), Id());
#endif
    active_ = false;
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
    if (umm::manager->request_slot_entry(Id())) {
      Kick();
    }
  }
}

void umm::UmInstance::Print() {
  sv_.Print();
}

uintptr_t umm::UmInstance::GetBackingPage(uintptr_t v_pg_start, x86_64::PgFaultErrorCode ec) {

  // Consult region list.
  umm::Region& reg = sv_.GetRegionOfAddr(v_pg_start);
  {
    reg.count++;
    // TODO(jmcadden): Support large pages per-region
    kassert(reg.page_order == 0);
  }

  // Sanity check, should never wr fault to a read only section.
  // This catches writes to text, for example.
  if(ec.isWriteFault() && !reg.writable ){
    kprintf_force(RED "I'm confused, we just write faulted on read only section" RESET, v_pg_start);
    kprintf_force(RED "%s \n" RESET, reg.name.c_str());
    while(1);
  }

  // If this is a fault on text or read only data, we don't allocate a page, simply map from elf.
  // if( reg.name == ".text" || reg.name == ".rodata"){
  if(!reg.writable){
    // If we're not in text or rodata, it's a surprise.
    if( ! ( reg.name == ".text" || reg.name == ".rodata") ){
      kprintf_force(RED "Surprised to find non writable region %s \n" RESET, reg.name.c_str());
      while(1);
    }

    // kprintf_force(RED "%s \n" RESET, reg.name.c_str());
    uintptr_t elf_pg_addr = (uintptr_t) (reg.data + reg.GetOffset(v_pg_start));
    // Must be 4k aligned.
    kassert(elf_pg_addr % (1<<12) == 0);
    return elf_pg_addr;
  }

  /* Allocate new physical page for the faulted region */
  uintptr_t bp_start_addr;
  {
    Pfn backing_page = ebbrt::page_allocator->Alloc();
    kbugon(backing_page == Pfn::None());
    bp_start_addr = backing_page.ToAddr();
  }

  // Copy on write condition.
  // We map the page in COW for 2 reasons:
  // 1) This could be a rd fault on data with a write to come later.
  // 2) When we free pages, we only free dirty pages,
  if(ec.isPresent() && ec.isWriteFault()){
    // Copy on write case.
    std::memcpy((void *)bp_start_addr, (const void *)v_pg_start, kPageSize);
    return bp_start_addr;
  }

  // Check for a backing source.
  if (reg.data != nullptr) {
    unsigned char *elf_src_addr = reg.data + reg.GetOffset(v_pg_start);
    // Copy backing data onto the allocated page
    std::memcpy((void *)bp_start_addr, (const void *)elf_src_addr, kPageSize);
  } else if (reg.name == ".bss" || reg.name == "usr" ) {
    // Zero bss or stack pages
    std::memset((void *)bp_start_addr, 0, kPageSize);
  } else {
    kabort("What other case is there?\n");
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

void umm::UmInstance::PgFtCtrs::zero_ctrs(){
  // Zero per region pfctrs.
  pgFaults = 0;
  rdFaults = 0;
  wrFaults = 0;
  cowFaults = 0;
}

void umm::UmInstance::ZeroPFCs(){
  // Zero region specific ctrs of sv.
  sv_.ZeroPFCs();
  // Zero aggregate instance ctrs.
  pfc.zero_ctrs();
}
