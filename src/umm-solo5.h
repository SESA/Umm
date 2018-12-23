//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_SOLO5_H_
#define UMM_UM_SOLO5_H_

#include "umm-common.h"

#include "UmManager.h"

#include <ebbrt/native/Clock.h>
#include <ebbrt/Debug.h>

#include "../ext/solo5/kernel/ebbrt/ukvm_guest.h"

// HACK(jmcadden): A const string in a header is a pretty bad way to specify boot arguments :( 
const std::string opts_ = R"({"cmdline":"bin/node-default /nodejsActionBase/app.js",
 "net":{"if":"ukvmif0","cloner":"true","type":"inet","method":"static","addr":"169.254.1.0","mask":"16"}})";
// const std::string opts_ = "";

#define SOLO5_USR_REGION_SIZE 1 << 28
#define SOLO5_CPU_TSC_FREQ 2599997000
#define SOLO5_CPU_TSC_STEP 300000

/*
 * Block until timeout_nsecs have passed or I/O is
 * possible, whichever is sooner. Returns 1 if I/O is possible, otherwise 0.
 */
void solo5_hypercall_poll(volatile void *arg); 

void solo5_hypercall_netinfo(volatile void *arg); 
void solo5_hypercall_netread(volatile void *arg);
void solo5_hypercall_netwrite(volatile void *arg);

static void solo5_hypercall_halt(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_halt *)arg;
  (void)arg_;
  ebbrt::kprintf_force("\nHalting Solo5. Goodbye!\n");
  umm::manager->Halt();
}

static void solo5_hypercall_walltime(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_walltime *)arg;
  auto tp = ebbrt::clock::Wall::Now();
  auto dur = tp.time_since_epoch();
  auto dur_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur);
  arg_->nsecs = dur_ns.count();
  ebbrt::kprintf("EbbRT walltime is %llu\n", arg_->nsecs);
}

static void solo5_hypercall_puts(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_puts *)arg;
  for (unsigned int i = 0; i < arg_->len; i++)
    ebbrt::kprintf("%c", arg_->data[i]);
}

static void solo5_hypercall_blkinfo(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_blkinfo *)arg;
  (void)arg_;
  ebbrt::kprintf("Error: Unsupported hypercall blkinfo \n");
}

static void solo5_hypercall_blkread(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_blkread *)arg;
  (void)arg_;
  ebbrt::kprintf("Error: Unsupported hypercall blkread \n");
}

static void solo5_hypercall_blkwrite(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_blkwrite *)arg;
  (void)arg_;
  ebbrt::kprintf("Error: Unsupported hypercall blkwrite \n");
}

// Set solo5 boot arguments
static inline uint64_t Solo5BootArguments(uint64_t kernel_end,
                                          uint64_t mem_size,
                                          const std::string &cmdline = opts_) {
  auto kern_info = new struct ukvm_boot_info;
  kern_info->mem_size = mem_size;
  kern_info->kernel_end = kernel_end;
  kern_info->cmdline = (char *)cmdline.c_str();
  // clock settings
  kern_info->cpu.tsc_freq = SOLO5_CPU_TSC_FREQ;
  kern_info->cpu.tsc_step = SOLO5_CPU_TSC_STEP;
  // solo5 hypercalls
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_WALLTIME] =
      (uint64_t)solo5_hypercall_walltime;
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_PUTS] =
      (uint64_t)solo5_hypercall_puts;
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_POLL] =
      (uint64_t)solo5_hypercall_poll;
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_BLKINFO] =
      (uint64_t)solo5_hypercall_blkinfo;
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_BLKWRITE] =
      (uint64_t)solo5_hypercall_blkwrite;
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_BLKREAD] =
      (uint64_t)solo5_hypercall_blkread;
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_NETINFO] =
      (uint64_t)solo5_hypercall_netinfo;
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_NETWRITE] =
      (uint64_t)solo5_hypercall_netwrite;
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_NETREAD] =
      (uint64_t)solo5_hypercall_netread;
  kern_info->cpu.hypercall_ptr[UKVM_HYPERCALL_HALT] =
      (uint64_t)solo5_hypercall_halt;
  return (uint64_t)kern_info;
}

#endif // UMM_UM_SOLO5_H_
