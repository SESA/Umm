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


void umm::UmManager::process_gateway(ebbrt::idt::ExceptionFrame *ef){
  // This is the enter / exit point for the function execution.
  // If the core is in the loaded position, we enter, if alerady running, we exit.

  auto stat = status();

  // Loaded, ready to start running.
  if (stat == loaded) {

    // Store the runSV() frame for when done SV execution.
    active_umi_->caller_restore_frame_ = *ef;

    // Overwrite exception frame from sv, setup by loader / setArguments().
    *ef = active_umi_->sv_.ef;
    set_status(running);
    return;
  }

  // If running, snapshot or blocked, this entry is treated as a halt
  // Switch back to the runSV() caller stack.
  if (stat == running || stat == blocked || stat == snapshot) {
    *ef = active_umi_->caller_restore_frame_;
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

void umm::UmManager::signal_yield(){
  kassert(status() == running);
	yeild_instance_ = true;
}

void umm::UmManager::process_checkpoint(ebbrt::idt::ExceptionFrame *ef){
  kassert(status() != snapshot);
  // ebbrt::kprintf_force(CYAN "Snapshotting, core %d \n" RESET, (size_t) ebbrt::Cpu::GetMine());
  set_status(snapshot);

  UmSV* snap_sv = new UmSV();
  snap_sv->ef = *ef;

  // Populate region list.
  // HACK: use a assignment operator.
  for (const auto &reg : active_umi_->sv_.region_list_) {
    Region r = reg;
    snap_sv->AddRegion(r);
  }

  // Copy all dirty pages into new page table.
  snap_sv->pth.copyInPages(getSlotPDPTRoot());
  active_umi_->snap_p->SetValue(snap_sv);
  set_status(running);
}

void umm::UmManager::PageFaultHandler::HandleFault(ExceptionFrame *ef,
                                                   uintptr_t addr) {
  umm::manager->process_pagefault(ef, addr);
}

void umm::UmManager::process_pagefault(ExceptionFrame *ef, uintptr_t vaddr) {
  if(status() == snapshot)
    kprintf_force(RED "Umm... Snapshot Pagefault!?\n" RESET);

  kassert(status() != empty);
  kassert(valid_address(vaddr));

  x86_64::PgFaultErrorCode ec;
  ec.val = ef->error_code;
  active_umi_->logFault(ec);

  auto virtual_page = Pfn::Down(vaddr);
  auto virtual_page_addr = virtual_page.ToAddr();
  lin_addr virt;
  virt.raw = virtual_page_addr;

  /* Pass to the mounted sv to select/allocate the backing page */
  uintptr_t physical_start_addr = 0;
  if(ec.P == 1){
    // kprintf_force("Copy on write a page!\n");
    physical_start_addr = active_umi_->GetBackingPageCOW(virtual_page_addr);
  } else{
    physical_start_addr = active_umi_->GetBackingPage(virtual_page_addr);
  }

  lin_addr phys;
  phys.raw = physical_start_addr;
  kassert(physical_start_addr != 0);

  simple_pte* PML4Root = UmPgTblMgmt::getPML4Root();
  simple_pte* slotRoot = PML4Root + kSlotPML4Offset;

  bool writeFault = (ec.WR) ? true : false;
  // kprintf_force(" \(%#llx => %p)", vaddr, physical_start_addr);
  // kprintf_force("\t\t write fault? %s, error code %x\n", ec.WR ? "yes" : "no", ec.val );

  if (slotRoot->raw == 0) {
    // No existing slotRoot entry.
    // kprintf_force(RED "Mapping to null pt\n" RESET, slotRoot->pageTabEntToAddr(PML4_LEVEL).raw);
    auto pdpt = UmPgTblMgmt::mapIntoPgTbl(nullptr, phys, virt, PDPT_LEVEL,
                                          TBL_LEVEL, PDPT_LEVEL, writeFault);

    // "Install". Set accessed in case a walker strides accessed pages.
    slotRoot->setPte(pdpt, false, true);

  } else {
    // Slot root holds ptr to sub PT.
    // kprintf_force(RED "Mapping to pt root %p\n" RESET, slotRoot->pageTabEntToAddr(PML4_LEVEL).raw);
    UmPgTblMgmt::mapIntoPgTbl((simple_pte *)slotRoot->pageTabEntToAddr(PML4_LEVEL).raw,
                              phys, virt, PDPT_LEVEL, TBL_LEVEL, PDPT_LEVEL, writeFault);
  }

  // NOTE: if we're in the COW case we have to flush the translation!!!
  if(ec.P){
    umm::UmPgTblMgmt::invlpg( (void*) virt.raw);
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
  simple_pte *slotPML4 = UmPgTblMgmt::getPML4Root() + kSlotPML4Offset;
  return slotPML4;
}

void umm::UmManager::setSlotPDPTRoot(umm::simple_pte* newRoot){
  kassert(newRoot != nullptr);
  (UmPgTblMgmt::getPML4Root()+ kSlotPML4Offset)->setPte(newRoot, false, true);
}

umm::umi::id umm::UmManager::Swap(std::unique_ptr<UmInstance> umi) {
  if (status() != empty) {
    // Only swap out a block instance
    kassert(status() == blocked);
    auto old_umi = Unload();
    auto old_umi_id = old_umi->Id();
    inactive_umi_map_.emplace(old_umi_id, std::move(old_umi));
    inactive_umi_queue_.push(old_umi_id);
    // Now the core is empty
    kassert(status() == empty);
  }
  return Load(std::move(umi));
}

umm::umi::id umm::UmManager::Load(std::unique_ptr<UmInstance> umi) {
  // Better not have a loaded root.
  simple_pte *pdptRoot = getSlotPDPTRoot();
  kassert(pdptRoot == nullptr);

  // If we have a vaild pth root, install it.
  auto pthRoot = umi->sv_.pth.Root();
  if(pthRoot != nullptr){
    // kprintf("Installing instance pte root.\n");
    setSlotPDPTRoot(pthRoot);
    pdptRoot = getSlotPDPTRoot();
    // kprintf("Slot root is %p\n", pdptRoot);
    kassert(pdptRoot != nullptr);
  }
  // Otherwise leave it 0 to be populated during 1st page fault.

	// Set snapshot for this instance
  if (valid_address(umi->snap_addr)) {
    set_snapshot(umi->snap_addr);
  }
  // Inform the proxy of the new instance
	auto umi_id = umi->Id();
  proxy->LoadUmi(umi_id);
  active_umi_ = std::move(umi);
  set_status(loaded);
	return umi_id;
}

/** Internal function, unloads the Slot and clears the caches */
std::unique_ptr<umm::UmInstance> umm::UmManager::Unload() {
  // Clear slot PTE.
  simple_pte *slotPML4Ent = getSlotPML4PTE();
  kassert(UmPgTblMgmt::exists(slotPML4Ent));
  slotPML4Ent->clearPTE();
  // TODO, make sure page table is g2g or reaped.

  // Modified page table, invalidate caches. This is confirmed to matter in virtualization.
  UmPgTblMgmt::flushTranslationCaches();

	yeild_instance_ = false;
  set_status(empty);

  kassert(!UmPgTblMgmt::exists(slotPML4Ent));

  return std::move(active_umi_);
}

std::unique_ptr<umm::UmInstance> umm::UmManager::Run(std::unique_ptr<umm::UmInstance> umi) {
	auto umi_id = Load(std::move(umi));
  kassert(status() == loaded);
  kprintf(GREEN "Umm... Deploying UMI %d on core #%d\n" RESET,
                umi_id, (size_t)ebbrt::Cpu::GetMine());
  trigger_bp_exception();
	// Return here after Halt() is called
	return Unload(); // Assume the umi remains loaded
}

void umm::UmManager::Halt() {
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
  kprintf(GREEN "Umm... Returned from the instance on core #%d\n" RESET,
                (size_t)ebbrt::Cpu::GetMine());
  // Clear proxy data
  proxy->LoadUmi(0);
	active_umi_->DisableTimers();
  trigger_bp_exception();
}

// XXX: How do we clear this ?
void umm::UmManager::set_snapshot(uintptr_t vaddr){
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
}

void umm::UmManager::Block(size_t ns){
  if (!ns) { // If no timeout amount
    kprintf(RED "0" RESET);
    // By returning immediately it effectively acts as a Read poll
    // but without giving up the core to process new IO (i.e., worthless)
    return;
  }
  auto wake_time = ebbrt::clock::Wall::Now();
	wake_time += std::chrono::nanoseconds(ns);
	active_umi_->Block(wake_time);
}
