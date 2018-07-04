//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmManager.h"

#include <ebbrt/native/Clock.h>
#include <ebbrt/native/EventManager.h>
#include <ebbrt/native/VMemAllocator.h>

struct ukvm_cpu_boot_info {
  uint64_t tsc_freq = 2599997000; 
  uint64_t ebbrt_printf_addr;
  uint64_t ebbrt_walltime_addr;
  uint64_t ebbrt_exit_addr;
};

struct ukvm_boot_info {
  uint64_t mem_size;
  uint64_t kernel_end;
  char *cmdline;
  ukvm_cpu_boot_info cpu;
};

uint64_t wallclock_kludge() {
  auto tp = ebbrt::clock::Wall::Now();
  auto dur = tp.time_since_epoch();
  auto dur_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur);
  return dur_ns.count();
}

void exit_kludge() {
  // FIXME(jmcadden): We don't yet support exits
  kabort("Received an exit call \n");
}

bool umm::UmManager::addrInVirtualRange(uintptr_t vaddr) {
  return (vaddr >= kSlotStartVAddr) && (vaddr < kSlotEndVAddr);
}

void umm::UmManager::PageFaultHandler::HandleFault(ExceptionFrame *ef,
                                                   uintptr_t addr) {
  manager->HandlePageFault(ef, addr);
}

void umm::UmManager::Init() {
  // Setup Ebb translation
  Create(UmManager::global_id);
  // Setup page fault handler
  auto hdlr = std::make_unique<PageFaultHandler>();
  ebbrt::vmem_allocator->AllocRange(kSlotPageLength, kSlotStartVAddr,
                                    std::move(hdlr));
}

void umm::UmManager::HandlePageFault(ExceptionFrame *ef, uintptr_t vaddr) {

  auto virtual_page = Pfn::Down(vaddr);
  auto virtual_page_addr = virtual_page.ToAddr();

  //kprintf("Getting backing page for address %p\n", vaddr);

  /* Pass to the mounted sv to select/allocate the backing page */
  auto physical_start_addr =
      um_kernel_->GetBackingPageAddress(virtual_page_addr);
  auto backing_page = Pfn::Down(physical_start_addr);

  /* Map backing page into core's page tables */
  ebbrt::vmem::MapMemory(virtual_page, backing_page, kPageSize);
}

void umm::UmManager::Load(std::unique_ptr<UmInstance> um) {
  kbugon(is_loaded_);
  um_kernel_ = std::move(um);
  is_loaded_ = true;
}

std::unique_ptr<umm::UmInstance> umm::UmManager::Unload() {
  kbugon(!is_loaded_);
  auto tmp_um = std::move(um_kernel_);
  is_loaded_ = false;
  // TODO(jmcadden): Unmap slot memory
  return std::move(tmp_um);
}

uint8_t umm::UmManager::Start() {
  assert(is_loaded_);

  // TODO(tommyu): This currently only starts an sv from the entry pt
  // supplied in the elf loader.

  // Solo5 boot arguments
  auto kern_info = new struct ukvm_boot_info;
  // TODO(jmcadden): Set memsize to slot
  kern_info->mem_size = 1 << 28;
  // TODO(jmcadden): Set heap as region within execution slot
  kern_info->kernel_end = (uint64_t)malloc(1 << 28);
  char opt_debug[] = "--solo5:debug";
  kern_info->cmdline = opt_debug;
  kern_info->cpu.ebbrt_printf_addr = (uint64_t)kprintf_force;
  kern_info->cpu.ebbrt_walltime_addr = (uint64_t)wallclock_kludge;
  kern_info->cpu.ebbrt_exit_addr = (uint64_t)exit_kludge;

  const uint64_t args = (uint64_t)kern_info;
  const char *argv = nullptr;

  kprintf_force(GREEN "\nOm: Kicking off Unikernel\n\n" RESET);

  // Call the start symbol of the unikernel
  uint32_t (*Entry)() = (uint32_t(*)())um_kernel_->GetEntrypoint();
  kprintf("Entry point is %p \n", Entry);

  __asm__ __volatile__("mov %0, %%rdi" ::"r"(args));
  __asm__ __volatile__("mov %0, %%rsi" ::"r"(argv));
  return Entry();
}
