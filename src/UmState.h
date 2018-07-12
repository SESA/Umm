//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_STATE_H_
#define UMM_UM_STATE_H_

#include "umm-common.h"

namespace umm {

/** UmState
 *  Data type containing Umm Instance state
 */
class UmState {
public:
  // UmState::Region Class
  struct Region {
    bool AddrIsInRegion(uintptr_t vaddr) {
      return (vaddr >= start && vaddr < start + length);
    }
    size_t GetOffset(uintptr_t vaddr) {
      if (AddrIsInRegion(vaddr))
        return (vaddr - start);
      else
        return 0;
    }
    void Print() {
      kprintf_force("Region name: %s\n", name.c_str());
      kprintf_force("       size: %llu\n", length);
      kprintf_force("  read-only: %d\n", !writable);
      kprintf_force("  page size: %d\n", kPageSize << page_order);
      kprintf_force("page misses: %d\n", count);
    }
    uintptr_t start; // starting virtual address of region
    size_t length; //TODO(jmcadden): rename to size
    std::string name;
    bool writable = false;
    uint8_t page_order = UMM_REGION_PAGE_ORDER; // pow2 page size 
    size_t count=0; // miss counter
    //TODO(jmcadden): Change data to void* or uintptr_t
    unsigned char *data = nullptr; // Location of backing data.
  }; // UmState::Region

  UmState(){};
  explicit UmState(uintptr_t entry) { SetEntry(entry); };
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
    kabort("Umm... No region found for addr %p\n", vaddr);
  }
  std::list<Region> region_list_;
  std::vector<uintptr_t> faulted_pages_;
  ExceptionFrame ef;
}; // UmState
} // umm

#endif // UMM_UM_STATE_H_
