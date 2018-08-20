//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmPth.h"
#include "UmPgTblMgr.h"
namespace umm {

  UmPth::UmPth(const UmPth &rhs) : root_(nullptr) {

  kprintf(CYAN "Pth copy constructor\n" RESET);
  *this = rhs;
}

UmPth& UmPth::operator=(const UmPth& rhs){
  lvl_ = rhs.lvl_;

  // kprintf(YELLOW "Pth assignment operator, copy pg tbl if exist.\n" RESET);
  // kprintf(YELLOW "rhs.lvl_ = %d; rhs.root_ = %p\n" RESET, rhs.lvl_, rhs.root_);

  // Shouldn't be overwriting yet.
  kassert(root_ == nullptr);

  // Deep copy dirty pages from other pth.
  if(rhs.root_ != nullptr)
    root_ = UmPgTblMgmt::walkPgTblCopyDirty(const_cast<simple_pte *>(rhs.root_), root_, lvl_);

  // if(rhs.root_ != nullptr)
    // rhs.printMappedPagesCount();

  // kprintf(YELLOW "root_ = %p\n" RESET, root_);

  return *this;
}

// TODO: const
void UmPth::copyInPages(const simple_pte *srcRoot){
  auto start = ebbrt::clock::Wall::Now();
  // Flush dirty bits out of caches.

  UmPgTblMgmt::doubleCacheInvalidate(const_cast<simple_pte *>(srcRoot), lvl_);

  // kprintf(BLUE "About to Copy in written pages, root_ is %p\n", root_);
  root_ = UmPgTblMgmt::walkPgTblCopyDirty(const_cast<simple_pte *>(srcRoot), root_, lvl_);
  // kprintf(BLUE "Copy done, root_ is %p\n", root_);

  // DEBUGGING
  // printf(YELLOW "After copy, dumping old page table\n" RESET);
  // UmPgTblMgmt::dumpFullTableAddrs(const_cast<simple_pte *>(srcRoot), PDPT_LEVEL);
  // printf(CYAN "After copy, dumping new page table\n" RESET);
  // printMappedPagesCount();
  // UmPgTblMgmt::dumpFullTableAddrs(root_, PDPT_LEVEL);

  auto stop = ebbrt::clock::Wall::Now();

  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  UmPgTblMgmt::countValidPagesLamb(counts, root_, lvl_);
  printf(CYAN "Copied: %lu dirty 4k pages", counts[_4K__]);
  printf(" in (%dms)\n" RESET, std::chrono::duration_cast<std::chrono::milliseconds>(stop - start));

  kassert(root_ != nullptr);
  // while(1);
}

void UmPth::printMappedPagesCount() const{
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  UmPgTblMgmt::countValidPagesLamb(counts, root_, lvl_);

  for (int i=4; i>0; i--){
    printf(YELLOW "counts[%s] = %lu\n" RESET, level_names[i], counts[i]);
  }
}
}
