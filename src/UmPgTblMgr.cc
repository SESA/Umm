//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Clock.h>
#include <ebbrt/native/PageAllocator.h>
#include <ebbrt/native/Pfn.h>
#include "umm-internal.h"

#include "UmManager.h"  // hack to get per core copied pages count.
#include "UmPgTblMgr.h"
#include "UmManager.h"
// #include <Umm.h>
#include <vector>

#define printf kprintf

const unsigned char orders[] = {0, SMALL_ORDER, MEDIUM_ORDER, LARGE_ORDER};

const unsigned char pgShifts[] = {0, SMALL_PG_SHIFT, MED_PG_SHIFT, LG_PG_SHIFT};

const uint64_t pgBytes[] = {0, 1ULL<<SMALL_PG_SHIFT, 1UL<<MED_PG_SHIFT, 1UL<<LG_PG_SHIFT};

const char* level_names[] = {"NO_LEVEL",
                             "TBL_LEVEL",
                             "DIR_LEVEL",
                             "PDPT_LEVEL",
                             "PML4_LEVEL"};


// TODO(tommyu): Is this ok usage?
using namespace umm;
using umm::lin_addr;
using umm::simple_pte;

UmPgTblMgmt::beforeRecFn nullBRFn   = [](simple_pte *curPte, uint8_t lvl){};
UmPgTblMgmt::afterRecFn  nullARFn   = [](simple_pte *childPte, simple_pte *curPte, uint8_t lvl){};
UmPgTblMgmt::leafFn      nullLFn    = nullBRFn;
UmPgTblMgmt::beforeRetFn nullBRetFn = nullBRFn;

void UmPgTblMgmt::freePageTableLamb(simple_pte *root, unsigned char lvl){
  // Free leaves.
  auto leafFn = [](simple_pte *curPte, uint8_t lvl){
    // NOTE: Rule we use here is only free page if you have write access!!!
    if( curPte->decompCommon.RW == 1){
    ebbrt::Pfn myPFN = ebbrt::Pfn::Down(curPte->pageTabEntToAddr(lvl).raw);
    // kprintf(RED "Free physical page at %p\n" RESET, myPFN.ToAddr());

    kassert(orders[lvl] == 0);

    ebbrt::page_allocator->Free(myPFN, orders[lvl]);
    }
  };

  // Free tables.
  auto bretFn = [](simple_pte *root, uint8_t lvl){
    ebbrt::Pfn myPFN = ebbrt::Pfn::Down(root);
    // kprintf(CYAN "Free page table at %p\n" RESET, myPFN.ToAddr());

    // Tables must be 1 4k page.
    int order = 0;
    ebbrt::page_allocator->Free(myPFN, order);
  };

  traverseAccessedPages(root, lvl, leafFn, bretFn);
}

// NOTE: World of lambdas begins here.
void UmPgTblMgmt::countValidPagesLamb(std::vector<uint64_t> &counts,
                                simple_pte *root, uint8_t lvl) {
  // Counts number mapped pages.
  auto leafFn = [&counts](simple_pte *curPte, uint8_t lvl){
    counts[lvl]++;
  };
  traverseValidPages(root, lvl, leafFn);
}

// void UmPgTblMgmt::countValidWritePagesLamb(std::vector<uint64_t> &counts,
//                                       simple_pte *root, uint8_t lvl) {
//   // HACK
//   // Counts number mapped pages.
//   auto leafFn = [&counts](simple_pte *curPte, uint8_t lvl){
//     counts[lvl]++;
//   };
//   traverseWriteablePages(root, lvl, leafFn);
// }

// TODO not used delete me.
void UmPgTblMgmt::doubleCacheInvalidate(simple_pte *root, uint8_t lvl){
  printf("Flushing translation caches two ways\n");
  cacheInvalidateValidPagesLamb(root, lvl);
  flushTranslationCaches();
}

void UmPgTblMgmt::cacheInvalidateValidPagesLamb(simple_pte *root, uint8_t lvl) {
  // Counts number mapped pages.
  auto leafFn = [](simple_pte *curPte, uint8_t lvl){
    invlpg(nextTableOrFrame(curPte, 0, lvl));
  };
  traverseValidPages(const_cast<umm::simple_pte*>(root), lvl, leafFn);
}

void UmPgTblMgmt::countValidPTEsLamb(std::vector<uint64_t> &counts,
                                     simple_pte *root, uint8_t lvl) {
  // Little bit of a HACK to put this in the predicate, but whatever.
  auto pred = [&counts](simple_pte *curPte, uint8_t lvl) -> bool {
    bool ret = exists(curPte);
    if(ret){
      counts[lvl]++;
    }
    return ret;
  };
  traversePageTable(root, lvl, pred, nullBRFn, nullARFn, nullLFn, nullBRetFn );
}

void UmPgTblMgmt::countAccessedPagesLamb(std::vector<uint64_t> &counts,
                                     simple_pte *root, uint8_t lvl) {
  // Counts number mapped pages.
  auto leafFn = [&counts](simple_pte *curPte, uint8_t lvl){
    counts[lvl]++;
  };
  traverseAccessedPages(root, lvl, leafFn);
}

void UmPgTblMgmt::countDirtyPagesLamb(std::vector<uint64_t> &counts,
                                     simple_pte *root, uint8_t lvl) {
  // Counts number dirty pages.
  auto leafFn = [&counts](simple_pte *curPte, uint8_t lvl){
    if(isLeaf(curPte, lvl))
      if(isDirty(curPte))
      counts[lvl]++;
  };
  // NOTE: Trying walking accessed, not valid.
  traverseAccessedPages(root, lvl, leafFn);
}

void UmPgTblMgmt::countWritablePagesLamb(std::vector<uint64_t> &counts,
                                      simple_pte *root, uint8_t lvl) {
  // Counts number dirty pages.
  auto leafFn = [&counts](simple_pte *curPte, uint8_t lvl){
                  if(isLeaf(curPte, lvl))
                    if(isWritable(curPte))
                      counts[lvl]++;
                };
  // NOTE: Trying walking accessed, not valid.
  traverseAccessedPages(root, lvl, leafFn);
}
void UmPgTblMgmt::printTraversalLamb(simple_pte *root, uint8_t lvl) {
  // Dummy example for how one might use the general traverser.
  auto predicate = [](simple_pte *curPte, uint8_t lvl) -> bool {
    return exists(curPte);
  };
  auto beforeRecursiveCall = [](simple_pte *curPte, uint8_t lvl) {
    printf("Before Recursion\n");
  };
  auto afterRecursiveCall = [](simple_pte *childPte, simple_pte *curPte, uint8_t lvl) {
    printf("After Recursion\n");
  };
  auto leaf = [](simple_pte *curPte, uint8_t lvl) {
    printf("At leaf\n");
  };
  auto beforeReturn = [](simple_pte *root, uint8_t lvl) {
    printf("About to return\n");
  };

  // TODO(tommyu): fn suffix.
  traversePageTable(root, lvl,
                    predicate,
                    beforeRecursiveCall,
                    afterRecursiveCall,
                    leaf,
                    beforeReturn
                    );
}

// void UmPgTblMgmt::traverseWriteablePages(simple_pte *root, uint8_t lvl, leafFn L) {
//   auto pred = [](simple_pte *curPte, uint8_t lvl) -> bool {
//     // exists() is redundant assuming non existant ptes are 0.
//     return exists(curPte) && isAccessed(curPte) && isWritable(curPte);
//   };

//   traversePageTable(root, lvl, pred, nullBRFn, nullARFn, L, nullBRetFn);
// }

void UmPgTblMgmt::traverseAccessedPages(simple_pte *root, uint8_t lvl, leafFn L) {
  auto pred = [](simple_pte *curPte, uint8_t lvl) -> bool {
    // exists() is redundant assuming non existant ptes are 0.
    return exists(curPte) && isAccessed(curPte);
  };

  traversePageTable(root, lvl, pred, nullBRFn, nullARFn, L, nullBRetFn);
}

void UmPgTblMgmt::traverseAccessedPages(simple_pte *root, uint8_t lvl, leafFn L, beforeRetFn BRET) {
  auto pred = [](simple_pte *curPte, uint8_t lvl) -> bool {
    // exists() is redundant assuming non existant ptes are 0.
    return exists(curPte) && isAccessed(curPte);
  };

  traversePageTable(root, lvl, pred, nullBRFn, nullARFn, L, BRET);
}

void UmPgTblMgmt::traverseValidPages(simple_pte *root, uint8_t lvl, leafFn L) {
  traverseValidPages(root, lvl, nullBRFn, nullARFn, L, nullBRetFn);
}

void UmPgTblMgmt::traverseValidPages(simple_pte *root, uint8_t lvl,
                                     beforeRecFn BR, afterRecFn AR, leafFn L, beforeRetFn BRET) {
  auto pred = [](simple_pte *curPte, uint8_t lvl) -> bool {
    return exists(curPte);
  };
  traversePageTable(root, lvl, pred, BR, AR, L, BRET);
}

// Continue predicate, before recursion, after recursion, leaf, before return.
simple_pte *UmPgTblMgmt::traversePageTable(simple_pte *root, uint8_t lvl,
                                           predicateFn P, beforeRecFn BR, afterRecFn AR,
                                           leafFn L, beforeRetFn BRET) {
  // A general page table traverser. Intention is not to call this directly
  // from client code, but to make specializations, like a fn that only walks
  // valid pages, accessed pages, dirty pages ...
  for (int i = 0; i < 512; i++) { // Loop over all entries in table.
    simple_pte *curPte = root + i;

    if (!P(curPte, lvl)) // ! allows pred to stay in positive sense for caller.
      continue;

    if (isLeaf(curPte, lvl)) { // -> a physical page of some sz.
      L(curPte, lvl);

    } else { // This entry points to a sub page table.
      BR(curPte, lvl);
      simple_pte *childRoot = traversePageTable(nextTableOrFrame(root, i, lvl), lvl - 1, P, BR, AR, L, BRET);
      // NOTE: This guy takes an extra arg.
      AR(childRoot, curPte, lvl);
    }
  }
  // NOTE: This guy takes root, not curPte!
  BRET(root, lvl);
  return root;
}

#if 0
simple_pte *UmPgTblMgmt::mapIntoPgTblLamb(simple_pte *root, lin_addr phys, lin_addr virt,
                                          unsigned char mapLvl, unsigned char curLvl) {
  auto f1 = [phys, virt, mapLvl](){
    // No pg table here, need to allocate.
    if (root == nullptr) {
      // printf("Root doesn't exist! Allocate a table.\n");
      auto page = ebbrt::page_allocator->Alloc();
      kbugon(page == Pfn::None());
      auto page_addr = page.ToAddr();
      memset((void *)page_addr, 0, pgBytes[TBL_LEVEL]);
      root = (simple_pte *)page_addr;
    }
  }
}
#endif

lin_addr UmPgTblMgmt::getPhysAddrLamb(lin_addr la, simple_pte* root, unsigned char lvl) {
  lin_addr ret; ret.raw = 0;

  auto leafFn = [](simple_pte *curPte, lin_addr virt, uint8_t lvl) -> uintptr_t{
    lin_addr pa;
    pa.raw = (uint64_t) nextTableOrFrame(curPte, 0, lvl);
    pa.raw = injectOffset(virt, lvl);
    return pa.raw;
  };

  ret.raw = walkPageTable(root, lvl, la, leafFn);
  return ret;
}

simple_pte *UmPgTblMgmt::addrToPTELamb(lin_addr la, simple_pte* root, unsigned char lvl) {
  simple_pte *pte;

  auto leafFn = [](simple_pte *curPte, lin_addr virt, uint8_t lvl) -> uintptr_t{
    return (uintptr_t)curPte;
  };

  pte = (simple_pte *) walkPageTable(root, lvl, la, leafFn);
  return pte;
}

void UmPgTblMgmt::dumpAllPTEsWalkLamb(lin_addr la, simple_pte* root,
                                            unsigned char lvl) {
  // prints out all PTEs relevant for walking la.
  printf("In %s\n", __func__);
  auto recFn = [](simple_pte *curPte, lin_addr virt, uint8_t lvl) -> void{
    printf("Lvl is %d\n", lvl);
    curPte->printCommon();
  };

  auto leafFn = [](simple_pte *curPte, lin_addr virt, uint8_t lvl) -> uintptr_t{
    return 0;
  };

  walkPageTable(root, lvl, la, recFn, leafFn);
}

// TODO not used delete me
void UmPgTblMgmt::setUserAllPTEsWalkLamb(lin_addr la, simple_pte* root,
                                      unsigned char lvl) {
  printf("In %s\n", __func__);
  auto recFn = [](simple_pte *curPte, lin_addr virt, uint8_t lvl) -> void{
    // Set user bit.
    curPte->raw = curPte->raw | 1 << 2;
  };

  auto leafFn = [](simple_pte *curPte, lin_addr virt, uint8_t lvl) -> uintptr_t{
    return 0;
  };

  walkPageTable(root, lvl, la, recFn, leafFn);
  // HACK: Super overkill, but being overly conservative.
  flushTranslationCaches();
}

uintptr_t UmPgTblMgmt::walkPageTable(simple_pte *root, uint8_t lvl,
                                     lin_addr virt, walkLeafFn L) {
  // Given a virtual address, walk down to mapping level. Assume can be used,
  // for example, by a mapper to inject an address at a given level, or by a
  // query for the physical frame mapped by a particular address.

  // Before doing anything, maybe nothing, maybe check if root is nullptr.

  simple_pte *curPTE = root + virt[lvl];

  if(isLeaf(curPTE, lvl)){
    return L(curPTE, virt, lvl);
  }
  // printf("We exist, but we're not a leaf, recurse!\n");
  return walkPageTable(nextTableOrFrame(root, virt[lvl], lvl), lvl-1, virt, L);
}

uintptr_t UmPgTblMgmt::walkPageTable(simple_pte *root, uint8_t lvl,
                                     lin_addr virt, walkRecFn R, walkLeafFn L) {
  simple_pte *curPTE = root + virt[lvl];
  R(curPTE, virt, lvl);
  if(isLeaf(curPTE, lvl)){
    return L(curPTE, virt, lvl);
  }
  return walkPageTable(nextTableOrFrame(root, virt[lvl], lvl), lvl-1, virt, R, L);
}

// Printer helper.
void UmPgTblMgmt::alignToLvl(unsigned char lvl){
  for (int j = 0; j<4-lvl; j++) printf("\t");
}

#if 0
void UmPgTblMgmt::reclaimAllPages(simple_pte *root, unsigned char lvl,
                                  bool reclaimPhysical /*=true*/) {
  // HACK(tommyu): Fails to remove top level table.

  // Loop over all entries in table.
  for (int i = 0; i < 512; i++) {
    simple_pte *curPte = root + i;
    ebbrt::Pfn myPFN = ebbrt::Pfn::Down(curPte->pageTabEntToAddr(lvl).raw);
    if (!exists(curPte)) continue;

    if (isLeaf(curPte, lvl)) {
      if (reclaimPhysical) {
        // Reclaim the leaf phys page.
        printf(RED "Free physical page at addr, %p\n" RESET, myPFN.ToAddr());

        // NOTE NYI
        kassert(orders[lvl] == 0);
        // Pfn expects a page number, not an address.
        ebbrt::page_allocator->Free(myPFN, orders[lvl]);
        // Remove entry.
        curPte->raw = 0;
      }
    } else {
      // We're > TBL_LEVEL & pointing to another table.

      // Reclaim below
      reclaimAllPages(nextTableOrFrame(root, i, lvl), lvl - 1);

      // Finished our work on that child, remove mapping.
      curPte->raw = 0;
    }
  }
  ebbrt::Pfn myPFN = ebbrt::Pfn::Down(root);
  printf(RED "Currently on %s, Free page %p at addr %p\n" RESET,
         level_names[lvl], myPFN, myPFN.ToAddr());
  ebbrt::page_allocator->Free(myPFN, orders[TBL_LEVEL]);
}
#endif

void UmPgTblMgmt::dumpFullTableAddrs(simple_pte *root, unsigned char lvl){
  printf(GREEN "Root at %s: %p\n" RESET, level_names[lvl], root);
  dumpFullTableAddrsHelper(root, lvl);
}

void UmPgTblMgmt::dumpFullTableAddrsHelper(simple_pte *root, unsigned char lvl){
  // Dump contents of entire table.
  // Open brace.
  if(root == nullptr){
    kabort("Root is nullptr\n");
  }


  alignToLvl(lvl);
  printf("%s " CYAN "@ %p" RESET "[\n", level_names[lvl], root);

  for (int i = 0; i < 512; i++) {
    if (!exists(root + i)) continue;

    // Dump Address, print phys in red.
    if(isLeaf(root + i,lvl)){
      alignToLvl(lvl);
      printf(RED "%d -> %p\n" RESET, i, (root+i)->pageTabEntToAddr(lvl));
      (root+i)->printCommon();
    } else {
      alignToLvl(lvl);
      printf(CYAN "%d ->\n" RESET, i);
    }

    if(! isLeaf(root + i, lvl)){
      // Recurse.
      dumpFullTableAddrsHelper(nextTableOrFrame(root, i, lvl), lvl - 1);
    }
  }
  //Close brace.
  alignToLvl(lvl); printf("\t");
  printf("]\n");
}

void UmPgTblMgmt::dumpTableAddrs(simple_pte *root, unsigned char lvl){
  // Dump contents of single table.
  for (int i = 0; i < 512; i++) {
    // Skip zero entries.
    lin_addr val = (root + i)->pageTabEntToAddr(lvl);
    if(val.raw == 0) continue;

    // Indent to lvl.
    for (int j = 0; j<4-lvl; j++) printf("\t");

    // Print address.
    printf(CYAN "%d -> %p\n" RESET, i, val.raw);
  }
}

void UmPgTblMgmt::countDirtyPagesHelper(std::vector<uint64_t> &counts,
                                        simple_pte *root, uint8_t lvl) {
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i)) {
      continue;
    }
    if (isLeaf(root + i, lvl)) {
      // printf("Found a leaf at level %d\n", lvl);
      // printf("%d >\n", i);
      if (isDirty(root + i)) {
        counts[lvl]++;
      }
    } else {
      countDirtyPagesHelper(counts, nextTableOrFrame(root, i, lvl), lvl - 1);
    }
  }
}

void UmPgTblMgmt::countDirtyPages(std::vector<uint64_t> &counts, simple_pte *root, uint8_t lvl) {
  // Going to grab PML4, so better be lvl 4.
  if(root == nullptr){
    kassert(lvl == 4);
  }

  if (root == nullptr){
    root = getPML4Root();
  }
  kassert(counts.size() == 5);
  countDirtyPagesHelper(counts, root, lvl);
}

void UmPgTblMgmt::countAccessedPagesHelper(std::vector<uint64_t> &counts,
                                           simple_pte *root, uint8_t lvl) {
  // Counts accessed leaf pages at various levels.
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i))
      continue;
    if (isLeaf(root + i, lvl)) {
      if (isAccessed(root + i)) {
        counts[lvl]++;
      }
    } else {
      countAccessedPagesHelper(counts, nextTableOrFrame(root, i, lvl), lvl - 1);
    }
  }
}

void UmPgTblMgmt::countAccessedPages(std::vector<uint64_t> &counts, simple_pte *root, uint8_t lvl) {
  // Going to grab PML4, so better be lvl 4.
  if(root == nullptr){
    kassert(lvl == 4);
  }

  if (root == nullptr){
    root = getPML4Root();
  }
  kassert(counts.size() == 5);
  countAccessedPagesHelper(counts, root, lvl);
}

void UmPgTblMgmt::countValidPTEsHelper(std::vector<uint64_t> &counts,
                                       simple_pte *root, uint8_t lvl) {
  // Counts valid leaf pages at various levels.
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i)) continue;

    counts[lvl]++;
    if(lvl > TBL_LEVEL){
      countValidPTEsHelper(counts, nextTableOrFrame(root, i, lvl), lvl - 1);
    }
  }
}

void UmPgTblMgmt::countValidPTEs(std::vector<uint64_t> &counts, simple_pte *root, uint8_t lvl) {
  // Going to grab PML4, so better be lvl 4.
  if(root == nullptr){
    kassert(lvl == 4);
  }

  if (root == nullptr){
    root = getPML4Root();
  }
  kassert(counts.size() == 5);
  countValidPTEsHelper(counts, root, lvl);
}

void UmPgTblMgmt::countValidPagesHelper(std::vector<uint64_t> &counts,
                                        simple_pte *root, uint8_t lvl) {
  // Counts valid leaf pages at various levels.
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i))
      continue;
    if (isLeaf(root + i, lvl)) {
      counts[lvl]++;
    } else {
      countValidPagesHelper(counts, nextTableOrFrame(root, i, lvl), lvl - 1);
    }
  }
}

void UmPgTblMgmt::countValidPages(std::vector<uint64_t> &counts, simple_pte *root, uint8_t lvl) {
  // Going to grab PML4, so better be lvl 4.
  if(root == nullptr){
    kassert(lvl == 4);
  }

  if (root == nullptr){
    root = getPML4Root();
  }
  kassert(counts.size() == 5);
  countValidPagesHelper(counts, root, lvl);
}

uintptr_t UmPgTblMgmt::injectOffset(lin_addr la, unsigned char lvl){
  lin_addr pa;
  switch (lvl) {
  case PDPT_LEVEL: pa.construct_addr_1G.OFFSET = la.decomp1G.PGOFFSET; break;
  case DIR_LEVEL: pa.construct_addr_2M.OFFSET = la.decomp2M.PGOFFSET; break;
  case TBL_LEVEL: pa.construct_addr_4K.OFFSET = la.decomp4K.PGOFFSET;}
  return pa.raw;
}

lin_addr UmPgTblMgmt::getPhysAddrRecHelper(lin_addr la, simple_pte* root, unsigned char lvl) {
  printf("la is %#0lx, offset is %lu, root is %p, lvl is %u\n", la.raw, la[lvl], root, lvl);

  if(!exists(root + la[lvl])){
    printf("Doesn't exist, our work is done! \n");
    kassert(false);
  }

  if (isLeaf(root + la[lvl], lvl)) {
    lin_addr pa;
    printf("This guy is a leaf! Follow final table inderection & Return the addr \n");
    // TODO: Should this really be modifying root?
    // root = nextTableOrFrame(root, la[lvl], lvl);
    // pa.raw = (uint64_t)root;
    pa.raw = (uint64_t) nextTableOrFrame(root, la[lvl], lvl);
    pa.raw = injectOffset(la, lvl);
    return pa;
  }

  // Recurse.
  printf("We exist, but we're not a leaf, recurse!\n");
  return getPhysAddrRecHelper(la, nextTableOrFrame(root, la[lvl], lvl), lvl-1);

}

lin_addr UmPgTblMgmt::getPhysAddrRec(lin_addr la, simple_pte* root /*=nullptr*/, unsigned char lvl /*=4*/) {
  // Error checks and hands off to helper.

  // Valid range for 4 level paging.
  kassert(lvl >= TBL_LEVEL && lvl <= PML4_LEVEL);

  if (root == nullptr) {
    // If coming from top, gotta get root from CR3.
    if (lvl == PML4_LEVEL) {
      root = getPML4Root();
    }
  } else {
    // If have root, better be below pml4.
    kassert(lvl < PML4_LEVEL);
  }

  // Better have a root to follow.
  kassert(root != nullptr);

  return getPhysAddrRecHelper(la, root, lvl);
}

simple_pte* UmPgTblMgmt::getSlotRoot(){
  return getPML4Root() + kSlotPML4Offset;
}

// TODO(tommyu): Delete me.
simple_pte* UmPgTblMgmt::getSlotPDPTRoot(){
  // Root of slot.
  simple_pte *root = getPML4Root();
  if(!exists(root + SLOT_PML4_NUM)){
    printf("Got a problem, looks like no Slot PDPT loaded\n");
    kassert(false);
  }
  return nextTableOrFrame(getPML4Root(), SLOT_PML4_NUM, 4);
}

void simple_pte::underlineNibbles(){
  printf("\t ");
  for(int i = 0; i<64; i++){
    if(i%8 < 4){
      printf(CYAN "__" RESET);
    }else{
      printf(YELLOW "__" RESET);
    }
  }
  printf("\n");
}

void simple_pte::printNibblesHex(){
  // Iterate over nibbles msn to lsn.
  uint64_t mask = 0xfULL << 60;
  printf("     ");
  for(unsigned int i=0; i < 2*sizeof(raw); i++, mask >>= 4){
    printf("       %lx", (raw & mask) >> (60 - (4 * i)));
  }
  printf("\n");
}

void simple_pte::printBits(uint64_t val, int len) {
  if (len < 0)
    return;
  for (len = len - 1; len > 0; len--) {
    if (val & (1ULL << len))
      printf(RED "1 " RESET);
    else
      printf("0 ");
  }
  if (val & (1ULL << len))
    printf(RED "1" RESET);
  else
    printf("0");
}

void simple_pte::printCommon() {
  printf("\t|                       |     1G                                    |                                 |P|     | |M| | |P|P|U|R|S|\n");
  printf("\t|       Reserved        |               2M          Address of PTE Table              |               |A| IGN |G|A|D|A|C|W|/|/|E|\n");
  printf("\t|                       |                                  4K                                         |T|     | |P| | |D|T|S|W|L|\n");
  printf("\t|");
  printBits(decompCommon.RES,         12); printf("|");
  printBits(decompCommon.PG_TBL_ADDR, 40); printf("|");
  printBits(decompCommon.WHOCARES2,   4); printf("|");
  printBits(decompCommon.MAPS,        1); printf("|");
  printBits(decompCommon.DIRTY,       1); printf("|");
  printBits(decompCommon.A,           1); printf("|");
  printBits(decompCommon.PCD,         1); printf("|");
  printBits(decompCommon.PWT,         1); printf("|");
  printBits(decompCommon.US,          1); printf("|");
  printBits(decompCommon.RW,          1); printf("|");
  printBits(decompCommon.SEL,         1); printf("|\n");
  underlineNibbles();
  printNibblesHex();
}

// TODO(tommyu): No attmpt at performance.
uint64_t simple_pte::pageTabEntToPFN(unsigned char lvl) {
  // Get page frame number corresponding to addr.
  // Because I haven't thought about this.
  return pageTabEntToAddr(lvl).raw >> 12;
}


lin_addr simple_pte::pageTabEntToAddr(unsigned char lvl) {
  lin_addr la;
  // Refer to figure 4-11 in Vol 3A of the intel man.
  // printf("in page tab ent to addr lvl is %d, maps is %u \n", lvl, decompCommon.MAPS);
  switch (lvl) {
  case PML4_LEVEL:
    // printf("PML4\n");
    // This PML4E points to a PDPT
    la.raw = (uint64_t)decompCommon.PG_TBL_ADDR << SMALL_PG_SHIFT;
    break;

  case PDPT_LEVEL:
    // printf("PDPT\n");
    if (decompCommon.MAPS) {
      // This PDPTE maps a 1GB page.
      la.raw = (uint64_t)decomp1G.PAGE_NUMBER << LG_PG_SHIFT;
    } else {
      // This PDPT points to a DIR.
      la.raw = (uint64_t)decompCommon.PG_TBL_ADDR << SMALL_PG_SHIFT;
    }
    break;

  case DIR_LEVEL:
    // printf("DIR\n");
    // TODO(tommyu): Put this in struct.
    if (decompCommon.MAPS) {
      // This DIRE maps a 2MB page.
      la.raw = (uint64_t)decomp2M.PAGE_NUMBER << MED_PG_SHIFT;
    } else {
      // This DIRE points to a PT.
      la.raw = (uint64_t)decompCommon.PG_TBL_ADDR << SMALL_PG_SHIFT;
    }
    break;

  case TBL_LEVEL:
    // printf("TAB\n");
    // This PTE maps a 4KB page.
    la.raw = (uint64_t)decomp4K.PAGE_NUMBER << SMALL_PG_SHIFT;
    break;
  default :
    la.raw = 0;
    kassert(false);
  }

  // printf("raw=%p \t ret=%p\n", raw, la.raw);
  return la;
}

lin_addr  simple_pte::cr3ToAddr(){
  // Not easy to integrate into pageTabEntToAddr.
  lin_addr la;
  la.raw = (uint64_t)decompCommon.PG_TBL_ADDR << SMALL_PG_SHIFT;

  return la;
}

simple_pte* UmPgTblMgmt::nextTableOrFrame(simple_pte* pg_tbl_start, uint64_t pg_tbl_offset, unsigned char lvl) {
  // Takes ptr to start of page table and offset, returns ptr to next table.
  simple_pte *pte_ptr;

  // printf(YELLOW "%s Table starts at %p, offset is 0x%llx \n" RESET, level_names[lvl],  pg_tbl_start, pg_tbl_offset);

  // Add the offset to get ptr to relevant PTE in this table.
  pte_ptr = pg_tbl_start + pg_tbl_offset;
  // printf(YELLOW "PTE is at %p, it's value is %p\n" RESET, pte_ptr, pte_ptr->raw);

  // Deref gives the relevant PTE of the current table.
  simple_pte pte = *pte_ptr;

  // Guarantee it actually exists.
  kassert(exists(pte_ptr));

  // Convert page number of PTE to an address. This is a pointer to the next level page table.
  simple_pte *ret = (simple_pte *)pte.pageTabEntToAddr(lvl).raw;
  // printf("lvl is %d Next table starts at %p\n" RESET, lvl, ret);

  return ret;
}

simple_pte* UmPgTblMgmt::getPML4Root(){
  simple_pte cr3;
  asm volatile("mov %%cr3, %[cr3]" : [cr3] "=r"(cr3));
  return (simple_pte *) cr3.cr3ToAddr().raw;
}

void UmPgTblMgmt::flushTranslationCaches(){
  // Flushes TLB & intermediate caches. There are options for flushing individual
  // pages using invlpg as well as marking some pages global / using ASID.
  // Here we keep it simple, and blow everything away.
  // printf("Flushing address translation caches!\n");
  // TODO: We're suspicious of this cr3 load being sufficient.
  asm volatile("movq %cr3, %rax");
  asm volatile("movq %rax, %cr3");
  // TODO: Theoretically redundant.
}

bool UmPgTblMgmt::exists(simple_pte *pte){
  if(pte->decompCommon.SEL == 1)
    return true;
  return false;
}

bool UmPgTblMgmt::isWritable(simple_pte *pte){
  if(pte->decompCommon.RW == 1){
    return true;
  }
  return false;
}

bool UmPgTblMgmt::isReadOnly(simple_pte *pte){
  if(pte->decompCommon.RW == 0){
    return true;
  }
  return false;
}

bool UmPgTblMgmt::isDirty(simple_pte *pte){
  if(pte->decompCommon.DIRTY){
    return true;
  }
  return false;
}

bool UmPgTblMgmt::isAccessed(simple_pte *pte){
  if(pte->decompCommon.A){
    return true;
  }
  return false;
}

bool UmPgTblMgmt::isLeaf(simple_pte *pte, unsigned char lvl){
  if(lvl == TBL_LEVEL){
    return true;
  }

  // No worries about PML4, reserved to 0;
  if(pte->decompCommon.MAPS){
    return true;
  }

  return false;
}

simple_pte * UmPgTblMgmt::walkPgTblCOW(simple_pte *root, simple_pte *copy, uint8_t lvl) {
  // Entry 0 is bogus and unused
  // HACK(tommyu): trying to get off the ground.
  uint64_t idx[5] = {0};
  // HACK(tommyu): This is actually critical.
  idx[4] = SLOT_PML4_NUM;
  // kprintf(CYAN "COW pg tbl copy\n" RESET);
  return walkPgTblCOWHelper(root, copy, lvl, idx);
}

simple_pte * UmPgTblMgmt::walkPgTblCopyDirtyCOW(simple_pte *root, simple_pte *copy, uint8_t lvl) {
  // Entry 0 is bogus and unused
  // HACK(tommyu): trying to get off the ground.
  uint64_t idx[5] = {0};
  // HACK(tommyu): This is actually critical.
  idx[4] = SLOT_PML4_NUM;
  // kprintf(RED "Snapshot COW\n" RESET);
  return walkPgTblCopyDirtyCOWHelper(root, copy, lvl, idx);
}

simple_pte * UmPgTblMgmt::walkPgTblCopyDirty(simple_pte *root, simple_pte *copy, uint8_t lvl) {
  // Entry 0 is bogus and unused
  // HACK(tommyu): trying to get off the ground.
  uint64_t idx[5] = {0};
  // HACK(tommyu): This is actually critical.
  idx[4] = SLOT_PML4_NUM;
  kprintf(MAGENTA "Deep pg tbl copy\n" RESET);
  return walkPgTblCopyDirtyHelper(root, copy, lvl, idx);
}

simple_pte * UmPgTblMgmt::walkPgTblCopyDirty(simple_pte *root, simple_pte *copy) {
  // Entry 0 is bogus and unused

  // HACK(tommyu): trying to get off the ground.
  uint64_t idx[5] = {0};

  if (root == nullptr){
    root = getSlotPDPTRoot();
  }
  unsigned char lvl = 3;

  // HACK(tommyu): This is actually critical.
  idx[4] = SLOT_PML4_NUM;

  simple_pte *ret = walkPgTblCopyDirtyHelper(root, copy, lvl, idx);

  return ret;
}

lin_addr UmPgTblMgmt::reconstructLinAddrPgFromOffsets(uint64_t *idx){
  lin_addr la;
  la.raw = 0;
  la.tblOffsets.PML4 = idx[PML4_LEVEL];
  la.tblOffsets.PDPT = idx[PDPT_LEVEL];
  la.tblOffsets.DIR = idx[DIR_LEVEL];
  la.tblOffsets.TAB = idx[TBL_LEVEL];

  // HACK(tommyu): Something about cannonical addressing? Need to do some reading.
  // Think this should only be applied if pml4 num >= 256 aka 0x100, half 512, 0x200.
  if (idx[PML4_LEVEL] >= 0x100)
    la.raw |= 0xffffUL << 48;
  return la;
}

lin_addr UmPgTblMgmt::copyDirtyPage(lin_addr src, unsigned char lvl){
  auto page = ebbrt::page_allocator->Alloc();
  // if(page == Pfn::None())
  //   kprintf_force(RED "Ran out of pages\n" RESET);
  kbugon(page == Pfn::None());
  auto page_addr = page.ToAddr();
  memcpy((void*)page_addr, (void*)src.raw, 1UL << pgShifts[lvl]);
  lin_addr la;
  la.raw = (uint64_t) page_addr;
  return la;

}
uint16_t lin_addr::operator[](uint8_t idx){
  // printf("Hi from [] operator! Got input %u \n", idx);
  uint64_t tmp = raw;
  // Remove offset;
  tmp >>= 12;
  // 1 gives tab, 2 dir ...
  for (int i = 0; i < idx - 1; i++)
    tmp >>= 9;

  // Lower 9 bits set. Same as 0x1FF.
  return tmp & ( (1 << 9) - 1 );
}

bool dbBool = true;

void simple_pte::clearPTE(){
  // Deref to pte & zero.
  this->raw = 0;
}

void simple_pte::setPte(simple_pte *tab,
                        bool dirty /*= false*/,
                        bool acc   /*= false*/,
                        bool rw    /*= true*/,
                        bool us    /*= false*/,
                        bool xd    /*= false*/){

  // TODO(tommyu): generalize to all levels.
  kassert((uint64_t)tab % (1 << 12) == 0);
  // printf("Have addr, returning pte\n");
  // printf("Addr is %p\n", tab);

  simple_pte pte; pte.raw = 0;
  pte.decompCommon.SEL   = 1;
  pte.decompCommon.RW    = (rw)    ? 1 : 0;
  pte.decompCommon.US    = (us)    ? 1 : 0;
  pte.decompCommon.A     = (acc)   ? 1 : 0;
  pte.decompCommon.DIRTY = (dirty) ? 1 : 0;
  pte.decompCommon.XD    = (xd)    ? 1 : 0;

  pte.decompCommon.PG_TBL_ADDR = (uint64_t)tab >> 12;

  raw = pte.raw;
}

simple_pte *UmPgTblMgmt::mapIntoPgTbl(simple_pte *root, lin_addr phys, lin_addr virt,
                                      unsigned char rootLvl, unsigned char mapLvl, unsigned char curLvl,
                                      bool writeFault, bool rdPerm, bool execDisable) {
  return mapIntoPgTblHelper(root, phys, virt,
                            rootLvl, mapLvl, curLvl,
                            writeFault, rdPerm, execDisable);
}

simple_pte *UmPgTblMgmt::mapIntoPgTblHelper(simple_pte *root, lin_addr phys, lin_addr virt,
                                            unsigned char rootLvl, unsigned char mapLvl, unsigned char curLvl,
                                            bool writeFault, bool rdPerm, bool execDisable) {
  kassert(rootLvl >= mapLvl);
  kassert(rootLvl >= curLvl);
  kassert(rootLvl <= PML4_LEVEL && rootLvl >= TBL_LEVEL);
  kassert(mapLvl <= PDPT_LEVEL);

  // No pg table here, need to allocate.
  if (root == nullptr) {
    auto page = ebbrt::page_allocator->Alloc();
    kbugon(page == Pfn::None());
    auto page_addr = page.ToAddr();
    memset((void *)page_addr, 0, pgBytes[TBL_LEVEL]);
    root = (simple_pte *)page_addr;
  }

  // Get offset using custom indexing operator.
  simple_pte *pte_ptr = root + virt[curLvl];
  // Gotta do a mapping
  if (curLvl == mapLvl) {
    // We're in the table, modify the entry & importantly mark it dirty.
    // TODO: Should this always be marked accessed? Def in copy dirty.
    // printf(MAGENTA "Mapping %p -> %p\n" RESET, virt.raw, phys.raw);
    // Mark mapping PTE user.
    // If write, mark dirty and R/W. Otherwise not dirty, read only.
    // pte_ptr->setPte((simple_pte *)phys.raw, writeFault, true, true, true);
    pte_ptr->setPte((simple_pte *)phys.raw, writeFault, true, rdPerm, true, execDisable);
  } else {
    if (exists(pte_ptr)) {
      // Recurse to next level
      mapIntoPgTbl(nextTableOrFrame(pte_ptr, 0, curLvl), phys, virt,
                   rootLvl, mapLvl, curLvl - 1,
                   writeFault, rdPerm, execDisable);
    } else {
      // Create next level and recurse.
      simple_pte *ret =
        mapIntoPgTbl(nullptr, phys, virt,
                     rootLvl, mapLvl, curLvl - 1,
                     writeFault, rdPerm, execDisable);
      // Dirty bit doesn't apply, accessed does.
      // Mark interior PTEs user.
      pte_ptr->setPte(ret, false, true, true, true);
    }
  }
  return root;
}

// HACK: Lots of copy pasta.
simple_pte *UmPgTblMgmt::findAndSetPTECOW(simple_pte *root, simple_pte *origPte,
                                           lin_addr virt, unsigned char rootLvl,
                                           unsigned char mapLvl,
                                           unsigned char curLvl) {
  kassert(rootLvl >= mapLvl);
  kassert(rootLvl >= curLvl);
  kassert(rootLvl <= PML4_LEVEL && rootLvl >= TBL_LEVEL);
  kassert(mapLvl <= PDPT_LEVEL);

  // No pg table here, need to allocate.
  if (root == nullptr) {
    auto page = ebbrt::page_allocator->Alloc();
    kbugon(page == Pfn::None());
    auto page_addr = page.ToAddr();
    // Set all invalid.
    memset((void *)page_addr, 0, pgBytes[TBL_LEVEL]);
    root = (simple_pte *)page_addr;
  }

  // Get offset using custom indexing operator.
  simple_pte *pte_ptr = root + virt[curLvl];
  // Gotta do a mapping
  if (curLvl == mapLvl) {
    // We're in the table, modify the entry & importantly mark it dirty.
    // TODO: Should this always be marked accessed? Def in copy dirty.
    // NOTE: Setting read only access.
    pte_ptr->setPte((simple_pte *) origPte->pageTabEntToAddr(TBL_LEVEL).raw, true, true, false, true); // TODO
  } else {
    if (exists(pte_ptr)) {
      findAndSetPTECOW(nextTableOrFrame(pte_ptr, 0, curLvl), origPte, virt, rootLvl, mapLvl, curLvl - 1);
    } else {
      simple_pte *ret =
        findAndSetPTECOW(nullptr, origPte, virt, rootLvl, mapLvl, curLvl - 1);
      // Dirty bit doesn't apply, accessed does.
      pte_ptr->setPte(ret, false, true, true, true);    // TODO tu, attention
    }
  }
  return root;
}

simple_pte *UmPgTblMgmt::walkPgTblCopyDirtyHelper(simple_pte *root,
                                                 simple_pte *copy,
                                                 unsigned char lvl,
                                                 uint64_t *idx) {
  // HACK(tommyu): This is a dirty hack to reconstruct the linear addr that got
  // you to this dirty page. I think we can do better.
  // Scan all 512 entries, if predicate holds, recurse.
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i))
      continue;
    idx[lvl] = i;

    if (isLeaf(root + i, lvl)) {
      // Higher NYI
      kassert(lvl == 1);
      if ((root + i)->decompCommon.DIRTY) {
        // Allocate new page and make copy.
        lin_addr backing;
        backing.raw = (root + i)->pageTabEntToAddr(lvl).raw;
        // Copy onto new page.
        lin_addr phys = copyDirtyPage(backing, lvl);

        // TODO(tommyu) is there a better way?
        // Reconstruct page Lin Addr.
        lin_addr virt = reconstructLinAddrPgFromOffsets(idx);

        copy = mapIntoPgTbl(copy, phys, virt, PDPT_LEVEL, lvl, PDPT_LEVEL, true);
      }
    } else {
      // Don't alter your own root, or you will break on the next goaround.
      copy = walkPgTblCopyDirtyHelper(nextTableOrFrame(root, i, lvl),
                                      copy, lvl - 1, idx);
    }
  }
  return copy;
}

simple_pte *UmPgTblMgmt::walkPgTblCopyDirtyCOWHelper(simple_pte *root,
                                                 simple_pte *copy,
                                                 unsigned char lvl,
                                                 uint64_t *idx) {
  // HACK(tommyu): This is a dirty hack to reconstruct the linear addr that got
  // you to this dirty page. I think we can do better.
  // Scan all 512 entries, if predicate holds, recurse.
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i))
      continue;
    idx[lvl] = i;

    if (isLeaf(root + i, lvl)) {
      // Higher NYI
      kassert(lvl == 1);
      if ((root + i)->decompCommon.DIRTY ) {
        // TODO(tommyu) is there a better way?
        // Reconstruct page Lin Addr.
        lin_addr virt = reconstructLinAddrPgFromOffsets(idx);
        if ((root + i)->decompCommon.RW == 1) {
          // This page was faulted in during the running of this instance, need
          // deep copy.
          // Allocate new page and make copy.
          lin_addr backing;
          backing.raw = (root + i)->pageTabEntToAddr(lvl).raw;
          // Copy onto new page.
          lin_addr phys = copyDirtyPage(backing, lvl);

          // umm::manager->num_cp_pgs++;

          // Super useful
          // kprintf_force(GREEN "C" RESET);

          copy =
              mapIntoPgTbl(copy, phys, virt, PDPT_LEVEL, lvl, PDPT_LEVEL, true);
        } else {
          // This is just a pointer to a page that exists in a previous
          // snapshot. Just create a pointer here.
          // copy =
          //   mapIntoPgTbl(copy, phys, virt, PDPT_LEVEL, lvl, PDPT_LEVEL, true);
          simple_pte *thisPte = root + i;
          copy = findAndSetPTECOW(copy, thisPte, virt, PDPT_LEVEL, lvl, PDPT_LEVEL);
        }
      }
    } else {
      // Don't alter your own root, or you will break on the next goaround.
      copy = walkPgTblCopyDirtyCOWHelper(nextTableOrFrame(root, i, lvl),
                                      copy, lvl - 1, idx);
    }
  }
  return copy;
}

simple_pte *UmPgTblMgmt::walkPgTblCOWHelper(simple_pte *root,
                                                 simple_pte *copy,
                                                 unsigned char lvl,
                                                 uint64_t *idx) {
  // HACK(tommyu): This is a dirty hack to reconstruct the linear addr that got
  // you to this dirty page. I think we can do better.
  // Scan all 512 entries, if predicate holds, recurse.
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i))
      continue;
    idx[lvl] = i;

    if (isLeaf(root + i, lvl)) {
      // Higher NYI
      kassert(lvl == 1);
      if ((root + i)->decompCommon.DIRTY) {
        // TODO: I think we can do this for all pages, not just dirty.

        // Allocate new page and make copy.
        // lin_addr backing;
        // backing.raw = (root + i)->pageTabEntToAddr(lvl).raw;
        // Copy onto new page.
        // lin_addr phys = copyDirtyPage(backing, lvl);

        // TODO(tommyu) is there a better way?
        // Reconstruct page Lin Addr.
        lin_addr virt = reconstructLinAddrPgFromOffsets(idx);

        // Super useful
        // kprintf_force(GREEN "C" RESET);

        // copy = mapIntoPgTbl(copy, phys, virt, PDPT_LEVEL, lvl, PDPT_LEVEL);
        simple_pte *thisPte = root + i;
        copy = findAndSetPTECOW(copy, thisPte, virt, PDPT_LEVEL, lvl, PDPT_LEVEL);

      }
    } else {
      // Don't alter your own root, or you will break on the next goaround.
      copy = walkPgTblCOWHelper(nextTableOrFrame(root, i, lvl),
                                      copy, lvl - 1, idx);
    }
  }
  return copy;
}

#if 0

void testResolveGoodAddr(){
  int my_var = 666;
  lin_addr myVarLA;
  myVarLA.raw = (uint64_t) &my_var;

  pgTabTool ptt;
  lin_addr pa = ptt.getPhysAddrRec(myVarLA);
  printf("Phys addr is %#0lx\n", pa.raw);
  printf("Value is %d \n", *((uint64_t*)pa.raw));
}

void testGetPTE(){
  int my_var = 666;
  lin_addr myVarLA;
  myVarLA.raw = (uint64_t) &my_var;

  pgTabTool ptt;
  simple_pte *sa = ptt.linAddrToPTE(myVarLA);
  printf("PTE is at %p\n", sa);
  // sa->printCommon();
}

void testGetNonExistPTE(){
  lin_addr myVarLA;
  myVarLA.raw = (uint64_t) 0xffffc00000000000 - 1;

  pgTabTool ptt;
  simple_pte *sa = ptt.linAddrToPTE(myVarLA);
  printf("PTE is at %p\n", sa);
  // sa->printCommon();
}

#endif

