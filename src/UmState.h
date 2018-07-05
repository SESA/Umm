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
    uintptr_t start; // starting virtual address of region
    size_t length;
    std::string name;
    bool writable = false;
    unsigned char *data = nullptr; // Location of backing data.
  }; // UmState::Region

  UmState() = delete;
  explicit UmState(uintptr_t entry) : entry_(entry){};
  void AddRegion(Region &reg) { region_list_.push_back(reg); }
  const Region &GetRegionOfAddr(uintptr_t vaddr) {
    for (auto &reg : region_list_) {
      if (reg.AddrIsInRegion(vaddr))
        return reg;
    }
    // FIXME(jmcadden): Don't return empty region, or define NOP region
    return std::move(Region());
  }
  uintptr_t entry_; // Instance entry point

private:
  std::list<Region> region_list_;
}; // UmState
} // umm

#endif // UMM_UM_STATE_H_
