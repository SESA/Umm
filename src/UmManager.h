//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_MANAGER_H_
#define UMM_UM_MANAGER_H_

/*
 *  UmManager.h - Ebb for managing the per-core execution of Umm instances
 */

#include <ebbrt/EbbId.h> 
#include <ebbrt/GlobalStaticIds.h>
#include <ebbrt/MulticoreEbb.h>
#include <ebbrt/native/VMemAllocator.h>

#include "umm-common.h"
#include "UmInstance.h"

namespace umm {

class UmManager : public ebbrt::MulticoreEbb<UmManager> {
public:
  static void Init(); // Class-wide static initialization logic
  static bool addrInVirtualRange(uintptr_t);
  void Load(std::unique_ptr<UmInstance>);
  uint8_t Start(); 
  std::unique_ptr<UmInstance> Unload();
  void HandlePageFault(ebbrt::idt::ExceptionFrame *ef, uintptr_t addr);

  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("UmManager");

private:
  class PageFaultHandler : public ebbrt::VMemAllocator::PageFaultHandler {
  public:
    void HandleFault(ebbrt::idt::ExceptionFrame *ef, uintptr_t addr) override;
  };

  bool is_loaded_ = false;
  std::unique_ptr<UmInstance> um_kernel_;
};

const uintptr_t kSlotStartVAddr = 0xFFFFC00000000000;
const uintptr_t kSlotEndVAddr = 0xFFFFC07FFFFFFFFF;
const uint64_t kSlotPageLength = 0x7FFFFFF;

constexpr auto manager =
    ebbrt::EbbRef<UmManager>(UmManager::global_id);
}

#endif // UMM_UM_MANAGER_H_
