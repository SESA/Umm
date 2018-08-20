#ifndef UMM_UM_UMREGION_H_
#define UMM_UM_UMREGION_H_

#include <string>
#include "umm-common.h"

namespace umm {

  /** SV Region class */
  struct Region {
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

    bool AddrIsInRegion(uintptr_t vaddr);
    size_t GetOffset(uintptr_t vaddr);
    void Print();

    // Region();
    // Region(const Region &other);
    // Region operator=(const Region &other);
    // Region();
    // Region(std::string name_, uintptr_t start_, size_t length_, bool writable_, unsigned char *data_) :
    //   name wh
    //   ;
    // Region(const Region& rhs);
    // Region& operator=(const Region& rhs);
    // bool equal_metadata(const Region& rhs) const;
    // bool equal_data(const Region& rhs) const;
  }; // UmSV::Region
} // umm

#endif // end UMM_UM_UMREGION_H_
