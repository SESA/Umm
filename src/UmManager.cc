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

// ENABLE SLOT DEBUG OUTPUT
#define DEBUG_PRINT_SLOT  0

uintptr_t umm::UmManager::GetKernStackPtr() const{
	// Assuming this core has an active umi and it's the one we want 
	kbugon(!slot_has_instance());
  return active_umi_->caller_restore_frame_.rsp;
  // return caller_restore_frame_.rsp;
}

uintptr_t umm::UmManager::RestoreFnStackPtr() const {
	kbugon(!slot_has_instance());
	// Assuming this core has an active umi and it's the one we want 
  return active_umi_->fnStack;
}

void umm::UmManager::SaveFnStackPtr(const uintptr_t fnStack){
	kbugon(!slot_has_instance());
	// Assuming this core has an active umi and it's the one we want 
  active_umi_->fnStack =  fnStack;
}

umm::UmManager::UmManager(){
#ifdef USE_SYSCALL
  // Instrument gdt with user segments.
  umm::syscall::addUserSegments();
  // init syscall extensions and MSRs.
  umm::syscall::enableSyscallSysret();
#endif
}

extern "C" void ebbrt::idt::DebugException(ExceptionFrame* ef) {
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
  // If the core is in the loaded position, we enter, if already active, we exit.

  auto stat = status();

  // Loaded, ready to start running.
  if (stat == loaded) {

    // Store the runSV() frame for when done SV execution.
    active_umi_->caller_restore_frame_ = *ef;

    // Overwrite exception frame from sv, setup by loader / setArguments().
    *ef = active_umi_->sv_.ef;
    set_status(active);

#ifdef USE_SYSCALL
    // Config gdt segments for user.
    ef->ss = (3 << 3) | 3;
    ef->cs = (4 << 3) | 3;
#endif

    return;
  }

  if (stat == halting) {
    *ef = active_umi_->caller_restore_frame_;
    set_status(finished);
    return;
  }

  kprintf_force("Trying to enter / exit from invalid state, %d\n", stat);
  kabort();

}

void umm::UmManager::UmmStatus::set(umm::UmManager::Status new_status) {
  switch (new_status) {
  case empty:
    if (s_ == active || s_ == snapshot) // Don't unload if active or snapshotting
      break;
    //runtime_ = 0;
    goto OK;
  case loaded:
    if (s_ != empty) // Only load when empty
      break;
    goto OK;
  case active:
    if (s_ != loaded && s_ != snapshot && s_ != idle )
      break;
    goto OK;
  case idle:
    if (s_ != active && s_ != loaded ) 
      break;
    // Log execution time before blocking. We'll resume the clock when active
    goto OK;
  case snapshot:
    if (s_ != active) // Only snapshot when active 
      break;
    goto OK;
  case halting:
    if (s_ == finished || s_ == empty ) 
      break;
    goto OK;
  case finished:
    if (s_ != halting  )
      break;
    goto OK;
  default:
    break;
  }
  kabort("Invalid status change %d->%d ", s_, new_status);
OK:
  s_ = new_status;
}


bool umm::UmManager::is_active_instance(umm::umi::id id) {
  if (active_umi_ && id == active_umi_->Id()) {
    return true;
  }
  return false;
}

void umm::UmManager::SignalHalt(umm::umi::id umi) {
	kassert(slot_has_instance());
  if (umi == active_umi_->Id()) {
    Halt();
  } else {
    kprintf(YELLOW "C%dU%d:SIG_HLT " RESET, (size_t)ebbrt::Cpu::GetMine(),
              active_umi_->Id());
    slot_queue_move_to_front(umi);
    // TODO: Activate instance?
    // If the core is idle and that idle instance can yield, activate this instance and halt it
    inactive_umi_halt_map_.emplace(umi, true);
  }
}


void umm::UmManager::SignalYield(umm::umi::id umi) {
  if (ActiveInstanceId() != umi) {
    return;
  }
  kprintf("C%dU%d:SIG_Y ", (size_t)ebbrt::Cpu::GetMine(),
                active_umi_->Id());
  // Trigger the swap as an async event
  ebbrt::event_manager->SpawnLocal([this]() { this->Yield(); },
                                   /*force async*/ true);
}

#if 0

void umm::UmManager::SignalNoYield(umm::umi::id umi) {
  if (slot_has_instance() && umi == active_umi_->Id()) {
    active_umi_->DisableYield();
  } else {
		// confirm we have an instance
    auto it = inactive_umi_map_.find(umi);
    if (it != inactive_umi_map_.end()) {
      // set DisableYield()
      kassert(it->second->Id() == umi);
      it->second->DisableYield();
    }else{
      kprintf_force(
          YELLOW "Warning: inactive UMI #%d instance not found\n" RESET,
          umi);
    }
  }
}
#endif

umm::UmInstance *umm::UmManager::ActiveInstance() {
  if (active_umi_)
    return active_umi_.get();
  return nullptr;
}

umm::umi::id umm::UmManager::ActiveInstanceId() {
  if (active_umi_)
    return active_umi_->Id();
  return 0;
}

/* Returns raw pointer to the instance */
umm::UmInstance *umm::UmManager::GetInstance(umm::umi::id umi) {
  if (slot_has_instance() && umi == active_umi_->Id())
    return active_umi_.get();
  auto it = inactive_umi_map_.find(umi);
  if (it != inactive_umi_map_.end()) {
    return it->second.get();
  }
  // Better check that your got a valid instance... lol
  return nullptr;
  }

bool umm::UmManager::RequestActivation(umm::umi::id umi){
  if(ActiveInstanceId() != umi){
    SignalResume(umi);
    return false;
  }
  if (status() == halting || status() == snapshot || status() == finished) {
    return false;
  }
  kassert(active_umi_->Id() == umi);
  return true;
}

void umm::UmManager::SignalResume(umm::umi::id umi){
  kprintf(CYAN "C%dU%d:SIG_LD " RESET, (size_t)ebbrt::Cpu::GetMine(), umi);
  if(ActiveInstanceId() != umi){
    slot_queue_move_to_front(umi);
    ebbrt::event_manager->SpawnLocal([this]() { this->Yield(); },
                                     true);
  }
}

void umm::UmManager::slot_queue_push(umi::id id) {
  idle_umi_queue_.emplace_back(id);
}

umm::umi::id umm::UmManager::slot_queue_pop() {
  if (!idle_umi_queue_.empty()) {
    auto ret = idle_umi_queue_.front();
    idle_umi_queue_.erase(idle_umi_queue_.begin());;
    return ret;
  }
  return null_umi_id;
}

bool umm::UmManager::slot_queue_remove(umi::id id){
  size_t pos;
  if (slot_queue_get_pos(id, &pos)) {
    kassert(pos < slot_queue_size());
    idle_umi_queue_.erase(idle_umi_queue_.begin() + pos);
    return true;
  }
  return false;
}

umm::umi::id umm::UmManager::slot_queue_pop_end(){
  if(!idle_umi_queue_.empty()){
    auto ret = idle_umi_queue_.back();
    idle_umi_queue_.pop_back();
    return ret;
  }
  return null_umi_id;
}

size_t umm::UmManager::slot_queue_size(){
  return idle_umi_queue_.size();
}

bool umm::UmManager::slot_queue_get_pos(umi::id umi_id, size_t *pos) {
  auto umi = std::find(idle_umi_queue_.begin(), idle_umi_queue_.end(), umi_id);
  if (umi == idle_umi_queue_.end()) {
    return false;
  }
  *pos = std::distance(idle_umi_queue_.begin(), umi);
  return true;
}

bool umm::UmManager::slot_queue_move_to_front(umi::id id){
  if (idle_umi_queue_.front() == id)
    return true;
  auto original_size = idle_umi_queue_.size();
  if (slot_queue_remove(id)) {
    idle_umi_queue_.emplace_front(id);
    kassert(idle_umi_queue_.size() == original_size);
    return true;
  }
  return false;
}

#if 0
  bool target_found = false;

  // Copy the queue, clear its contents, push new front object
  std::queue<umi::id> tmp_queue = inactive_umi_queue_;
  inactive_umi_queue_ = std::queue<umi::id>();
  inactive_umi_queue_.emplace(id);

  // Push remaining elements back onto queue
  while (!tmp_queue.empty()) {
    auto e = tmp_queue.front();
    if( e == id) {
      target_found = true;
    }else{
      inactive_umi_queue_.emplace(e);
    }
    tmp_queue.pop();
  }
  // If the target was not already on the queue don't add it
  if(!target_found){
    inactive_umi_queue_.pop();
  }
}
#endif

/* Perform a yield if there is a yield to preform */
void umm::UmManager::Yield(){
  if(status() != idle && status() != empty ){
    kprintf(YELLOW "C%d:Y(%d)?" RESET, (size_t)ebbrt::Cpu::GetMine(), status());
    return;
  }

  if (status() == idle) {
    if (!active_umi_->Yieldable()) {
      return;
    }
  }

  // Ignore if no replacement target exists
  if (slot_queue_size() == 0)
    return;

  /* OK, lets try and yield! */

  /* Find the first yieldable instance */
  umi::id next_umi_id = null_umi_id;
  for (auto it = idle_umi_queue_.begin(); it != idle_umi_queue_.end(); ++it) {
    auto umi = GetInstance(*it);
    if (umi && !umi->Yieldable()) {
      kassert(umi->Id() == *it);
      next_umi_id = umi->Id();
      break;
    }
    kprintf(YELLOW "Skip yield to U%d\n" RESET, *it);
  }
  if (next_umi_id == null_umi_id) {
    kprintf(RED "NO yield target available \n" RESET);
    return;
  }
  kprintf(GREEN "Ok yield to U%d\n" RESET, next_umi_id);

  // Grab the instance from the queue
  auto it = inactive_umi_map_.find(next_umi_id);
  if (it == inactive_umi_map_.end()) {
    kabort("UmManager: Instance #%d not found...\n", next_umi_id);
  }

  // Go to go! Let's remove the new instance from the scheduling queues
  auto next_umi = std::move(it->second);
  inactive_umi_map_.erase(next_umi_id);
  slot_queue_remove(next_umi_id);

  /* Load the instance into the slot */
  if (status() == idle) {
    // Swap in the new instance if the slot is idle
    slot_swap_instance(std::move(next_umi));
  } else if (status() == empty) {
    // Load the instance if the slot is empty
    slot_load_instance(std::move(next_umi));
  } else {
    kabort("Attempted yield with status =%d\n", status());
  }
  kassert(status() == loaded);

  /*TODO: Things start to get messy after this point */

  // Next, see if the new instance needs to be booted 
  auto it2 = activation_promise_map_.find(next_umi_id);
  if (it2 != activation_promise_map_.end()) {
    auto ap = std::move(it2->second);
    activation_promise_map_.erase(next_umi_id);
    ap.SetValue(next_umi_id); // XXX: This will syncronously call Then(){...}
    kassert(status() == loaded);
    // The activation future will take over from here...
    kprintf(RED "Finished yield core to UMI #%d. TIME TO ACTIVATE!\n" RESET, next_umi_id);
    return;
  }

  // Finally, see if this umi is signaled to be halted
  auto it3 = inactive_umi_halt_map_.find(next_umi_id);
  if (it3 != inactive_umi_halt_map_.end()) {
    kprintf(RED "Loading an immediately halting instance #%d!\n" RESET, next_umi_id);
    inactive_umi_halt_map_.erase(next_umi_id);
    set_status(idle);
    Halt();
    return;
  }

  /* TODO: Here, we should simply "activate" the instance and let it decide
   * whether it should be booted, halted, unblocked, etc. */
  set_status(idle);
  active_umi_->Activate();
  return;

}


#if 0
/* Yield the current (idle) instance & schedule in a replacement */
void umm::UmManager::Yield(){

  // Ignore if the core is active 
  if (status() == active) {
    return;
  } else if (status() == loaded || status() == snapshot ||
             status() == halting || status() == finished) {
    // Fail in these cases, they should not happen
    kabort("UmManager: Attepting to yield in an invaid state = %d\n", status());
  }
  // Ignore if no replacement
  if (inactive_umi_queue_.empty())
    return;

  /* OK TO YIELD! */
  /* Activate next instance on the queue */
  if (ebbrt::event_manager->QueueLength()) {
    kprintf_force(YELLOW
            "yield with (%d) pending events \n" RESET,
            ebbrt::event_manager->QueueLength());
  }

  // Disable to timer of the current (idle) instance 
  kbugon(timer_set);

  // Pop the next instance from the queue
  umi::id next_umi_id = inactive_umi_queue_.front();
  auto it = inactive_umi_map_.find(next_umi_id);
  if (it == inactive_umi_map_.end()) {
    kabort("UmManager: Instance #%d not found...\n", next_umi_id);
  }

  // Verify that this instance should be scheduled in
  if( it->second->Yieldable()){
    kprintf_force(YELLOW "Skipping yield of U%d \n" RESET,
                  it->second->Id());
    return;
  }

  // Go to go. Let's remove the new instance from the scheduling queues 
  auto next_umi = std::move(it->second);
  inactive_umi_map_.erase(next_umi_id);
  inactive_umi_queue_.pop();

  /* Load the instance into the slot */
  if (status() == idle) {
    // Swap in the new instance if the slot is idle
    slot_swap_instance(std::move(next_umi));
  } else if (status() == empty) {
    // Load the instance if the slot is empty
    slot_load_instance(std::move(next_umi));
  } else {
    kabort("Attempted yield with status =%d\n", status());
  }
  kassert(status() == loaded);


  // Next, see if the new instance needs to be booted 
  auto it2 = activation_promise_map_.find(next_umi_id);
  if (it2 != activation_promise_map_.end()) {
    auto ap = std::move(it2->second);
    activation_promise_map_.erase(next_umi_id);
    ap.SetValue(next_umi_id); // XXX: This will syncronously call Then(){...}
    kassert(status() == loaded);
    // The activation future will take over from here...
    kprintf(RED "Finished yield core to UMI #%d. TIME TO ACTIVATE!\n" RESET, next_umi_id);
    return;
  }

  // Finally, see if this umi is signaled to be halted
  auto it3 = inactive_umi_halt_map_.find(next_umi_id);
  if (it3 != inactive_umi_halt_map_.end()) {
    kprintf(RED "Loading an immediately halting instance #%d!\n" RESET, next_umi_id);
    inactive_umi_halt_map_.erase(next_umi_id);
    set_status(idle);
    Halt();
    return;
  }

  /* TODO: Here, we should simply "activate" the instance and let it decide
   * whether it should be booted, halted, unblocked, etc. */
  set_status(idle);
  active_umi_->Activate();
  return;
}
#endif

void umm::UmManager::process_checkpoint(ebbrt::idt::ExceptionFrame *ef) {
  kassert(status() != snapshot);
  kprintf(MAGENTA "C%dU%d:SNAP! " RESET, (size_t)ebbrt::Cpu::GetMine(), active_umi_->Id());
  set_status(snapshot);

  UmSV *snap_sv = new UmSV();
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
  set_status(active);
}

void umm::UmManager::PageFaultHandler::HandleFault(ExceptionFrame *ef,
                                                   uintptr_t addr) {
  umm::manager->process_pagefault(ef, addr);
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
  // Pagefault
  kassert(status() != empty);
  kassert(valid_address(vaddr));
  kassert(status() != snapshot);

  x86_64::PgFaultErrorCode ec;
  ec.val = ef->error_code;

  // Increment page fault counters. Optional.
  active_umi_->logFault(ec);

  // This allocates a page for the umi. It is filled with data in this call
  // from one of 3 sources:
  // 1) If this is a copy on write (determined by the PET existing) it is
  // copied from the source.
  // 2) If it's a page from the ELF, it's copied.
  // 3) If it's a zeroed page not in the ELF (like BSS), it's zero filled.
  lin_addr phys, virt;
  {
    virt.raw = Pfn::Down(vaddr).ToAddr();
    phys.raw = active_umi_->GetBackingPage(virt.raw, ec.isPresent());
  }

  // Take that physical page and map it into the VAS creating a PT if necessary.
  bool writeFault = ec.isWriteFault();
  simple_pte* slotRoot = UmPgTblMgmt::getPML4Root() + kSlotPML4Offset;
  // TODO: Maybe just if notPresent?
  if (slotRoot->raw == 0) {
    // No existing slotRoot entry. Create new page table.
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
  if(ec.isPresent()){
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
  // Root of slot.
  simple_pte *slotPML4 = UmPgTblMgmt::getPML4Root() + kSlotPML4Offset;
  return slotPML4;
}

void umm::UmManager::setSlotPDPTRoot(umm::simple_pte* newRoot){
  kassert(newRoot != nullptr);
  (UmPgTblMgmt::getPML4Root()+ kSlotPML4Offset)->setPte(newRoot, false, true, true, true);
  // (UmPgTblMgmt::getPML4Root()+ kSlotPML4Offset)->setPte(newRoot, false, true);
}

ebbrt::Future<umm::umi::id>
umm::UmManager::queue_instance_activation(std::unique_ptr<UmInstance> umi) {
  kassert(status() != empty); // Otherwise.. we should just load and run
  auto id = umi->Id();
  kprintf(YELLOW "Queueing UMI %d on core #%d\n" RESET, id,
          (size_t)ebbrt::Cpu::GetMine());
  auto umi_p = ebbrt::Promise<umi::id>();
  auto umi_f = umi_p.GetFuture();
  slot_queue_push(id);
  activation_promise_map_.emplace(id, std::move(umi_p));
  inactive_umi_map_.emplace(id, std::move(umi));
  return umi_f;
}

umm::umi::id umm::UmManager::slot_swap_instance(std::unique_ptr<UmInstance> umi) {
  if (status() != empty) {
    // Only swap out a block instance
    kassert(status() == idle);
    auto old_umi = slot_unload_instance();
    auto old_umi_id = old_umi->Id();
    inactive_umi_map_.emplace(old_umi_id, std::move(old_umi));
    // Push the loaded umi TO THE END OF THE QUEUE
    // XXX: Is this right???
    slot_queue_push(old_umi_id);
    // Now the core is empty
    kassert(status() == empty);
  }
  return slot_load_instance(std::move(umi));
}

umm::umi::id umm::UmManager::slot_load_instance(std::unique_ptr<UmInstance> umi) {
  kassert(status() == empty);
  // Better not have a loaded root.
  simple_pte *pdptRoot = getSlotPDPTRoot();
  kassert(pdptRoot == nullptr);

  // If we have a vaild pth root, install it.
  auto pthRoot = umi->sv_.pth.Root();
  if(pthRoot != nullptr){
    //kprintf("Installing instance pte root.\n");
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
  proxy->SetActiveInstance(umi_id);
  active_umi_ = std::move(umi);
  set_status(loaded);
#if DEBUG_PRINT_SLOT
  kprintf_force("\nC%dU%d:LD ", (size_t)ebbrt::Cpu::GetMine(), active_umi_->Id());
#endif
	return umi_id;
}

/** Internal function, unloads the Slot and clears the caches */
std::unique_ptr<umm::UmInstance> umm::UmManager::slot_unload_instance() {
  // Clear slot PTE.
  simple_pte *slotPML4Ent = getSlotPML4PTE();
  kassert(UmPgTblMgmt::exists(slotPML4Ent));
  slotPML4Ent->clearPTE();
  // TODO, make sure page table is g2g or reaped.

  // Modified page table, invalidate caches. This is confirmed to matter in virtualization.
  UmPgTblMgmt::flushTranslationCaches();

  set_status(empty);

  kassert(!UmPgTblMgmt::exists(slotPML4Ent));

#if DEBUG_PRINT_SLOT
  kprintf_force("C%dU%d:ULD ", (size_t)ebbrt::Cpu::GetMine(), active_umi_->Id());
#endif
  return std::move(active_umi_);
}

ebbrt::Future<umm::umi::id>
umm::UmManager::Load(std::unique_ptr<umm::UmInstance> umi) {
    auto id = umi->Id();
  if (status() == empty) {
		// If slot is empty load right away
    slot_load_instance(std::move(umi));
    return ebbrt::MakeReadyFuture<umm::umi::id>(id);
  } else if (status() == idle && active_umi_->Yieldable()) {
    // If current umi is idle and can yield, swap in new instance
		// TODO: move disable_timer into slot_swap
    disable_timer();
    auto loaded_id = slot_swap_instance(std::move(umi));
    kbugon(loaded_id != id);
    return ebbrt::MakeReadyFuture<umm::umi::id>(id);
  } else {
    // Active instance unable to yield. Queue this activation
    return queue_instance_activation(std::move(umi));
  }
}

std::unique_ptr<umm::UmInstance> umm::UmManager::Start(umm::umi::id umi_id) {
  kassert(status() == loaded);
  kassert(umi_id == active_umi_->Id());
  kprintf(GREEN "C%dU%d: Start " RESET, 
          (size_t)ebbrt::Cpu::GetMine(), umi_id);
  trigger_bp_exception();

  // Return here after Halt is called
  if (slot_queue_size()) {
    // Instance can yeild. Start process
    kprintf(YELLOW "Finished execution... But there are %d more UMIs on this core (core%u)\n" RESET,
            slot_queue_size(), (size_t)ebbrt::Cpu::GetMine());
    ebbrt::event_manager->SpawnLocal([this]() { this->Yield(); },
                                     true);
  }
  return slot_unload_instance(); // Assume the umi remains loaded
}

std::unique_ptr<umm::UmInstance>
umm::UmManager::Run(std::unique_ptr<umm::UmInstance> umi) {
  auto umi_id = umi->Id();
  kprintf(GREEN "C%dU%d: Run " RESET, 
          (size_t)ebbrt::Cpu::GetMine(), umi_id);
  if (status() == empty) {
    slot_load_instance(std::move(umi));
  } else if (status() == idle) {
    // If the core is idle we'll try and take over
    if (active_umi_->Yieldable()) {
      // Yes, this umi can yield
      kabort(RED "(TODO): Swap active instance with new one", RESET);
      // TODO: Swap in and queue
    } else {
      // Active instance unable to yield. Queue this activation
      kabort(YELLOW "Queuing Instance for later", RESET);
      queue_instance_activation(std::move(umi)).Block();
    }
  } else {
    kabort("Incompatible core status: %d\n", status());
  }
  kassert(status() == loaded);
  kassert(umi_id == active_umi_->Id());
  kprintf(GREEN "Umm... Run UMI %d on core #%d\n" RESET, umi_id,
          (size_t)ebbrt::Cpu::GetMine());
  trigger_bp_exception();
  // Return here after Halt is called
  return slot_unload_instance(); // Assume the umi remains loaded
}

// TODO: function to disable snapshot
void umm::UmManager::set_snapshot(uintptr_t vaddr) {
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

  void umm::UmManager::Fire() {
    kassert(timer_set);
    timer_set = false;
    //kprintf(YELLOW "*" RESET);

    // If the instance is not blocked this timeout is stale, ignore it
    if (status() != idle)
      return;

    //kprintf(RED "*" RESET);
    // if(context_ == nullptr)
    //  return;

    // We take a single clock reading which we use to simplify some corner cases
    // with respect to enabling the timer. This way there is a single time point
    // when this event occurred and all clock computations can be relative to
    // it.
    auto now = ebbrt::clock::Wall::Now();

    // If we reached the time_blocked period then unblock the execution
    if (time_wait != ebbrt::clock::Wall::time_point() && now >= time_wait) {
      time_wait = ebbrt::clock::Wall::time_point(); // clear the time
      active_umi_->Activate();
    }
  }

  void umm::UmManager::enable_timer(ebbrt::clock::Wall::time_point now) {
    if (timer_set || (now >= time_wait)) {
      return;
    }

    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(time_wait - now);
    ebbrt::timer->Start(*this, duration, /* repeat = */ false);
    timer_set = true;
  }

  void umm::UmManager::disable_timer() {
    if (timer_set) {
      ebbrt::timer->Stop(*this);
    }
    timer_set = false;
    time_wait = ebbrt::clock::Wall::time_point(); // clear timer
  }

  void umm::UmManager::Block(size_t ns) {

    set_status(idle);

    /** If we can schedule in a new instance begin that process,
        otherwise set a timer and block */
#if 0
    if (active_umi_->Yieldable() && inactive_umi_queue_.size()) {
      // Trigger the swap as an async event
      ebbrt::event_manager->SpawnLocal(
          [this]() { this->Yield(); }, /*force async*/ true);
      kprintf_force(YELLOW "C%dU%d:Y " RESET, (size_t)ebbrt::Cpu::GetMine(),
                    active_umi_->Id());
    } else {
      // Set timer and deactivate
      auto now = ebbrt::clock::Wall::Now();
      time_wait = now + std::chrono::nanoseconds(ns);
      //enable_timer(now);
      kprintf_force(RED "C%dU%d:B<%u> " RESET, (size_t)ebbrt::Cpu::GetMine(),
                    active_umi_->Id(), (ns / 1000));
    }
#endif
    active_umi_->Block(ns); // Release the core 
    // Return here only after call to Activate()
    if (status() == halting) {
      kabort("We should never see this\n");
    }
    kassert(status() != finished);
    set_status(active);
  }

  void umm::UmManager::Halt() {
    kbugon(status() == empty);
    active_umi_->DisableYield();
    disable_timer();
    set_status(halting);

    if (ebbrt::event_manager->QueueLength()) {
      kprintf(
          YELLOW
          "Attempting to clear (%d) pending events before halting...\n" RESET,
          ebbrt::event_manager->QueueLength());
      ebbrt::event_manager->SpawnLocal([this]() { this->Halt(); }, true);
      return;
    }

    kassert(status() != empty);
    auto umi_id = active_umi_->Id();
    kprintf(YELLOW "C%dU%d:HLT " RESET,
            (size_t)ebbrt::Cpu::GetMine(), umi_id);
    // Clear proxy data
    proxy->RemoveInstanceState(umi_id);
    trigger_bp_exception();
  }

