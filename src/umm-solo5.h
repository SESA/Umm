//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_SOLO5_H_
#define UMM_UM_SOLO5_H_

#include "umm-common.h"

#include "UmManager.h"

#include <ebbrt/native/Clock.h>

namespace umm{


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

static uint64_t wallclock_kludge() {
  auto tp = ebbrt::clock::Wall::Now();
  auto dur = tp.time_since_epoch();
  auto dur_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur);
  return dur_ns.count();
}

static void exit_kludge() {
  // FIXME(jmcadden): We don't yet support exits
  kprintf("Received an exit call \n");
  umm::manager->Start();
}

static inline uint64_t Solo5BootArguments(uint64_t kernel_end, uint64_t mem_size) {
  // Solo5 boot arguments
  auto kern_info = new struct ukvm_boot_info;
  kern_info->mem_size = mem_size;
  kern_info->kernel_end = kernel_end;
  char opt_debug[] = "--solo5:debug";
  kern_info->cmdline = opt_debug;
  kern_info->cpu.ebbrt_printf_addr = (uint64_t)kprintf_force;
  kern_info->cpu.ebbrt_walltime_addr = (uint64_t)wallclock_kludge;
  kern_info->cpu.ebbrt_exit_addr = (uint64_t)exit_kludge;
  return (uint64_t)kern_info;
}
}

#endif // UMM_UM_SOLO5_H_
