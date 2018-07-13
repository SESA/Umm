//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmManager.h"

#include <ebbrt/native/VMemAllocator.h>

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
  } else {
    // If this context is not already running we treat this entry an jump into
    // the instance. Backup the restore_frame (context) of the client and modify
    // the existing frame to "return" back into the instance
    caller_restore_frame_ = *ef;
    ef->rip = umi_->sv_.ef.rip;
    ef->rdi = umi_->sv_.ef.rdi;
    ef->rsi = umi_->sv_.ef.rsi;

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
    if (s_ != loaded && s_ != snapshot) // Run when loaded or more
      break;
    clock_ = ebbrt::clock::Wall::Now(); 
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

  // XXX: Here be Dragons...
  
  // Iterate the region list of the loaded umi
  for (const auto &reg : umi_->sv_.region_list_) {
    kprintf(CYAN "Umm... checkpointing %s region\n" RESET, reg.name.c_str());
    UmSV::Region r = reg;

    if (r.writable) {
      size_t plen;
      // XXX: Special case usr region b/c its too big to malloc
      if (reg.name == "usr") {
        // Allocate a new (empty) region for the writable sections
        plen = kPageSize;
      }else{
        plen = r.length + kPageSize - (r.length % kPageSize); // round up to full page
      }
      r.data = (unsigned char *)malloc(plen);
      kprintf(CYAN "Umm... new data region for %s len=%d[%d] (%p)\n" RESET,
              r.name.c_str(), plen, r.length, r.data);
    }
    snap_sv->AddRegion(r);
  }

  // XXX: More Dragons...

  //   Interate the list of faulted_pages and map in each page
  for( auto vaddr : umi_->sv_.faulted_pages_ ){
    auto r = snap_sv->GetRegionOfAddr(vaddr);
    kassert(r.writable);
    size_t offset = r.GetOffset(vaddr);
    // XXX: Special case usr region b/c its too big to malloc
    if (r.name == "usr") {
      // user region
      offset = 0;
    } else {
      offset = r.GetOffset(vaddr);
    }
    kprintf( CYAN "Umm... capture page %p in %s(%d) |  r.data + offset (%p + %d)\n" RESET,
        vaddr, r.name.c_str(), r.length, r.data, offset);
    // We copy from vaddr directly because it should still be mapped
    std::memcpy((void *)(r.data + offset), (const void *)vaddr, kPageSize);
  }

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

  // TODO: Replace with our own mapping mechanism and semantics

  auto virtual_page = Pfn::Down(vaddr);
  auto virtual_page_addr = virtual_page.ToAddr();

  /* Pass to the mounted sv to select/allocate the backing page */
  auto physical_start_addr = umi_->GetBackingPage(virtual_page_addr);
  auto backing_page = Pfn::Down(physical_start_addr); // TODO: make this an assert

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

