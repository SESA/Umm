//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmManager.h"

#include <ebbrt/native/VMemAllocator.h>
#include <atomic>

extern "C" void ebbrt::idt::DebugException(ExceptionFrame* ef) {
  kprintf(MAGENTA "Umm... Taking a snapshot\n" RESET);
  // Set resume flag to prevent infinite retriggering of exception
  ef->rflags |= 1 << 16;

  umm::manager->process_checkpoint(ef);
}

extern "C" void ebbrt::idt::BreakpointException(ExceptionFrame* ef) {
  umm::manager->process_resume(ef);
}

void umm::UmManager::Init() {
  // Setup multicore Ebb translation
  Create(UmManager::global_id);
  // Reserve virtual region for slot and setup a fault handler 
  auto hdlr = std::make_unique<PageFaultHandler>();
  ebbrt::vmem_allocator->AllocRange(kSlotPageLength, kSlotStartVAddr,
                                    std::move(hdlr));
}

void umm::UmManager::process_resume(ebbrt::idt::ExceptionFrame *ef){
  if (status() == running) {
    // If the instance is running this exception is treated as an exit to
    // restore context of the client who called Start()
    *ef = caller_restore_frame_;
    set_status(finished);
    // TODO(tommyu): else if status() = loaded
  } else {
    // If this context is not already running we treat this entry an jump into
    // the instance. Backup the restore_frame (context) of the client and modify
    // the existing frame to "return" back into the instance
    caller_restore_frame_ = *ef;
    ef->rip = umi_->sv_.ef.rip;
    ef->rdi = umi_->sv_.ef.rdi;
    ef->rsi = umi_->sv_.ef.rsi;

    // TODO(us): Confirm what's going on here.
    if( umi_->sv_.ef.rbp )
      ef->rbp = umi_->sv_.ef.rbp;
    if( umi_->sv_.ef.rsp )
      ef->rsp = umi_->sv_.ef.rsp;

    set_status(running);
  }
}

void umm::UmManager::UmmStatus::set(umm::UmManager::Status new_status) {
  auto now = ebbrt::clock::Wall::Now();
  switch (new_status) {
  case empty:
    if (s_ > loaded) // Don't unload if running or more
      break;
    goto OK;
  case loaded:
    if (s_ != empty) // Only load when empty
      break;
    goto OK;
  case running:
    if (s_ != loaded && s_ != snapshot && s_ != blocked) 
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
    if (s_ != running) // Only finish when running
      break;
    runtime_ +=
        std::chrono::duration_cast<std::chrono::milliseconds>(now - clock_).count();
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

  auto snap_sv = new UmSV();
  snap_sv->ef = *ef;

  // Populate region list.
  for (const auto &reg : umi_->sv_.region_list_) {
    UmSV::Region r = reg;
    snap_sv->AddRegion(r);
  }

  // Copy all dirty pages into new page table.
  snap_sv->pth.copyInPages(getSlotPDPTRoot());

  // Save the snapshot and resume execution of the instance
  umi_snapshot_.SetValue(*snap_sv);
  set_status(running);
}

void umm::UmManager::PageFaultHandler::HandleFault(ExceptionFrame *ef,
                                                   uintptr_t addr) {
  umm::manager->process_pagefault(ef, addr);
}


void umm::UmManager::process_pagefault(ExceptionFrame *ef, uintptr_t vaddr) {
  if(status() == snapshot)
    kprintf(RED "Umm... Snapshot Pagefault\n" RESET);

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

    // "Install"
    slotRoot->tableOrFramePtrToPte(pdpt);

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
    (root + kSlotPML4Offset)->pageTabEntToAddr(PML4_LEVEL) .raw;
}

void umm::UmManager::setSlotPDPTRoot(umm::simple_pte* newRoot){
  kassert(newRoot != nullptr);
  (UmPgTblMgmt::getPML4Root()+ kSlotPML4Offset)->tableOrFramePtrToPte(newRoot);
}

void umm::UmManager::Load(std::unique_ptr<UmInstance> umi) {
  // Better not have a loaded root.
  simple_pte *pdptRoot = getSlotPDPTRoot();
  kassert(pdptRoot == nullptr);

  // If we have a vaild pth root, install it.
  auto pthRoot = umi->sv_.pth.Root();
  if(pthRoot != nullptr){
    printf("Installing instance pte root.\n");
    setSlotPDPTRoot(pthRoot);
    pdptRoot = getSlotPDPTRoot();
    kassert(pdptRoot != nullptr);
  }

  // Otherwise leave it 0 to be populated during 1st page fault.

  set_status(loaded);
  umi_ = std::move(umi);
}

std::unique_ptr<umm::UmInstance> umm::UmManager::Unload() {
  simple_pte *pdptRoot = getSlotPDPTRoot();
  kassert(pdptRoot != nullptr);

  // Clear slot PTE.
  pdptRoot->clearPTE();

  set_status(empty);

  auto tmp_um = std::move(umi_);
  return std::move(tmp_um);
}

void umm::UmManager::Start() { 
  if (status() == loaded)
    kprintf_force(GREEN "\nUmm... Kicking off the instance on core #%d\n" RESET,
                  (size_t)ebbrt::Cpu::GetMine());
  trigger_entry_exception();
  kprintf_force(GREEN "Umm... Returned from the instance on core #%d (%dms)\n" RESET,
                (size_t)ebbrt::Cpu::GetMine(), status_.time());
  //umi_->Print();
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
  return umi_snapshot_.GetFuture();
}

void umm::UmManager::Fire(){
  kassert(timer_set);
  timer_set = false;
  // We take a single clock reading which we use to simplify some corner cases
  // with respect to enabling the timer. This way there is a single time point
  // when this event occurred and all clock computations can be relative to it.
  auto now = ebbrt::clock::Wall::Now();

  // If we reached the time_blocked period then unblock the execution 
  if (time_wait != ebbrt::clock::Wall::time_point() && now >= time_wait) {
    time_wait = ebbrt::clock::Wall::time_point(); // clear the time

    // Unblock instance
    status_.set(running);
    ebbrt::event_manager->ActivateContext(std::move(*context_));
  }
  //kabort("UmManager timeout error: blocking indefinitley\n");
}

void umm::UmManager::Block(size_t ns){
  kassert(!timer_set);
  if(!ns)
    return;

  auto now = ebbrt::clock::Wall::Now();
  time_wait = now + std::chrono::nanoseconds(ns);
  SetTimer(now);
  status_.set(blocked);
  context_ = new ebbrt::EventManager::EventContext();
  ebbrt::event_manager->SaveContext(*context_);
}

void umm::UmManager::SetTimer(ebbrt::clock::Wall::time_point now){
  if (timer_set)
    return;

  if (now >= time_wait) {
    return;
  }
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(time_wait - now);
  ebbrt::timer->Start(*this, duration, /* repeat = */ false);
  timer_set = true;
}

void umm::UmManager::DisableTimers(){ 
  if (timer_set) {
    ebbrt::timer->Stop(*this);
  }
  timer_set = false;
  time_wait = ebbrt::clock::Wall::time_point(); // clear timer
}
