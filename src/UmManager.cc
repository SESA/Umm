//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmManager.h"
// TODO: Delete after debug.
#include "UmPgTblMgr.h"
#include "UmProxy.h"
#include "UmRegion.h"
#include "umm-internal.h"

#include <ebbrt/native/VMemAllocator.h>
#include <atomic>

extern "C" void ebbrt::idt::DebugException(ExceptionFrame* ef) {
  kprintf_force(MAGENTA "Umm... Taking a snapshot!!!\n" RESET);
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
  set_status(snapshot);

  kprintf(BLUE "Creating SV for snapshot!!!\n" RESET);
  auto snap_sv = new UmSV();
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
  umi_snapshot_->SetValue(*snap_sv);
  set_status(running);
}

void umm::UmManager::PageFaultHandler::HandleFault(ExceptionFrame *ef,
                                                   uintptr_t addr) {
  umm::manager->process_pagefault(ef, addr);
}


void umm::UmManager::process_pagefault(ExceptionFrame *ef, uintptr_t vaddr) {
  if(status() == snapshot)
    kprintf_force(RED "Umm... Snapshot Pagefault\n" RESET);
  kassert(status() != empty);
  kassert(valid_address(vaddr));

  auto virtual_page = Pfn::Down(vaddr);
  auto virtual_page_addr = virtual_page.ToAddr();
  lin_addr virt;
  virt.raw = virtual_page_addr;

  /* Pass to the mounted sv to select/allocate the backing page */
  auto physical_start_addr = umi_->GetBackingPage(virtual_page_addr);
  lin_addr phys;
  phys.raw = physical_start_addr;

  /* Map backing page into core's page tables */
  // Get PML4[0x180].
  // If 0, create new table on first mapping
  //   Update PML4[0x180]
  // Else, call map using.

  simple_pte* PML4Root = UmPgTblMgmt::getPML4Root();
  simple_pte* slotRoot = PML4Root + kSlotPML4Offset;

  if (slotRoot->raw == 0) {
    // No existing slotRoot entry.
    auto pdpt = UmPgTblMgmt::mapIntoPgTbl(nullptr, phys, virt, PDPT_LEVEL,
                                         TBL_LEVEL, PDPT_LEVEL);

    // "Install". Set accessed in case a walker strides accessed pages.
    slotRoot->setPte(pdpt, false, true);

  } else {
    // Slot root holds ptr to sub PT.
    UmPgTblMgmt::mapIntoPgTbl((simple_pte *)slotRoot->pageTabEntToAddr(PML4_LEVEL).raw,
                             phys, virt, PDPT_LEVEL, TBL_LEVEL, PDPT_LEVEL);
  }
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
  (UmPgTblMgmt::getPML4Root()+ kSlotPML4Offset)->setPte(newRoot, false, true);
}

void umm::UmManager::Load(std::unique_ptr<UmInstance> umi) {
  // Better not have a loaded root.
  simple_pte *pdptRoot = getSlotPDPTRoot();
  kassert(pdptRoot == nullptr);

  // If we have a vaild pth root, install it.
  auto pthRoot = umi->sv_.pth.Root();
  if(pthRoot != nullptr){
    kprintf("Installing instance pte root.\n");
    setSlotPDPTRoot(pthRoot);
    pdptRoot = getSlotPDPTRoot();
    kassert(pdptRoot != nullptr);
  }

  // Otherwise leave it 0 to be populated during 1st page fault.

  set_status(loaded);
  umi_ = std::move(umi);
}

std::unique_ptr<umm::UmInstance> umm::UmManager::Unload() {
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
  kprintf_force(GREEN "Umm... Deploying SV on core #%d\n" RESET,
                (size_t)ebbrt::Cpu::GetMine());
  trigger_bp_exception();
}

void umm::UmManager::Halt() {
  kprintf_force(GREEN "Calling halt\n" RESET);

  kassert(status() != empty);
  // TODO:This might be a little harsh in general, but useful for debugging.
  kprintf_force(GREEN "Umm... Returned from the instance on core #%d (%dms)\n" RESET,
                (size_t)ebbrt::Cpu::GetMine(), status_.time());
  // Clear proxy data
  proxy->UmClearData();
  DisableTimers();
  delete context_;
  context_ = nullptr;
  trigger_bp_exception();
}

ebbrt::Future<umm::UmSV> umm::UmManager::SetCheckpoint(uintptr_t vaddr){
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
  umi_snapshot_ = new ebbrt::Promise<umm::UmSV>();
  return umi_snapshot_->GetFuture();
}

void umm::UmManager::Fire(){
  kprintf(RED "(F)" RESET);
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
    kprintf("Activating context, block ctr:%d\n", block_ctr_);
    // ebbrt::event_manager->ActivateContextSync(std::move(*context_));
    ebbrt::event_manager->ActivateContext(std::move(*context_));
    kprintf("Post activate, block ctr:%d\n", block_ctr_);
  }
  kprintf(RED "(FR)" RESET);
  // kabort("UmManager timeout error: blocking indefinitley\n");
}

void umm::UmManager::Block(size_t ns){
  // Maybe just clobber old timer?
  kassert(!timer_set);
  if(!ns){
    kprintf_force(RED "0" RESET);
    return;
  }

  auto now = ebbrt::clock::Wall::Now();
  time_wait = now + std::chrono::nanoseconds(ns);
  SetTimer(now);
  status_.set(blocked);
  context_ = new ebbrt::EventManager::EventContext();
  int current_bc = block_ctr_;
  block_ctr_++;
  kprintf(RED "Context saved: %d\n", current_bc);
  ebbrt::event_manager->SaveContext(*context_);
  kprintf(RED "Context restored: %d\n", current_bc);

}

void umm::UmManager::SetTimer(ebbrt::clock::Wall::time_point now){
  if (timer_set){
    kprintf_force(RED "T" RESET);
    return;
  }

  if (now >= time_wait) {
    kprintf_force(YELLOW "T" RESET);
    return;
  }

  kprintf_force(GREEN "T" RESET);
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(time_wait - now);
  ebbrt::timer->Start(*this, duration, /* repeat = */ false);
  timer_set = true;
}

void umm::UmManager::DisableTimers(){ 
  if (timer_set) {
    ebbrt::timer->Stop(*this);
  }
  kprintf_force(RED "Disable timers....\n" RESET);
  timer_set = false;
  time_wait = ebbrt::clock::Wall::time_point(); // clear timer
}
