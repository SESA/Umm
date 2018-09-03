//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_SOLO5_H_
#define UMM_UM_SOLO5_H_

#include "umm-common.h"

#include "UmManager.h"
#include "UmProxy.h"

#include <ebbrt/native/Clock.h>

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
static int solo5_hypercall_poll(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_poll *)arg;
  arg_->ret = 0;
  umm::manager->Block(arg_->timeout_nsecs);
  // return from block
  if(umm::proxy->UmHasData()){
    arg_->ret = 1;
  }
  return 0;
}

static int solo5_hypercall_netinfo(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_netinfo *)arg;
  auto ma = umm::proxy->UmMac();
  arg_->mac_address[0] = ma[0];
  arg_->mac_address[1] = ma[1];
  arg_->mac_address[2] = ma[2];
  arg_->mac_address[3] = ma[3];
  arg_->mac_address[4] = ma[4];
  arg_->mac_address[5] = ma[5];
  return 0;
}

/* UKVM_HYPERCALL_NETWRITE 
struct ukvm_netwrite {
    //IN 
    UKVM_GUEST_PTR(const void *) data;
    size_t len;

    //OUT
    int ret; // amount written 
}; */
static int solo5_hypercall_netwrite(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_netwrite *)arg;
  arg_->ret = umm::proxy->UmWrite(arg_->data, arg_->len);
  return 0;
}

/* UKVM_HYPERCALL_NETREAD 
struct ukvm_netread {
    // IN 
    UKVM_GUEST_PTR(void *) data;

    // IN/OUT
    size_t len; // amount read

    // OUT
    int ret; // 0=OK
}; */
static int solo5_hypercall_netread(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_netread *)arg;
  arg_->len = umm::proxy->UmRead(arg_->data, arg_->len);
  // ret is 0 on successful read, 1 otherwise
  arg_->ret = (arg_->len > 0) ? 0 : 1;
  return 0;
}

static int solo5_hypercall_halt(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_halt *)arg;
  (void)arg_;
  ebbrt::kprintf("\nHalting Solo5. Goodbye!\n");
  umm::manager->Halt();
  return 0;
}

static int solo5_hypercall_walltime(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_walltime *)arg;
  auto tp = ebbrt::clock::Wall::Now();
  auto dur = tp.time_since_epoch();
  auto dur_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur);
  arg_->nsecs = dur_ns.count();
  ebbrt::kprintf("EbbRT walltime is %llu\n", arg_->nsecs);
  return 0;
}

static int solo5_hypercall_puts(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_puts *)arg;
  for (unsigned int i = 0; i < arg_->len; i++)
    ebbrt::kprintf_force("%c", arg_->data[i]);
  return 0;
}

static int solo5_hypercall_blkinfo(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_blkinfo *)arg;
  (void)arg_;
  ebbrt::kprintf("Error: Unsupported hypercall blkinfo \n");
  return 1;
}

static int solo5_hypercall_blkread(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_blkread *)arg;
  (void)arg_;
  ebbrt::kprintf("Error: Unsupported hypercall blkread \n");
  return 1;
}

static int solo5_hypercall_blkwrite(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_blkwrite *)arg;
  (void)arg_;
  ebbrt::kprintf("Error: Unsupported hypercall blkwrite \n");
  return 1;
}

// Set solo5 boot arguments
static inline uint64_t Solo5BootArguments(uint64_t kernel_end,
                                          uint64_t mem_size,
                                          std::string &cmdline) {
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
