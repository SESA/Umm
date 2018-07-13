//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_PTH_H_
#define UMM_UM_PTH_H_

#include "umm-common.h"

namespace umm {

/** 
 *	UmPth - Page table handler type of an Um Instance 
 * 	( Or an UmSV, not sure yet...)
 */
class UmPth {
public:
  UmPth() delete;
  UmPth(uintptr_t root, size_t lvl) : root_(root), lvl_(lvl) {}
	// public methods
  uintptr_t Root() { return root_; }
private:
  const uintptr_t root_;
  const size_t lvl_;
}; // UmPth
} // umm

#endif // UMM_UM_PTH_H_
