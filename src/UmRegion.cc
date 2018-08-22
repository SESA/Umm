#include "UmRegion.h"
namespace umm{

bool Region::AddrIsInRegion(uintptr_t vaddr) {
  return (vaddr >= start && vaddr < start + length);
}

size_t Region::GetOffset(uintptr_t vaddr) {
  if (AddrIsInRegion(vaddr))
    return (vaddr - start);
  else // FIXME: We should be warning or aborting here
    return 0;
}

void Region::Print() {
  kprintf_force("Region name: %s\n", name.c_str());
  kprintf_force("      start: %p\n", start);
  kprintf_force("       size: %llu\n", length);
  kprintf_force("  read-only: %d\n", !writable);
  kprintf_force("  page size: %d\n", kPageSize << page_order);
  kprintf_force("page faults: %d\n", count);
  kprintf_force("       data: %llu\n", data);
}
} // namespace umm

  // Region::Region() {
  //   kprintf("Region constructor\n");
  // }

  // Region::Region(const Region &other) {
  //   kprintf("Region copy constructor\n");
  //   *this = other;
  // }

  // Region& Region::operator=(const Region& rhs) {
  //   this->name = rhs.name;
  //   this->start = rhs.start;
  //   this->length = rhs.length;
  //   this->writable = rhs.writable;
  //   this->page_order = rhs.page_order;
  //   kprintf("NYI deep copy reg.\n");
  //   return *this;
  // }

  // umm::Region::Region(const umm::Region &other){
  //   kprintf("Copy constructor \n");
  //   *this = other;
  //   // Don't pick up other region's data pointer.
  //   this->data = 0;
  // }

  // umm::Region umm::Region::operator=(const umm::Region &other){
  // }

  // umm::Region::Region(){
  //   kprintf("Constructor \n");
  // }

// bool umm::Region::equal_metadata(const Region& rhs) const {
//   // Can't use default comparitor because of data*.

//   if (this->name != rhs.name) {
//     return false;
//   }

//   // Need to point to same region.
//   if (this->start != rhs.start) {
//     return false;
//   }
//   // Different size regions!
//   if (this->length != rhs.length) {
//     return false;
//   }

//   // Couldn't tell them apart.
//   return true;
// }

// bool umm::Region::equal_data(const Region& rhs) const {
//   // If data reg isn't allocated, no need to check.
//   if (data == nullptr) {
//     return true;
//   }

//   // Data is allocated, gotta check.
//   if (memcmp(this->data, rhs.data, this->length) == 0) {
//     return true;
//   }

//   return false;
// }


// bool umm::Region::operator==(const Region& rhs) const {
//   // Try to fail fast if possible.
//   if (!this->equal_metadata(rhs)) {
//     return false;
//   }
//   // Have to consider the underlying data.
//   if (!this->equal_data(rhs)) {
//     return false;
//   }
//   return true;
// }
