//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmManager.h"

#include <ebbrt/native/VMemAllocator.h>

extern "C" void ebbrt::idt::DebugException(ExceptionFrame* ef) {
  // Set resume flag to prevent infinite retriggering of exception
  ef->rflags |= 1 << 16;
  umm::manager->process_checkpoint(ef);
}

extern "C" void ebbrt::idt::BreakpointException(ExceptionFrame* ef) {
  kprintf(MAGENTA "Umm... Handling breakpoint exception\n" RESET);
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
  if(is_running_){
    *ef = restore_frame_;
    is_running_ = false;
  } else {
    restore_frame_ = *ef;
    ef->rip = umi_->ef_.rip;
    ef->rdi = umi_->ef_.rdi;
    ef->rsi = umi_->ef_.rsi;
    is_running_ = true;
  }
}

void umm::UmManager::process_checkpoint(ebbrt::idt::ExceptionFrame *ef){
  kprintf(MAGENTA "UMM... SNAPSHOT TIME. *CLICK* \n" RESET);
}

void umm::UmManager::PageFaultHandler::HandleFault(ExceptionFrame *ef,
                                                   uintptr_t addr) {
  umm::manager->process_pagefault(ef, addr);
}


void umm::UmManager::process_pagefault(ExceptionFrame *ef, uintptr_t vaddr) {
  auto virtual_page = Pfn::Down(vaddr);
  auto virtual_page_addr = virtual_page.ToAddr();
  /* Pass to the mounted sv to select/allocate the backing page */
  auto physical_start_addr = umi_->GetBackingPage(virtual_page_addr);
  auto backing_page = Pfn::Down(physical_start_addr);

  /* Map backing page into core's page tables */
  ebbrt::vmem::MapMemory(virtual_page, backing_page, kPageSize);
}

void umm::UmManager::Load(std::unique_ptr<UmInstance> umi) {
  kbugon(is_loaded_);
  umi_ = std::move(umi);
  is_loaded_ = true;
}

std::unique_ptr<umm::UmInstance> umm::UmManager::Unload() {
  kbugon(!is_loaded_);
  auto tmp_um = std::move(umi_);
  is_loaded_ = false;
  // XXX(jmcadden): Unmap slot memory
  return std::move(tmp_um);
}

void umm::UmManager::Start() { // Enter
  kbugon(!is_loaded_);
  if (!is_running_)
    kprintf_force(GREEN "\nUmm... Kicking off the Um Instance!\n" RESET);
  trigger_entry_exception();
  kprintf_force(GREEN "Umm... Returned from Um Instance!\n" RESET);
  umi_->Print();
  is_running_ = false;
}

void umm::UmManager::SetCheckpoint(uintptr_t vaddr){
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
}

bool umm::UmManager::valid_address(uintptr_t vaddr) {
  // Address within the virtual address range 
  return ((vaddr >= kSlotStartVAddr) && (vaddr < kSlotEndVAddr));
}

