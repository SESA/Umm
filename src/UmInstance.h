//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_INSTANCE_H_
#define UMM_UM_INSTANCE_H_


#include "umm-common.h"

#include "UmState.h"

namespace umm {

/** UmInstance 
 *  Executable unikernel monitor (Um) instance 
 */
class UmInstance {
public:
  UmInstance() = delete;
  explicit UmInstance(UmState sv);
  /** GetBackingPage - Resolve phyical page for virtual address */
  uintptr_t GetBackingPage(uintptr_t vaddr);
  void SetArguments(const uint64_t argc, const char* argv[]=nullptr);
  void Print();
  UmState Snapshot(ebbrt::idt::ExceptionFrame *ef);
  size_t page_count = 0;
  UmState sv_;
}; // umm::UmInstance
}

#endif // UMM_UM_INSTANCE_H_
