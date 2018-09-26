//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmPth.h"
#include "UmPgTblMgr.h"
#include "umm-internal.h"

namespace umm {

UmPth::UmPth(const UmPth &rhs) : root_(nullptr) {
  kprintf(CYAN "Pth copy constructor\n" RESET);
  *this = rhs;
}

  UmPth::~UmPth(){
    if (root_ != nullptr){
      kprintf(YELLOW "Reclaiming page table.\n" RESET);
      UmPgTblMgmt::freePageTableLamb(root_, lvl_);
    }
  }

UmPth& UmPth::operator=(const UmPth& rhs){
  // NOTE: RHS better be a sv or need to dump translation caches.
  lvl_ = rhs.lvl_;

  // Shouldn't be overwriting yet.
  kassert(root_ == nullptr);

  // Deep copy dirty pages from other pth.

  if(rhs.root_ != nullptr){
    // kprintf_force(YELLOW "Dump rhs sv again~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n" RESET);
    // UmPgTblMgmt::dumpFullTableAddrs(const_cast<simple_pte *>(rhs.root_), PDPT_LEVEL);
    // kprintf_force(YELLOW "COW for inst~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n" RESET);

    root_ = UmPgTblMgmt::walkPgTblCOW(const_cast<simple_pte *>(rhs.root_), root_, lvl_);
    // printMappedPagesCount(root_);

    // simple_pte* tmp = UmPgTblMgmt::walkPgTblCopyDirty(const_cast<simple_pte *>(rhs.root_), root_, lvl_);
    // printMappedPagesCount(tmp);
      // int db=0; while(db);



    // kprintf_force(RED "Done COW\n" CYAN " pg tbl copy~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n" RESET);
    // UmPgTblMgmt::dumpFullTableAddrs(const_cast<simple_pte *>(root_), PDPT_LEVEL);
  }
  return *this;
}

// TODO: const
void UmPth::copyInPages(const simple_pte *srcRoot){

  auto start = ebbrt::clock::Wall::Now();

  // Flush dirty bits out of caches.
  // UmPgTblMgmt::doubleCacheInvalidate(const_cast<simple_pte *>(srcRoot), lvl_);
  UmPgTblMgmt::flushTranslationCaches();
  // kprintf_force(CYAN "Before page table copy, orig.\n" RESET);
  // UmPgTblMgmt::dumpFullTableAddrs(const_cast<simple_pte *>(srcRoot), PDPT_LEVEL);

  // kprintf(BLUE "About to Copy in written pages, root_ is %p\n", root_);
  // kprintf_force(CYAN "Copy via Snapshot mechagnism~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n" RESET);
  root_ = UmPgTblMgmt::walkPgTblCopyDirty(const_cast<simple_pte *>(srcRoot), root_, lvl_);

  // kprintf(CYAN "Copy done, root_ is %p\n", root_);
  // UmPgTblMgmt::dumpFullTableAddrs(root_, PDPT_LEVEL);

  // DEBUGGING
  // kprintf(YELLOW "After copy, dumping old page table\n" RESET);
  // UmPgTblMgmt::dumpFullTableAddrs(const_cast<simple_pte *>(srcRoot), PDPT_LEVEL);
  // printMappedPagesCount();
  // kprintf(CYAN "After copy, dumping new page table\n" RESET);


  auto stop = ebbrt::clock::Wall::Now();

  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  UmPgTblMgmt::countValidPagesLamb(counts, root_, lvl_);
  kprintf(CYAN "Copied: %lu dirty 4k pages", counts[_4K__]);
  kprintf(" in (%dms)\n" RESET, std::chrono::duration_cast<std::chrono::milliseconds>(stop - start));

  kassert(root_ != nullptr);
}

void UmPth::printMappedPagesCount() const{
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  UmPgTblMgmt::countValidPagesLamb(counts, root_, lvl_);

  for (int i=4; i>0; i--){
    kprintf(YELLOW "counts[%s] = %lu\n" RESET, level_names[i], counts[i]);
  }
}
}
