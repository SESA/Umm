//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmManager.h"

#include <ebbrt/native/VMemAllocator.h>

extern "C" void ebbrt::idt::DebugException(ExceptionFrame* ef) {
  kprintf(MAGENTA "Umm... Creating a snapshot\n" RESET);
  // Set resume flag to prevent infinite retriggering of exception
  ef->rflags |= 1 << 16;
  umm::manager->process_checkpoint(ef);
  kprintf(MAGENTA "Umm... Returning to execution\n" RESET);
}

extern "C" void ebbrt::idt::BreakpointException(ExceptionFrame* ef) {
  kprintf(CYAN "Umm... Handling breakpoint exception\n" RESET);
  umm::manager->process_resume(ef);
}

void umm::UmManager::Init() {
  // Setup Ebb translation
  Create(UmManager::global_id);
  // Setup page fault handler
  auto hdlr = std::make_unique<PageFaultHandler>();
  ebbrt::vmem_allocator->AllocRange(kSlotPageLength, kSlotStartVAddr,
                                    std::move(hdlr));
}

void umm::UmManager::process_resume(ebbrt::idt::ExceptionFrame *ef){
  //PrintExceptionFrame(ef);
  if (status() == running) {
    // If the instance is running this exception is treated as an exit to
    // restore context of the client who called Start()
    *ef = caller_restore_frame_;
    set_status(finished);
  } else {
    // If this context is not running, this is an jump into the instance. We
    // backup the context of the client and edit the frame to "return" in to the
    // instance
    caller_restore_frame_ = *ef;
    ef->rip = umi_->sv_.ef.rip;
    ef->rdi = umi_->sv_.ef.rdi;
    ef->rsi = umi_->sv_.ef.rsi;
    set_status(running);
  }
}

void umm::UmManager::UmmStatus::Set(umm::UmManager::Status new_status) {
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
    if (s_ == empty) // Only run when loaded
      break;
    goto OK;
  case snapshot:
    if (s_ != running) // Only snapshot when running <??
      break;
    goto OK;
  case finished:
    if (s_ != running) // Only finish when running
      break;
    goto OK;
  default:
    break;
  }
  kabort("Invalid status change %d->%d ", s_, new_status);
OK:
  s_ = new_status;
}

void umm::UmManager::process_checkpoint(ebbrt::idt::ExceptionFrame *ef){
  kprintf(RED "PROCESSING CHECKPOINT \n" RESET);
  kassert(status() != snapshot);
  set_status(snapshot);
  snap_restore_frame_ = *ef;
  //trigger_entry_exception(); 
  //umi_->Print();
  //umi_snapshot_.SetValue(umi_->Snapshot(ef));
  kprintf(GREEN "ALL DONE CHECKPOINTING \n" RESET);
  set_status(running);
}

void umm::UmManager::PageFaultHandler::HandleFault(ExceptionFrame *ef,
                                                   uintptr_t addr) {
  umm::manager->process_pagefault(ef, addr);
}


void umm::UmManager::process_pagefault(ExceptionFrame *ef, uintptr_t vaddr) {
  if(status() == snapshot)
    kprintf(RED "SNAPSHOT PAGEFAULT! \n" RESET);

  auto virtual_page = Pfn::Down(vaddr);
  auto virtual_page_addr = virtual_page.ToAddr();
  /* Pass to the mounted sv to select/allocate the backing page */
  auto physical_start_addr = umi_->GetBackingPage(virtual_page_addr);
  auto backing_page = Pfn::Down(physical_start_addr);
  /* Map backing page into core's page tables */
  ebbrt::vmem::MapMemory(virtual_page, backing_page, kPageSize);
}

void umm::UmManager::Load(std::unique_ptr<UmInstance> umi) {
  set_status(loaded);
  umi_ = std::move(umi);
}

std::unique_ptr<umm::UmInstance> umm::UmManager::Unload() {
  set_status(empty);
  auto tmp_um = std::move(umi_);
  // XXX(jmcadden): Unmap slot memory
  return std::move(tmp_um);
}

void umm::UmManager::Start() { // Enter
  if (status() == loaded )
    kprintf_force(GREEN "\nUmm... Kicking off the Um Instance\n" RESET);
  trigger_entry_exception();
  kprintf_force(GREEN "Umm... Returned from Um Instance\n" RESET);
  umi_->Print();
  set_status(running);
}

ebbrt::Future<umm::UmState> umm::UmManager::SetCheckpoint(uintptr_t vaddr){
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

bool umm::UmManager::valid_address(uintptr_t vaddr) {
  // Address within the virtual address range 
  return ((vaddr >= kSlotStartVAddr) && (vaddr < kSlotEndVAddr));
}

