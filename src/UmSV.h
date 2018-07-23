//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_SV_H_
#define UMM_UM_SV_H_

#include "umm-common.h"
#include "UmPth.h"

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
  /** SV Region class */
  struct Region {
    bool AddrIsInRegion(uintptr_t vaddr) {
      return (vaddr >= start && vaddr < start + length);
    }
    size_t GetOffset(uintptr_t vaddr) {
      if (AddrIsInRegion(vaddr))
        return (vaddr - start);
      else // FIXME: We should be warning or aborting here
        return 0;
    }
    void Print() {
      kprintf_force("Region name: %s\n", name.c_str());
      kprintf_force("       size: %llu\n", length);
      kprintf_force("  read-only: %d\n", !writable);
      kprintf_force("  page size: %d\n", kPageSize << page_order);
      kprintf_force("page faults: %d\n", count);
    }
    /** Mostly-static state */
    std::string name;
    uintptr_t start; /** Starting virtual address of region */
    size_t length;   // TODO: rename to "size"
    bool writable = false;
    uint8_t page_order = UMM_REGION_PAGE_ORDER; /** Pow2 page size to conform
                                                   with EbbRT's PageAllocator
                                                   interfaces */
    unsigned char *data = nullptr;              // Location of backing data.
                                                // TODO: Change to uintptr_t
    /* Transient state */                       // XXX: Clear on copy?
    size_t count = 0;                           /** Page faults on region */

  }; // UmSV::Region

  UmSV(){ ef = {0}; };
  explicit UmSV(uintptr_t entry) { ef={0}; SetEntry(entry); };
  void SetEntry(uintptr_t paddr) { ef.rip = paddr; }
  void AddRegion(Region &reg) { region_list_.push_back(reg); }
  void Print() {
    for (auto &reg : region_list_)
      reg.Print();
      kprintf_force("--\n");
  }
  Region &GetRegionOfAddr(uintptr_t vaddr) {
    for (auto &reg : region_list_) {
      if (reg.AddrIsInRegion(vaddr)){
        return reg;
      }
    }
    kabort("Umm... No region found for addr %p n", vaddr);
  }
  std::list<Region> region_list_; // TODO: generic type 
  ExceptionFrame ef;
  UmPth pth;
}; // UmSV
} // umm
#endif // UMM_UM_SV_H_
