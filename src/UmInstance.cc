//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/PageAllocator.h>

#include "util/x86_64.h"
#include "UmInstance.h"
#include "UmManager.h"
#include "umm-internal.h"

#include "UmPgTblMgr.h" // User page HACK XXX

#include "../ext/solo5/kernel/ebbrt/ukvm_guest.h"

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

void umm::UmInstance::WritePacket(std::unique_ptr<ebbrt::IOBuf> buf){
  umi_recv_queue_.emplace(std::move(buf));
}

std::unique_ptr<ebbrt::IOBuf> umm::UmInstance::ReadPacket(){
  if(!HasData())
    return nullptr;
  auto buf = std::move(umi_recv_queue_.front());
  umi_recv_queue_.pop();
  return std::move(buf);
}

void umm::UmInstance::Activate(){
  kassert(!active_);
  kassert(context_);
  // ebbrt::event_manager->ActivateContextSync(std::move(*context_));
  active_ = true;
  ebbrt::event_manager->ActivateContext(std::move(*context_));
}

void umm::UmInstance::Deactivate() {
  kassert(active_);
  active_ = false;
  context_ = new ebbrt::EventManager::EventContext();
  // kprintf(YELLOW "%u" RESET, id_);
  ebbrt::event_manager->SaveContext(*context_);
  // kprintf(GREEN "%u" RESET, id_);
}

void umm::UmInstance::EnableYield() {
  if(!yield_flag_){
    kprintf(CYAN "UMI #%d yield enabled\n" RESET, id_);
    yield_flag_ = true;
  }
}

void umm::UmInstance::DisableYield() {
  if(yield_flag_){
    kprintf(CYAN "UMI #%d yield disabled\n" RESET, id_);
    yield_flag_ = false;
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
