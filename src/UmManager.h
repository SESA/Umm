//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_MANAGER_H_
#define UMM_UM_MANAGER_H_

#include <ebbrt/EbbId.h> 
#include <ebbrt/GlobalStaticIds.h>
#include <ebbrt/MulticoreEbb.h>
#include <ebbrt/native/VMemAllocator.h>

#include "umm-common.h"
#include "umm-x86_64.h"

#include "UmInstance.h"
#include "UmState.h"

namespace umm {

/** UmManager
 *  Ebb that manages the per-core execution of Umm instances
 */
class UmManager : public ebbrt::MulticoreEbb<UmManager> {
public:
  enum Status : uint8_t { empty = 0, loaded, running, snapshot, finished };
  static void Init(); // Class-wide static initialization logic
  void Load(std::unique_ptr<UmInstance>);
  void Start(); 
  ebbrt::Future<UmState> SetCheckpoint(uintptr_t vaddr);
  std::unique_ptr<UmInstance> Unload();

  void process_pagefault(ExceptionFrame *ef, uintptr_t addr);
  void process_resume(ExceptionFrame *ef);
  void process_checkpoint(ExceptionFrame *ef);
  Status status() { return status_.Get(); } ; 
  void set_status( Status s ) { return status_.Set(s);}
  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("UmManager");

private:
  class PageFaultHandler : public ebbrt::VMemAllocator::PageFaultHandler {
  public:
    void HandleFault(ebbrt::idt::ExceptionFrame *ef, uintptr_t addr) override;
  };
  class UmmStatus {
  public:
    UmManager::Status Get() { return s_; }
    void Set(UmManager::Status);

  private:
    UmManager::Status s_ = empty;
  };
  void trigger_entry_exception(){ __asm__ __volatile__("int3"); };
  bool valid_address(uintptr_t);

  UmmStatus status_;
  ExceptionFrame caller_restore_frame_; 
  ExceptionFrame snap_restore_frame_; 
  std::unique_ptr<UmInstance> umi_;
  ebbrt::Promise<UmState> umi_snapshot_;
};

const uintptr_t kSlotStartVAddr = 0xFFFFC00000000000;
const uintptr_t kSlotEndVAddr = 0xFFFFC07FFFFFFFFF;
const uint64_t kSlotPageLength = 0x7FFFFFF;
const uint16_t kSlotPML4Offset = 0x180;

constexpr auto manager =
    ebbrt::EbbRef<UmManager>(UmManager::global_id);
}

#endif // UMM_UM_MANAGER_H_
