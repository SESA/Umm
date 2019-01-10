//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_MANAGER_H_
#define UMM_UM_MANAGER_H_
#include <deque>

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
#include "Counter.h"

namespace umm {

/* Execution slot constants */
const uintptr_t kSlotStartVAddr = 0xFFFFC00000000000;
const uintptr_t kSlotEndVAddr = 0xFFFFC07FFFFFFFFF;
const uint64_t kSlotPageLength = 0x7FFFFFF;
const uint16_t kSlotPML4Offset = 0x180;

// Use int 3 by default, this enables syscall mechanism.
#define USE_SYSCALL

/**
 *  UmManager - MultiCore Ebb that manages per-core executions of SV instances
 */
class UmManager : public ebbrt::MulticoreEbb<UmManager>,  public ebbrt::Timer::Hook {
public:
  // int num_cp_pgs = 0;
  /** Global EbbId */
  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("UmManager");

  UmManager();
  uintptr_t GetKernStackPtr() const;
  // TODO for threading, will have to move this to the umi?
  uintptr_t fnStack;
  uintptr_t RestoreFnStackPtr() const;
  void SaveFnStackPtr(const uintptr_t fnStack);

  /** Class-wide static Ebb initialization */
  static void Init(); 
  umm::count::Counter ctr;
  // List of recorded durations.
  std::list<count::Counter::TimeRecord> ctr_list;

  /** Slot status values*/
  enum Status : uint8_t { empty = 0, loaded, active, snapshot, idle, halting, finished };

  /* Slot helper functions */ 
  inline bool valid_address(uintptr_t vaddr) {
    return vaddr && ((vaddr >= umm::kSlotStartVAddr) && (vaddr < umm::kSlotEndVAddr));
  }

  /** Load - Submit an instance to execute
   *  Returned future is complete when core is loaded
   */
  ebbrt::Future<umi::id> Load(std::unique_ptr<UmInstance>);
  std::unique_ptr<UmInstance> Start(umi::id);

  /** Run - Submit an instance for execution instance
    * Blocks calling event until Halt() has been processed for this instance.
    * Execution may start execution immediatly or be queue until later
    */
  std::unique_ptr<UmInstance> Run(std::unique_ptr<UmInstance>);

  /** Block - Sleep active instance for duration of 'ns' nanoseconds 
  *   This is called by solo5-hypercall-poll hander
  */
  // TODO: Move block to inside the instance
  void Block(size_t ns);

  /** Immediately halt the active instance */
  void Halt(); 

  /** Signal the core to halt UMI at the next opportunity */
  void SignalHalt(umm::umi::id id); 

  /** Signal the core to yield UMI at the next opportunity */
  void SignalYield(umm::umi::id id); 

#if 0
  /** Signal the core to no longer yield UMI */
  void SignalNoYield(umm::umi::id id); 
#endif

  /** Signal the core to restore a yielded instance */
  void SignalResume(umm::umi::id id);

  /* An instance wants to resume */
  bool RequestActivation(umm::umi::id);

  /** Timer event handler */
  void Fire() override;

  /** Public utility/helpter functions */

  /* Returns raw pointer to a managed instance */
  UmInstance *GetInstance(umm::umi::id); 

  /* Returns raw pointer to the active instance */
  UmInstance *ActiveInstance();

  /* Returns id of the loaded instance */
  // TODO: mark as const
  umi::id ActiveInstanceId();

  /* Return slot status */ 
  // TODO: mark as const
  Status status() { return status_.get(); } ;

  /* Return instance activation queue length */
  // TODO: mark as const
  size_t activation_queue_size() { return activation_promise_map_.size(); }

  /** Return true if umi::id is active on this core */
  // TODO: mark as const
  bool is_active_instance(umm::umi::id);
  
  /** Return true is slot has an instance loaded */
  // TODO: mark as const
  bool slot_has_instance() const { return (active_umi_) ? true : false; }

  /* Fault handlers */ 
  void process_pagefault(ExceptionFrame *ef, uintptr_t addr);
  void process_gateway(ExceptionFrame *ef);
  void process_checkpoint(ExceptionFrame *ef);

private:
  /** 
    * PageFaultHandler for the EbbRT VMem subsystem
    */
  class PageFaultHandler : public ebbrt::VMemAllocator::PageFaultHandler {
  public:
    void HandleFault(ebbrt::idt::ExceptionFrame *ef, uintptr_t addr) override;
  };

  /**  //TODO: Rename SlotStatus
    * Status tracks the status and runtime of slot exection
    */
  class UmmStatus {
  public:
    UmManager::Status get() { return s_; }
    void set(UmManager::Status);
    uint64_t time() { return 0; /* in milliseconds */ }
  private:
    UmManager::Status s_ = empty;
    ebbrt::clock::Wall::time_point clock_;
  }; // UmmStatus

  /** Yield loaded instance */
  void Yield(); 

  /** Slot utilities - Load/unload/swap instances, controls slot state */

  /** Load the slot with the given instance 
   *  Return umi::id of loaded instance
   *  Resulting slot status == 'loaded'
   */
  umi::id slot_load_instance(std::unique_ptr<UmInstance>);

  /** Unload the active instance 
   *  Return instance 
   *  Resulting status == 'empty'
   */
  std::unique_ptr<UmInstance> slot_unload_instance();

  /** Swap in the given instance, queue current (blocked) instance 
   *  Return umi::id of loaded instance
   *  Resulting status == 'loaded'
   */
  umi::id slot_swap_instance(std::unique_ptr<UmInstance>);

  /** Attempt to yield current instance, load next queued instance 
   *  Return umi::id of loaded instance
   *  Resulting status == 'loaded' || 'idle'
   */
  void slot_yield_instance();

  /** Slot inactive umi queue */
  const umi::id null_umi_id = 0;
  void slot_queue_push(umi::id);
  bool slot_queue_move_to_front(umi::id);
  umi::id slot_queue_pop();
  umi::id slot_queue_pop_end();
  bool slot_queue_remove(umi::id);
  bool slot_queue_get_pos(umi::id, size_t*);
  size_t slot_queue_size();

  // Trigger exection entry IN/OUT of the slot

  void trigger_bp_exception() { __asm__ __volatile__("int3"); };

  // Queue an instance for activation. Returns Future fifilled once loaded
  // TODO: Is activation the right word here?
  // Instance launch?
  ebbrt::Future<umi::id> queue_instance_activation(std::unique_ptr<UmInstance>);

  /** Inactive UMIs */
  std::unordered_map<umi::id, std::unique_ptr<UmInstance>> inactive_umi_map_;
  //std::queue<umi::id> inactive_umi_queue_;
  std::deque<umi::id> idle_umi_queue_;
  std::unordered_map<umi::id, bool> inactive_umi_halt_map_;

  /** Queued Launches */
  std::unordered_map<umi::id, ebbrt::Promise<umi::id>> activation_promise_map_;

  /* Internal state */
  // The active Um Instance
  std::unique_ptr<UmInstance> active_umi_;
  // Slot status
  UmmStatus status_;

  /** Timing */
  void enable_timer(ebbrt::clock::Wall::time_point now);
  void disable_timer(); 
  bool timer_set = false;
  ebbrt::clock::Wall::time_point clock_;
  ebbrt::clock::Wall::time_point time_wait; // block until this time

  /** Internal Methods */
  simple_pte *getSlotPML4PTE();
  simple_pte* getSlotPDPTRoot();
  void setSlotPDPTRoot(simple_pte* newRoot);
  void set_status( Status s ) { return status_.set(s);}
  void set_snapshot(uintptr_t vaddr);
};

/* Globel reference to the per-core UmManager instance */
constexpr auto manager = 
    ebbrt::EbbRef<UmManager>(UmManager::global_id);
}

#endif // UMM_UM_MANAGER_H_
