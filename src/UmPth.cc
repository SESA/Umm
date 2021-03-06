//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmPth.h"
#include "UmManager.h"
#include "UmPgTblMgr.h"
#include "umm-internal.h"

namespace umm {

UmPth::UmPth(const UmPth &rhs) : root_(nullptr) {
  kprintf(CYAN "Pth copy constructor\n" RESET);
  *this = rhs;
}

UmPth::~UmPth(){

  // kprintf_force("root is %p\n", root_);
  if (root_ != nullptr){
    // kprintf_force(YELLOW "Reclaiming page table.\n" RESET);

    // #if PTH_PERF
    // kprintf_force(RED "CreateTimeRecord\n");
    // auto a = umm::manager->ctr.CreateTimeRecord(std::string("free pt"));
    // #endif
    UmPgTblMgmt::freePageTableLamb(root_, lvl_);
    // #if PTH_PERF
    // kprintf_force("Add pt time record\n");
    // umm::manager->ctr.add_to_list(umm::manager->ctr_list, a);
    // #endif

  }
  // kprintf_force("Freed the page table!\n");
}

UmPth& UmPth::operator=(const UmPth& rhs){
  // NOTE: RHS better be a sv or need to dump translation caches.
  lvl_ = rhs.lvl_;

  // Shouldn't be overwriting yet.
  kassert(root_ == nullptr);

  if(rhs.root_ != nullptr){

#ifdef NOCOW
// DEEP COPY
#if PTH_CTRS
    auto a = umm::manager->ctr.CreateTimeRecord(std::string("Deep CP PT"));
#endif
    // Deep copy dirty pages from other pth.
    root_ = UmPgTblMgmt::walkPgTblCopyDirty(const_cast<simple_pte *>(rhs.root_),
                                            root_, lvl_);

#else

// USE COW
#if PTH_CTRS
    // auto a = umm::manager->ActiveInstance()->ctr.CreateTimeRecord(std::string("COW PT"));
#endif
    // COW read only dirty pages, deep copy RW dirty pages from other pth.
    root_ = UmPgTblMgmt::walkPgTblCOW(const_cast<simple_pte *>(rhs.root_),
                                      root_, lvl_);
#endif

#if PTH_CTRS
    // umm::manager->ActiveInstance()->ctr.add_to_list(a);
#endif
  }
  return *this;
}

// TODO: const Not used delete me.
void UmPth::copyInPages(const simple_pte *srcRoot) {

  // Flush dirty bits out of caches.
  UmPgTblMgmt::flushTranslationCaches();

#ifdef NOCOW
  root_ = UmPgTblMgmt::walkPgTblCopyDirty(const_cast<simple_pte *>(srcRoot),
                                          root_, lvl_);
#else

  root_ = UmPgTblMgmt::walkPgTblCopyDirtyCOW(const_cast<simple_pte *>(srcRoot),
                                             root_, lvl_);
#endif

  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  UmPgTblMgmt::countValidPagesLamb(counts, root_, lvl_);

  kassert(root_ != nullptr);
}
  size_t UmPth::CountOwnedPages() const{
    std::vector<uint64_t> counts (5);
    UmPgTblMgmt::countWritablePagesLamb(counts, root_, lvl_);

    return counts[1];

  }
}
