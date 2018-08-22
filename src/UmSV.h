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
  UmSV(){
    // Put Exception frame into a known state.
    ef = {0};

    // HACK: With the ef zeroed in the sv constructor, this is the minimal state
    // to add to get a JS app running on a rump kernel. There is fpu and simd
    // control state as well as a bit set in the CS segment register. These values
    // were simply observed to work, they may be a superset of what's actually
    // neded to run. https://www.felixcloutier.com/x86/FXSAVE.html

    // Avoid X87FpuFloatingPointError:
    // https://wiki.osdev.org/Exceptions#x87_Floating-Point_Exception
    ef.fpu[0] = 0x37f;

    // Avoid SimdFloatingPointException:
    // https://wiki.osdev.org/Exceptions#SIMD_Floating-Point_Exception
    ef.fpu[3] = 0xffff00001f80;

    // Avoid GeneralProtectionException on CS register.
    ef.cs = 0x8;

    kprintf(GREEN "CONS\n" RESET);
  }

  // Delegating constructor to default.
  explicit UmSV(uintptr_t entry) : UmSV() { SetEntry(entry); }

  UmSV(const UmSV& rhs){
    region_list_ = rhs.region_list_;
    ef = rhs.ef;
    pth = rhs.pth;
    kprintf(GREEN "Copy cons.\n" RESET);
  }

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
