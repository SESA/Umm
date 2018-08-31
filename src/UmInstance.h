//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_INSTANCE_H_
#define UMM_UM_INSTANCE_H_

#include "umm-common.h"

// TODO: this feels bad.
#include "../ext/solo5/kernel/ebbrt/ukvm_guest.h"
#include "UmSV.h"

namespace umm {

/**
 * UmInstance - Executable unikernel monitor (Um) instance
 *
 * Represents state executable by an Um execution manager.  At it's core, the
 * UmInstance contains an UmSV along with any instance specific state.  In the
 * future, the UmInstance maybe typed with a particular instance-type (for
 * example, A Solo5-instances which conforms to the solo5 boot parameters). In
 * this way, the UmSV can remain target-agnonstic, only presented the 'raw'
 * state of the process.  */
class UmInstance {
public:
  UmInstance() = delete;
  // Using a reference so we don't make a redundant copy.
  // This is where the argument page table is copied.
  explicit UmInstance(const UmSV &sv) : sv_(sv) {};

  /** Resolve phyical page for virtual address */
  uintptr_t GetBackingPage(uintptr_t vaddr);

  // TODO(jmcadden): Move this interface into the UmSV
  void SetArguments(const uint64_t argc, const char* argv[]=nullptr);

	/* Dump state of the instance*/
  void Print();

	// TODO: Add runtime duration into the instance
  size_t page_count = 0; // TODO: replace with region-specific counter

  // This may be overkill.
  ukvm_boot_info bi;
  UmSV sv_;
}; // umm::UmInstance
}

#endif // UMM_UM_INSTANCE_H_
