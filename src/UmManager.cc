//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmManager.h"
// TODO: Delete after debug.
#include "UmPgTblMgr.h"
#include "UmProxy.h"
#include "UmRegion.h"
#include "UmSyscall.h"
#include "umm-internal.h"

#include <ebbrt/native/VMemAllocator.h>
#include <atomic>

uintptr_t umm::UmManager::GetCallerStack(){
  return caller_restore_frame_.rsp;
}

umm::UmManager::UmManager(){
#ifdef USE_SYSCALL
  // Instrument gdt with user segments.
  umm::syscall::addUserSegments();
  // init syscall extensions and MSRs.
  umm::syscall::enableSyscallSysret();

  // Don't fault during fn exec.
  umm::proxy->UmClearData();
#endif
}

extern "C" void ebbrt::idt::DebugException(ExceptionFrame* ef) {
  kprintf(MAGENTA "Umm... Taking a snapshot!!!\n" RESET);
  // Set resume flag to prevent infinite retriggering of exception
  ef->rflags |= 1 << 16;

  umm::manager->process_checkpoint(ef);
}

extern "C" void ebbrt::idt::BreakpointException(ExceptionFrame* ef) {
  umm::manager->process_gateway(ef);
}

void umm::UmManager::Init() {
  // Setup multicore Ebb translation
  Create(UmManager::global_id);
  
  // Initialize the UmProxy Ebb
  UmProxy::Init();
  
  // Reserve virtual region for slot and setup a fault handler 
  auto hdlr = std::make_unique<PageFaultHandler>();
  ebbrt::vmem_allocator->AllocRange(kSlotPageLength, kSlotStartVAddr,
                                    std::move(hdlr));
}

int myround = 0;
ebbrt::idt::ExceptionFrame oldef;

void debugKickoff(ebbrt::idt::ExceptionFrame *ef){
  myround++;
  // if first round, store old ef
  if(myround == 1)
    oldef = *ef;
  // else if(myround == 2)
  //   *ef = myef;
  // else
  // kabort();

  else if (myround == 2) {

    uint64_t *oldEfPtr = (uint64_t *)&oldef;
    uint64_t *curEfPtr = (uint64_t *)ef;
    kprintf("old %p \t\t, current %p\n", oldEfPtr, curEfPtr);
    for (unsigned int i = 0; i < sizeof(ebbrt::idt::ExceptionFrame) / 8; i++) {
      uint64_t old_reg = oldEfPtr[i];
      uint64_t current_reg = curEfPtr[i];

      if (old_reg == current_reg)
        kprintf(GREEN);
      else
        kprintf(RED);

      kprintf("[%d] %p\t%p\n", i, old_reg, current_reg);
      kprintf(RESET);
    }
  }
  else
    kabort();
}
#if 0
uintptr_t hackyRIP;
// HACK

void primordial_sysret(){
  // How we first get into userspace.
  // We're after iret, not on any real stack.
  // We squirreled the entry point into RSP.

  // __asm__ __volatile__("movq %rsp, %rcx"); //RIP loaded from RCX

  __asm__ __volatile__("mov %0, %%rcx" ::"r"(hackyRIP));


  // HACK: This is some value that appears to work.
  // Might want to consider what it means.
  // __asm__ __volatile__("movq 0x46, %r11"); // R11 is moved into flags.
  // uint64_t rflags = 0x46; // Read using GDB
  __asm__ __volatile__("movq %0, %%r11" ::"r"(0x2ULL));


  // HACK: Software swings RSP. This is a bullshit val that might overwirte
  // Translation mem when it underflows. We've seen it work in the past.
  // During Rump kernel boot, this is swung to our correct "Heap" region.
  // TODO: At least swing to user region.
  __asm__ __volatile__("movq %0, %%rsp" ::"r"((uintptr_t) umm::kSlotEndVAddr & ~0xfULL));

  // To usermode we go!
  __asm__ __volatile__("sysretq");
  // Never return here
  kabort("Returning from impossible point\n");
}
#endif
void umm::UmManager::process_gateway(ebbrt::idt::ExceptionFrame *ef){
  // This is the enter / exit point for the function execution.
  // If the core is in the loaded position, we enter, if alerady running, we exit.

  auto stat = status();

  // Loaded, ready to start running.
  if (stat == loaded) {

    // Store the runSV() frame for when done SV execution.
    caller_restore_frame_ = *ef;

    // Overwrite exception frame from sv, setup by loader / setArguments().
    *ef = umi_->sv_.ef;

    set_status(running);

#ifdef USE_SYSCALL
    // Config gdt segments for user.
    ef->ss = (3 << 3) | 3;
    ef->cs = (4 << 3) | 3;

    // New execution sets rsp. Redeploy doesn't.
    if(ef->rsp == 0)
      ef->rsp = 0xFFFFC07FFFFFFFF0;

#endif
    return;
  }

  // If running, snapshot or blocked, this entry is treated as a halt
  // Switch back to the runSV() caller stack.
  if (stat == running || stat == blocked || stat == snapshot) {
    *ef = caller_restore_frame_;
    set_status(finished);
    return;
  }

  kprintf_force("Trying to enter / exit from invalid state, %d\n", stat);
  kabort();

}

void umm::UmManager::UmmStatus::set(umm::UmManager::Status new_status) {
  // printf("Making state change %d->%d\n", s_, new_status);
  auto now = ebbrt::clock::Wall::Now();
  switch (new_status) {
  case empty:
    if (s_ > loaded && s_ != finished) // Don't unload if running or more
      break;
    runtime_ = 0;
    goto OK;
  case loaded:
    if (s_ != empty) // Only load when empty
      break;
    goto OK;
  case running:
    if (s_ != loaded && s_ != snapshot && s_ != blocked && s_)
      break;
    clock_ = ebbrt::clock::Wall::Now(); 
    goto OK;
  case blocked:
    if (s_ != running ) // Only block when running
      break;
    // Log execution time before blocking. We'll resume the clock when running
    runtime_ +=
        std::chrono::duration_cast<std::chrono::milliseconds>(now - clock_).count();
    goto OK;
  case snapshot:
    if (s_ != running) // Only snapshot when running <??
      break;
    // Log execution time before taking snapshot. We'll resume the clock when running
    runtime_ +=
        std::chrono::duration_cast<std::chrono::milliseconds>(now - clock_).count();
    goto OK;
  case finished:
    if (s_ == running) {
      runtime_ +=
          std::chrono::duration_cast<std::chrono::milliseconds>(now - clock_)
              .count();
    }
    goto OK;
  default:
    break;
  }
  kabort("Invalid status change %d->%d ", s_, new_status);
OK:
  s_ = new_status;
}

void umm::UmManager::process_checkpoint(ebbrt::idt::ExceptionFrame *ef){
  kassert(status() != snapshot);
  // ebbrt::kprintf_force(CYAN "Snapshotting, core %d \n" RESET, (size_t) ebbrt::Cpu::GetMine());
  pfc.dump_ctrs();
  set_status(snapshot);

  UmSV* snap_sv = new UmSV();
  snap_sv->ef = *ef;

  // Populate region list.
  // HACK: use a assignment operator.
  for (const auto &reg : umi_->sv_.region_list_) {
    Region r = reg;
    snap_sv->AddRegion(r);
  }

  // Copy all dirty pages into new page table.
  snap_sv->pth.copyInPages(getSlotPDPTRoot());


  // Save the snapshot and resume execution of the instance
  // This will synchronously execute a future->Then( lambda ) 
  // ebbrt::kprintf_force(CYAN "Have snapshot, it has pages\n" RESET, (size_t) ebbrt::Cpu::GetMine());

  // {
  //   std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  //   UmPgTblMgmt::countValidWritePagesLamb(counts, snap_sv->pth.Root(), 3);
  //   for (int i = 4; i > 0; i--) {
  //     kprintf_force(YELLOW "counts[%s] = %lu\n" RESET, level_names[i],
  //                   counts[i]);
  //   }
  // }

  umi_snapshot_->SetValue(snap_sv);
  set_status(running);
}

void umm::UmManager::PageFaultHandler::HandleFault(ExceptionFrame *ef,
                                                   uintptr_t addr) {
  umm::manager->process_pagefault(ef, addr);
}

void umm::UmManager::logFaults(x86_64::PgFaultErrorCode ec){

  // Green if present, else red.
  // if(ec.P){
  //   kprintf_force(GREEN);
  // }else{
  //   kprintf_force(RED);
  // }

  pfc.pgFaults++;

  // Write or Read?
  if(ec.WR){
    if(ec.P){
      // Present, COW
      // kprintf_force(CYAN "W");
      pfc.cowFaults++;
    }else{
      pfc.wrFaults++;
      // kprintf_force(RED "W");
    }
  } else {
    pfc.rdFaults++;
    // kprintf_force(YELLOW "R");

  }

  // if(ec.ID){
  //   kprintf_force(GREEN "I");
  // }

  // kprintf_force(RESET);
}

void errorCodePrinter(uintptr_t vaddr, x86_64::PgFaultErrorCode ec) {
  kprintf_force(MAGENTA "fault addr is %p, err: %x ", vaddr, ec.val);
  if (ec.P) {
    kprintf_force("[Pres] ");
  }
  if (ec.WR) {
    kprintf_force("[Write ] ");
  }
  if (ec.US) {
    kprintf_force("[User ] ");
  }
  if (ec.RES0) {
    kprintf_force("[Res bits ] ");
  }
  if (ec.ID) {
    kprintf_force("[Ins Fetch ] ");
  }
  kprintf_force("\n" RESET);
}

void printPTWalk(uintptr_t virt, umm::simple_pte* root, unsigned char lvl){
  umm::lin_addr la;
  la.raw = virt;
  umm::UmPgTblMgmt::dumpAllPTEsWalkLamb(la, root, lvl);
}

void umm::UmManager::process_pagefault(ExceptionFrame *ef, uintptr_t vaddr) {

  kassert(status() != empty);
  kassert(valid_address(vaddr));
  if(status() == snapshot)
    kprintf_force(RED "Umm... Snapshot Pagefault\n" RESET);

  x86_64::PgFaultErrorCode ec;
  ec.val = ef->error_code;
  logFaults(ec);
  // errorCodePrinter(vaddr, ec);

  auto virtual_page = Pfn::Down(vaddr);
  auto virtual_page_addr = virtual_page.ToAddr();
  lin_addr virt;
  virt.raw = virtual_page_addr;


  /* Pass to the mounted sv to select/allocate the backing page */
  uintptr_t physical_start_addr = 0;
  if(ec.P == 1){
    // kprintf_force("Copy on write a page!\n");
    physical_start_addr = umi_->GetBackingPageCOW(virtual_page_addr);

  } else{
    physical_start_addr = umi_->GetBackingPage(virtual_page_addr);
  }
  kassert(physical_start_addr != 0);


  lin_addr phys;
  phys.raw = physical_start_addr;

  simple_pte* PML4Root = UmPgTblMgmt::getPML4Root();
  simple_pte* slotRoot = PML4Root + kSlotPML4Offset;

  bool writeFault = (ec.WR) ? true : false;
  // kprintf_force(" \(%#llx => %p)", vaddr, physical_start_addr);
  // kprintf_force("\t\t write fault? %s, error code %x\n", ec.WR ? "yes" : "no", ec.val );

  if (slotRoot->raw == 0) {
    // No existing slotRoot entry.
    auto pdpt = UmPgTblMgmt::mapIntoPgTbl(nullptr, phys, virt, PDPT_LEVEL,
                                          TBL_LEVEL, PDPT_LEVEL, writeFault);

    // "Install". Set accessed in case a walker strides accessed pages.
    // Mark PML4 Ent User.
    slotRoot->setPte(pdpt, false, true, true, true);

  } else {
    // Slot root holds ptr to sub PT.
    UmPgTblMgmt::mapIntoPgTbl((simple_pte *)slotRoot->pageTabEntToAddr(PML4_LEVEL).raw,
                              phys, virt, PDPT_LEVEL, TBL_LEVEL, PDPT_LEVEL, writeFault);
  }

  // NOTE: if we're in the COW case we have to flush the translation!!!
  if(ec.P){
    umm::UmPgTblMgmt::invlpg( (void*) virt.raw);
  }
  // printPTWalk(vaddr, UmPgTblMgmt::getPML4Root(), PML4_LEVEL);

}

umm::simple_pte* umm::UmManager::getSlotPDPTRoot(){
  // Root of slot.
  simple_pte *root = UmPgTblMgmt::getPML4Root();
  if(!UmPgTblMgmt::exists(root + kSlotPML4Offset)){
    return nullptr;
  }
  // TODO(tommyu): don't really need to deref.
  // HACK(tommyu): Fix this busted ass shit..
  return (simple_pte *)
    (root + kSlotPML4Offset)->pageTabEntToAddr(PML4_LEVEL).raw;
}

umm::simple_pte* umm::UmManager::getSlotPML4PTE(){
  // Root of slot.
  simple_pte *slotPML4 = UmPgTblMgmt::getPML4Root() + kSlotPML4Offset;
  return slotPML4;
}

void umm::UmManager::setSlotPDPTRoot(umm::simple_pte* newRoot){
  kassert(newRoot != nullptr);
  (UmPgTblMgmt::getPML4Root()+ kSlotPML4Offset)->setPte(newRoot, false, true, true, true);
  // (UmPgTblMgmt::getPML4Root()+ kSlotPML4Offset)->setPte(newRoot, false, true);
}

void umm::UmManager::Load(std::unique_ptr<UmInstance> umi) {
  pfc.zero_ctrs();
  // Better not have a loaded root.
  simple_pte *pdptRoot = getSlotPDPTRoot();
  kassert(pdptRoot == nullptr);

  // If we have a vaild pth root, install it.
  auto pthRoot = umi->sv_.pth.Root();
  if(pthRoot != nullptr){
    kprintf("Installing instance pte root.\n");
    setSlotPDPTRoot(pthRoot);
    pdptRoot = getSlotPDPTRoot();
    // kprintf("Slot root is %p\n", pdptRoot);
    kassert(pdptRoot != nullptr);
  }

  // Otherwise leave it 0 to be populated during 1st page fault.

  set_status(loaded);
  umi_ = std::move(umi);
}

std::unique_ptr<umm::UmInstance> umm::UmManager::Unload() {
  // ebbrt::kprintf_force(MAGENTA "Unloadin core %d \n" RESET, (size_t) ebbrt::Cpu::GetMine());
  // pfc.dump_ctrs();

  simple_pte *slotPML4Ent = umm::manager->getSlotPML4PTE();
  kassert(UmPgTblMgmt::exists(slotPML4Ent));

  // Clear slot PTE.

  // TODO, make sure page table is g2g or reaped.
  slotPML4Ent->clearPTE();

  // Modified page table, invalidate caches. This is confirmed to matter in virtualization.
  // UmPgTblMgmt::invlpg((void *) UmPgTblMgmt::getPML4Root()); // Think this is insufficient.
  UmPgTblMgmt::flushTranslationCaches();

  set_status(empty);

  kassert( ! UmPgTblMgmt::exists(slotPML4Ent));

  return std::move(umi_);
}

void umm::UmManager::runSV() {
  kassert(status() == loaded);
  kprintf(GREEN "Umm... Deploying SV on core #%d\n" RESET,
                (size_t)ebbrt::Cpu::GetMine());

  // NOTE: We transfer to sv execution here via the breakpoint exception!!!
  trigger_bp_exception();
  // NOTE: After Halt is called triggering it's own #BP we reload to this state.

  // Cleanup execution.
  // Clear proxy data
  proxy->UmClearData();
  DisableTimers();
  delete context_;
  context_ = nullptr;

}

void umm::UmManager::Halt() {
  kprintf(GREEN "Calling halt\n" RESET);
  
  if(ebbrt::event_manager->QueueLength()){
    kprintf_force(YELLOW "Attempting to clear (%d) pending events before halting...\n" RESET, ebbrt::event_manager->QueueLength());
    ebbrt::event_manager->SpawnLocal(
        [this]() {
          this->Halt();
        },
        true);
    return;
  }

  kassert(status() != empty);
  // TODO:This might be a little harsh in general, but useful for debugging.
  kprintf(GREEN "Umm... Returned from the instance on core #%d (%dms)\n" RESET,
                (size_t)ebbrt::Cpu::GetMine(), status_.time());
  trigger_bp_exception();
}

ebbrt::Future<umm::UmSV*> umm::UmManager::SetCheckpoint(uintptr_t vaddr){
  kassert(valid_address(vaddr));

  x86_64::DR7 dr7;
  x86_64::DR0 dr0;
  dr7.get();
  dr0.get();

  // Want to enable DR0 to break if instruction in app is executed.
  // DR7 configures on what condition accessing the data should cause excep.
  // DR0 holds the address we desire to break on.
  dr0.val = vaddr;
  dr0.set();
  // Intel 64 man vol 3 17.2.4 for details.
  // Local enable bit 0.
  dr7.L0 = 1;
  // Deassert bits 16 and 17 to break on instruction execution only.
  dr7.RW0 = 0;
  // Deassert bits 18,19 because other 3 options lead to undefined
  // behavior.
  dr7.LEN0 = 0;
  dr7.set();

  // TODO: leak
  umi_snapshot_ = new ebbrt::Promise<umm::UmSV*>();
  return umi_snapshot_->GetFuture();
}

void umm::UmManager::Fire(){
  // kprintf(RED "(F)" RESET);
  kassert(timer_set);
  timer_set = false;

  // If the instance is not blocked this timeout is stale, ignore it
  if(status() != blocked)
    return;

  if(context_ == nullptr)
    return;

  // We take a single clock reading which we use to simplify some corner cases
  // with respect to enabling the timer. This way there is a single time point
  // when this event occurred and all clock computations can be relative to it.
  auto now = ebbrt::clock::Wall::Now();

  // If we reached the time_blocked period then unblock the execution 
  if (time_wait != ebbrt::clock::Wall::time_point() && now >= time_wait) {
    time_wait = ebbrt::clock::Wall::time_point(); // clear the time

    // Unblock instance
    status_.set(running);
    // kprintf("Activating context, block ctr:%d\n", block_ctr_);
    // ebbrt::event_manager->ActivateContextSync(std::move(*context_));
    ebbrt::event_manager->ActivateContext(std::move(*context_));
    // kprintf("Post activate, block ctr:%d\n", block_ctr_);
  }
  // kprintf(RED "(FR)" RESET);
  // kabort("UmManager timeout error: blocking indefinitley\n");
}

void umm::UmManager::Block(size_t ns){
  // Maybe just clobber old timer?
  kassert(!timer_set);
  if(!ns){
    kprintf(RED "0" RESET);
    return;
  }

  auto now = ebbrt::clock::Wall::Now();
  time_wait = now + std::chrono::nanoseconds(ns);
  SetTimer(now);
  status_.set(blocked);
  context_ = new ebbrt::EventManager::EventContext();
  // int current_bc = block_ctr_;
  block_ctr_++;
  // kprintf(RED "Context saved: %d\n", current_bc);
  ebbrt::event_manager->SaveContext(*context_);
  // kprintf(RED "Context restored...\n");
  // kprintf(RED "Context restored: %d\n", current_bc);
}

void umm::UmManager::SetTimer(ebbrt::clock::Wall::time_point now){
  if (timer_set){
    // kprintf(RED "T" RESET);
    return;
  }

  if (now >= time_wait) {
    // kprintf(YELLOW "T" RESET);
    return;
  }

  // kprintf(GREEN "T" RESET);
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(time_wait - now);
  ebbrt::timer->Start(*this, duration, /* repeat = */ false);
  timer_set = true;
}

void umm::UmManager::DisableTimers(){ 
  if (timer_set) {
    ebbrt::timer->Stop(*this);
  }
  // kprintf(RED "Disable timers....\n" RESET);
  timer_set = false;
  time_wait = ebbrt::clock::Wall::time_point(); // clear timer
}

void umm::UmManager::PgFtCtrs::zero_ctrs(){
  pgFaults = 0;
  rdFaults = 0;
  wrFaults = 0;
  cowFaults = 0;
}
void umm::UmManager::PgFtCtrs::dump_ctrs(){
  kprintf_force("total: %lu\n", pgFaults);
  kprintf_force("rd:    %lu\n", rdFaults);
  kprintf_force("wr:    %lu\n", wrFaults);
  kprintf_force("cow:   %lu\n", cowFaults);
}
