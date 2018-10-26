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

		/** Conditional stack switch */
		/* When SV is loaded from the ELF, the init stack pointer is zero */ 
		// TODO: Set this in the ELF loader
    // New execution sets rsp. Redeploy doesn't.
    if(ef->rsp == 0)
      ef->rsp = 0xFFFFC07FFFFFFFF0;
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
    kprintf_force(
        RED "Signal manager to HALT... #%d\n" RESET, umi);
    slot_queue_move_to_front(umi);
    // TODO: Activate instance?
    // If the core is idle and that idle instance can yield, activate this instance and halt it
    inactive_umi_halt_map_.emplace(umi, true);
    kprintf_force(RED "UMI #%d queued to halt\n" RESET, umi);
  }
}

void umm::UmManager::SignalYield(umm::umi::id umi) {
	kassert(slot_has_instance());
  if (slot_has_instance() && umi == active_umi_->Id()) {
    active_umi_->EnableYield();
  } else {
    kprintf_force(
        RED "(TODO) Signal manager to ENABLE YIELD of inactive UMI #%d\n" RESET,
        umi);
		kabort();
  }
}

void umm::UmManager::SignalNoYield(umm::umi::id umi) {
  if (slot_has_instance() && umi == active_umi_->Id()) {
    active_umi_->DisableYield();
  } else {
    kprintf_force(
        RED "Signal manager to DISABLE YIELD of inactive UMI #%d\n" RESET, umi);
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

void umm::UmManager::SignalResume(umm::umi::id umi){
  if (slot_has_instance() && umi != active_umi_->Id()) {
    slot_queue_move_to_front(umi);
  }
}

void umm::UmManager::slot_queue_move_to_front(umi::id id){
  if (inactive_umi_queue_.front() == id)
    return;
  
  auto original_size = inactive_umi_queue_.size();
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
  kbugon(inactive_umi_queue_.size() != original_size);
}

/* Yield the current (idle) instance & schedule in a replacement */
void umm::UmManager::slot_yield_instance(){

  if (status() == active) // Ignore if the core is not idle or empty
    return;
  if (inactive_umi_queue_.empty()) // Ignore if no replacement
    return;

  if (status() == loaded || status() == snapshot || status() == halting){
    // These cases should not happen   
    kabort("UmManager: Attepting to yield in an invaid state = %d\n", status());
  }

  /* OK TO YIELD! */
  /* Activate next instance on the queue */

  // Disable to timer of the current (idle) instance 
  disable_timer();

  // Pop the instance from the front of the queue 
  umi::id next_umi_id = inactive_umi_queue_.front();
  inactive_umi_queue_.pop();
  auto it = inactive_umi_map_.find(next_umi_id);
  if (it == inactive_umi_map_.end()) {
    kabort("UmManager: Instance #%d not found...\n", next_umi_id);
  }

  if (status() == finished) {
    kprintf_force(RED "CORE %D: (ERROR) Core is finished. We want to activate UMI #%d\n" RESET,
            (size_t)ebbrt::Cpu::GetMine(), next_umi_id);
    kabort();
    return;
  }

  if (status() == idle) {
    kprintf("[%D:%d->%d]", (size_t)ebbrt::Cpu::GetMine(),
            active_umi_->Id(), next_umi_id);
    slot_swap_instance(std::move(it->second));
  }else if(status() == empty){
//    kprintf(YELLOW "CORE %D: Nothing to Yield. Activating UMI #%d\n" RESET,
//            (size_t)ebbrt::Cpu::GetMine(), next_umi_id);
    slot_load_instance(std::move(it->second));
  }else{
    kabort("Attempted yield with status =%d\n",status());
  }

  inactive_umi_map_.erase(next_umi_id);

  // Next, see if the new instance is booted 
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
    kprintf(RED "Loading an immediatly halting instance #%d!\n" RESET, next_umi_id);
    inactive_umi_halt_map_.erase(next_umi_id);
    set_status(idle);
    Halt();
    return;
  }

  //kprintf(GREEN "Finished yield core to UMI #%d\n" RESET, next_umi_id);
  set_status(idle);
  active_umi_->Activate();
  return;
}

void umm::UmManager::process_checkpoint(ebbrt::idt::ExceptionFrame *ef) {
  kassert(status() != snapshot);
  // ebbrt::kprintf_force(CYAN "Snapshotting, core %d \n" RESET, (size_t)
  // ebbrt::Cpu::GetMine());
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
  if(status() == snapshot)
    kassert(RED "Umm... Snapshot Pagefault!?\n" RESET);

  kassert(status() != empty);
  kassert(valid_address(vaddr));
  if(status() == snapshot)
    kprintf_force(RED "Umm... Snapshot Pagefault\n" RESET);

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

ebbrt::Future<umm::umi::id>
umm::UmManager::queue_instance_activation(std::unique_ptr<UmInstance> umi) {
  kassert(status() != empty); // Otherwise.. we should just load and run
  auto id = umi->Id();
  kprintf(YELLOW "Queueing UMI %d on core #%d\n" RESET, id,
          (size_t)ebbrt::Cpu::GetMine());
  auto umi_p = ebbrt::Promise<umi::id>();
  auto umi_f = umi_p.GetFuture();
  inactive_umi_queue_.push(id);
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
    inactive_umi_queue_.push(old_umi_id);
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

  return std::move(active_umi_);
}

ebbrt::Future<umm::umi::id>
umm::UmManager::Load(std::unique_ptr<umm::UmInstance> umi) {
    auto id = umi->Id();
  if (status() == empty) {
		// If slot is empty load right away
    slot_load_instance(std::move(umi));
    return ebbrt::MakeReadyFuture<umm::umi::id>(id);
  } else if (status() == idle && active_umi_->CanYield()) {
    // If current umi is idle and can yield, swap in new instance
    kprintf(YELLOW "Swap inactive instance with UMI #%d" RESET, id);
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
  kprintf(GREEN "Umm... Start UMI %d on core #%d\n" RESET, umi_id,
          (size_t)ebbrt::Cpu::GetMine());
  trigger_bp_exception();

  // Return here after Halt is called
  if (inactive_umi_queue_.size()) {
    // Instance can yeild. Start process
    kprintf(YELLOW "Finished execution... But there are %d more UMIs on this core (core%u)\n" RESET,
            inactive_umi_queue_.size(), (size_t)ebbrt::Cpu::GetMine());
    ebbrt::event_manager->SpawnLocal([this]() { this->slot_yield_instance(); },
                                     true);
  }
  return slot_unload_instance(); // Assume the umi remains loaded
}

std::unique_ptr<umm::UmInstance>
umm::UmManager::Run(std::unique_ptr<umm::UmInstance> umi) {
  auto umi_id = umi->Id();
  if (status() == empty) {
    slot_load_instance(std::move(umi));
  } else if (status() == idle) {
    // If the core is idle we'll try and take over
    if (active_umi_->CanYield()) {
      // Yes, this umi can yield
      kabort(RED "(TODO): Swap active instance with new one", RESET);
      // TODO: Swap in and queue
    } else {
      // Active instance unable to yield. Queue this activation
      kabort(YELLOW "Queing Instance for later", RESET);
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
      // Unblock execution
      active_umi_->Activate();
      //ebbrt::event_manager->SpawnLocal([this]() { active_umi_->Activate(); },
      //                                 true);
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
    if (timer_set) {
      // Somthing is funky...
      kprintf(RED "[ERROR..1 ] Core %u blocking with a timer set\n" RESET,
              (size_t)ebbrt::Cpu::GetMine());
      kprintf(RED "[ERROR..2] active_umi=%d slot_status=%d \n" RESET,
              active_umi_->Id(), status());
      kabort();
    }

    if(status() == halting){
      kprintf_force(RED "Blocking instance #%d in HALTING state\n" RESET,
              active_umi_->Id());
      return;
    }

    if (!ns) {
      kprintf(RED "0" RESET);
      return;
    }

    kprintf(RED "B" RESET);

    // Enter idle state
    set_status(idle);

    if (active_umi_->CanYield() && inactive_umi_queue_.size()) {
      // Instance can yeild. Start process
       //kprintf( "Core %u: Starting Yield\n" RESET, (size_t)ebbrt::Cpu::GetMine());
       ebbrt::event_manager->SpawnLocal(
           [this]() { this->slot_yield_instance(); }, true);
    } else {
      // Set timer and deactivate
      auto now = ebbrt::clock::Wall::Now();
      time_wait = now + std::chrono::nanoseconds(ns);
      enable_timer(now);
    }
    active_umi_->Deactivate(); // Block instance
    if (status() == halting) {
      kprintf(CYAN "\n BLOCKING FOREVER!!\n" RESET);
      active_umi_->Deactivate(); // Block
      kprintf_force(CYAN "\nWHAT?? WE SHOULD NEVER RESUME!?\n" RESET);
      kabort();
    }
    kassert(status() != finished);
    set_status(active);
    // Return here once activated
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
      ebbrt::event_manager->SpawnLocal(
          [this]() {
            this->Halt();
          },
          true);
      return;
    }

    kassert(status() != empty);
    auto umi_id = active_umi_->Id();
    kprintf(GREEN "Umm... Returned from instance #%d on core #%d\n" RESET,
            umi_id, (size_t)ebbrt::Cpu::GetMine());
    // Clear proxy data
    proxy->RemoveInstanceState(umi_id);
    trigger_bp_exception();
}

