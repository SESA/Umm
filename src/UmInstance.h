//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_INSTANCE_H_
#define UMM_UM_INSTANCE_H_

#include "umm-common.h"
#include "UmState.h"

namespace umm {

/**
 * UmInstance - Executable unikernel monitor (Um) instance
 *
 * Represents state executable by an Um execution manager.  At it's core, the
 * UmInstance contains an UmSV along with any instance specific state.  In the
 * future, the UmInstance maybe typed with a particular instance-type (for
 * example, A Solo5-instances which conforms to the solo5 boot parameters). In
 * this way, the UmSV can remain target-agnonstic, always displaying the raw
 * state of the process.  */
class UmInstance {
public:
  UmInstance() = delete;
  explicit UmInstance(UmState sv) : sv_(sv) {};

  /** Resolve phyical page for virtual address */
  uintptr_t GetBackingPage(uintptr_t vaddr);

  // TODO(jmcadden): Move this interface into the UmSV
  //	  						 Or, make it part of a virtual class
  void SetArguments(const uint64_t argc, const char* argv[]=nullptr);

	/* Dump state of the instance*/
  void Print();
  size_t page_count = 0; // TODO: delete
	
	/* TODO(jmcadden): Consider making the sv_ a private member */
  UmState sv_;
	/* TODO: Move runtime clock from the manager into the instance */ 
}; // umm::UmInstance
}

#endif // UMM_UM_INSTANCE_H_
