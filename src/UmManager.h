//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_MANAGER_H_
#define UMM_UM_MANAGER_H_

#include <ebbrt/Clock.h>
#include <ebbrt/EbbId.h>
#include <ebbrt/EventManager.h>
#include <ebbrt/GlobalStaticIds.h>
#include <ebbrt/MulticoreEbb.h>
#include <ebbrt/Timer.h>

#include <ebbrt/native/VMemAllocator.h>

#include "UmInstance.h"
#include "UmPgTblMgr.h"
#include "UmSV.h"
#include "umm-common.h"

namespace umm {

/* Execution slot constants */ 
const uintptr_t kSlotStartVAddr = 0xFFFFC00000000000;
const uintptr_t kSlotEndVAddr = 0xFFFFC07FFFFFFFFF;
const uint64_t kSlotPageLength = 0x7FFFFFF;
const uint16_t kSlotPML4Offset = 0x180;

/**
 *  UmManager - MultiCore Ebb that manages per-core executions of SV instances
 */
class UmManager : public ebbrt::MulticoreEbb<UmManager> {
public:


  inline bool valid_address(uintptr_t vaddr) {
    return vaddr && ((vaddr >= umm::kSlotStartVAddr) && (vaddr < umm::kSlotEndVAddr));
  }

  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("UmManager");
  /** Execution slot status */
  enum Status : uint8_t { empty = 0, loaded, running, snapshot, blocked, finished };

  /** Block UMI for nanoseconds */
  void Block(size_t ns);

  /** Class-wide static initialization */
  static void Init(); 

  /** Start execution of loaded instance */
  std::unique_ptr<UmInstance> Run(std::unique_ptr<UmInstance>);

  /** Stop the execution of a loaded instance */
  void Halt();

  /* Utility methods */ 
  void process_pagefault(ExceptionFrame *ef, uintptr_t addr);
  void process_gateway(ExceptionFrame *ef);
  void process_checkpoint(ExceptionFrame *ef);
  Status status() { return status_.get(); } ;
  /* Signal the manager to yeild the instance on next Block */
  void signal_yield();


private:
  /** 
    * PageFaultHandler for the EbbRT VMem subsystem
    */
  class PageFaultHandler : public ebbrt::VMemAllocator::PageFaultHandler {
  public:
    void HandleFault(ebbrt::idt::ExceptionFrame *ef, uintptr_t addr) override;
  };
  void trigger_bp_exception() { __asm__ __volatile__("int3"); };

  /**  //TODO: Rename SlotStatus 
    * Status tracks the status and runtime of slot exection
    */
  class UmmStatus {
  public:
    UmManager::Status get() { return s_; }
    void set(UmManager::Status);
    uint64_t time() { return runtime_; /* in milliseconds */ }
  private:
    UmManager::Status s_ = empty;
    ebbrt::clock::Wall::time_point clock_;
    uint64_t runtime_ = 0;
  }; // UmmStatus

  /** Internal Methods */
  simple_pte *getSlotPML4PTE();
  simple_pte* getSlotPDPTRoot();
  void setSlotPDPTRoot(simple_pte* newRoot);
  void set_status( Status s ) { return status_.set(s);}
  void set_snapshot(uintptr_t vaddr);

  /** Load/Unload Instance onto the core */
  umi::id Load(std::unique_ptr<UmInstance>);
  std::unique_ptr<UmInstance> Unload();
  /** Swap out block instance */
  umi::id Swap(std::unique_ptr<UmInstance>);
  bool yeild_instance_ = false;

  /** Active UMI references */
  std::unique_ptr<UmInstance> active_umi_;
  std::unordered_map<umi::id,std::unique_ptr<UmInstance>> inactive_umi_map_;
  std::queue<umi::id> inactive_umi_queue_;

  /* Internal state */
  UmmStatus status_; // TODO: SlotStatus
};

constexpr auto manager = 
    ebbrt::EbbRef<UmManager>(UmManager::global_id);
}

#endif // UMM_UM_MANAGER_H_
