//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_SV_H_
#define UMM_UM_SV_H_

// #include "umm-common.h"
#include "UmPth.h"
#include "UmRegion.h"

namespace umm {

/** UmSV - State Vector
 * A data type containing the raw execution state of the process. To be
 * executed a raw SV must be instantiated into a executable type
 * (UmInstance).
 *
 * SV originates from the Om/PSML Research Group at Boston University
 */
class UmSV {
public:
  // UmSV();
  UmSV(){ ef = {0}; };
  // UmSV(const UmSV&);
  explicit UmSV(uintptr_t entry) { ef={0}; SetEntry(entry); };

  void SetEntry(uintptr_t paddr);
  void AddRegion(Region &reg);
  void Print();
  // void deepCopy(const UmSV other);
  umm::Region& GetRegionOfAddr(uintptr_t vaddr);
  const Region& GetRegionByName(const char *p);

  // UmSV& operator=(const UmSV& rhs);
  // bool deepCompareRegionLists(const UmSV& other) const;
  // void deepCopyRegionList(const UmSV& other);

  std::list<Region> region_list_; // TODO: generic type
  ExceptionFrame ef;
  UmPth pth;

}; // UmSV
} // umm
#endif // UMM_UM_SV_H_
