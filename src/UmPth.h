//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_PTH_H_
#define UMM_UM_PTH_H_

#include "umm-common.h"
#include "UmPgTblMgr.h"

#define PTH_CTRS 0

namespace umm {

/** 
 *	UmPth - Page table handler type of an UmSV
 */
class UmPth {
public:
  UmPth(): root_(nullptr),lvl_(PDPT_LEVEL){};
  // Destructor.
  ~UmPth();
  UmPth(simple_pte *root, uint8_t lvl) : root_(root), lvl_(lvl) {}
  UmPth(const UmPth &rhs);
  UmPth& operator=(const UmPth& rhs);
	// public methods
  simple_pte *Root() const { return root_; }
  void copyInPages(const simple_pte *srcRoot);
  void printMappedPagesCount() const;
private:
  simple_pte *root_;
  uint8_t lvl_;
}; // UmPth
} // umm

#endif // UMM_UM_PTH_H_
