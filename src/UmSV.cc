//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include "UmSV.h"
#include "UmRegion.h"

#include "umm-internal.h"
namespace umm{
  UmSV::UmSV(const UmSV& rhs){
    kassert(&rhs != nullptr);

    // kprintf(RED "region list copy\n" RESET);
    region_list_ = rhs.region_list_;
    ef = rhs.ef;
    pth = rhs.pth;
    // kprintf(GREEN "Copy cons.\n" RESET);
  }

void UmSV::SetEntry(uintptr_t paddr) { ef.rip = paddr; }
  void UmSV::AddRegion(Region &reg) { region_list_.push_back(reg); }
void UmSV::Print() {
  for (auto &reg : region_list_)
    reg.Print();
  kprintf_force("--\n");
}

Region& UmSV::GetRegionOfAddr(uintptr_t vaddr) {
  for (auto &reg : region_list_) {
    if (reg.AddrIsInRegion(vaddr)){
      return reg;
    }
  }
  kabort("Umm... No region found for addr %p n", vaddr);
  while(1);
}


const Region&
UmSV::GetRegionByName(const char* p){

  for (auto const& it : region_list_) {
    if(! strcmp(p, it.name.c_str()))
      return it;
  }
  kprintf_force("A region by that name doesn't exist.\n");
  kabort();
  while(1);
}

} // namespace umm

  // UmSV::UmSV(const UmSV &other) {
  //   kprintf("SV copy constructor\n");
  //   *this = other;
  // }

  // UmSV &UmSV::operator=(const UmSV &rhs) {
  //   // Assignment operator.
  //   // Simple copy
  //   this->ef = rhs.ef;
  //   // Call assignment operator.
  //   this->pth = rhs.pth;

  //   kprintf("Region list copy nyi\n");

  //   return *this;
  // }
// bool UmSV::operator==(const UmSV& rhs) {
//   if (this->region_list_.size() != rhs.region_list_.size())
//     return false;

//   // Try the metadata. Yes, we could do this in one loop, but this allows
//   // us to fail fast on the metadata.
//   auto lhs_it = this->region_list_.begin();
//   auto rhs_it = rhs.region_list_.begin();

//   for (; (lhs_it != this->region_list_.end()); lhs_it++, rhs_it++) {
//     if (!lhs_it->equal_metadata(const_cast<const Region &>(*rhs_it)))
//       return false;
//   }
//   return this->deepCompareRegionLists(rhs);
// }

// bool umm::UmSV::deepCompareRegionLists(const UmSV& other) const {
//   if (this->region_list_.size() != other.region_list_.size())
//     return false;

//   auto lhs_it = this->region_list_.begin();
//   auto rhs_it = other.region_list_.begin();
//   for (; (lhs_it != this->region_list_.end()); lhs_it++, rhs_it++) {

//     if (!lhs_it->equal_data(*rhs_it)) {
//       kprintf(" Sec: %s doesn't match. \n", lhs_it->name.c_str());
//       return false;
//     }
//   }
//   return true;
// }

// void umm::UmSV::deepCopy(const umm::UmSV other){
//   this->ef = other.ef;
//   this->pth = other.pth;
//   this->deepCopyRegionList(other);
// }

// void umm::UmSV::deepCopyRegionList(const umm::UmSV& other) {
//   // TODO(tommyu): Check if there's a deep copy that you can just call.
//   // ebbrt::kprintf("in deep_copy_reg_list, size target list %d \n",
//   // rl->size());
//   kassert(other.region_list_.size() != 0);

//   // TODO: releasing mem NYI.
//   kassert(region_list_.size() == 0);

//   for (auto it = other.region_list_.begin(); it != other.region_list_.end(); it++) {

//     // Insert into SV's region list, set allocated.
//     // TODO(tommyu): Consider destruction.
//     region_list_.push_back(*it);
//   }
//   kassert(region_list_.size() == other.region_list_.size());
//   // Right now all we have is metadata, this loads underlying data.
//   // TODO(tommyu) HACK: this is just to get the demo going. Really should
//   // make exact copy of rl...
// }

