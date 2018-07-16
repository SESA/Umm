//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Clock.h>
#include <ebbrt/native/PageAllocator.h>
#include <unordered_map>
// #include "../../../EbbRT/src/native/Perf.h"
#include "umm-common.h"

#include "UmPgTblMgr.h"
// #include <Umm.h>
#include <vector>
// #include <inttypes.h>


const unsigned char pgShifts[] = {0, SMALL_PG_SHIFT, MED_PG_SHIFT, LG_PG_SHIFT};

const char* level_names[] = {"NO_LEVEL",
                             "PT_LEVEL",
                             "DIR_LEVEL",
                             "PDPT_LEVEL",
                             "PML4_LEVEL"};

#define printf ebbrt::kprintf_force

// TODO(tommyu): Is this ok usage?
using umm::lin_addr;
using umm::simple_pte;
using umm::UmPgTblMgr;
using umm::phys_addr;

void UmPgTblMgr::countDirtyPagesHelper(std::vector<uint64_t> &counts,
                                       simple_pte *root, uint8_t lvl) {
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i))
      continue;
    if (isLeaf(root + i, lvl)) {
      if (isDirty(root + i)) {
        counts[lvl]++;
      }
    } else {
      countDirtyPagesHelper(counts, nextTableOrFrame(root, i, lvl), lvl - 1);
    }
  }
}

void UmPgTblMgr::countDirtyPages(std::vector<uint64_t> &counts, simple_pte *root, uint8_t lvl) {
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

void UmPgTblMgr::countAccessedPagesHelper(std::vector<uint64_t> &counts,
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

void UmPgTblMgr::countAccessedPages(std::vector<uint64_t> &counts, simple_pte *root, uint8_t lvl) {
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

void UmPgTblMgr::countValidPagesHelper(std::vector<uint64_t> &counts,
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

void UmPgTblMgr::countValidPages(std::vector<uint64_t> &counts, simple_pte *root, uint8_t lvl) {
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

void UmPgTblMgr::traverseValidPagesHelper(simple_pte *root, uint8_t lvl) {
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i))
      continue;
    if (isLeaf(root + i, lvl)) {
    } else {
      traverseValidPagesHelper(nextTableOrFrame(root, i, lvl), lvl - 1);
    }
  }
}

void UmPgTblMgr::traverseValidPages(simple_pte *root, uint8_t lvl){
  // Going to grab PML4, so better be lvl 4.
  if(root == nullptr){
    kassert(lvl == 4);
  }

  if (root == nullptr){
    root = getPML4Root();
  }
  traverseValidPagesHelper(root, lvl);
}

phys_addr UmPgTblMgr::getPhysAddrRecHelper(lin_addr la, simple_pte* root, unsigned char lvl) {
  printf("la is %#0lx, offset is %lu, root is %p, lvl is %u\n", la.raw, la[lvl], root, lvl);

  if(!exists(root + la[lvl])){
    printf("Doesn't exist, our work is done! \n");
    kassert(false);
  }

  if (isLeaf(root + la[lvl], lvl)) {
    phys_addr phys_addr;
    printf("This guy is a leaf! Follow final table inderection & Return the addr \n");
    root = nextTableOrFrame(root, la[lvl], lvl);
    phys_addr.raw = (uint64_t)root;
    switch (lvl) {
    case PDPT__: phys_addr.construct_addr_1G.OFFSET = la.decomp1G.PGOFFSET; break;
    case DIR__: phys_addr.construct_addr_2M.OFFSET = la.decomp2M.PGOFFSET; break;
    case TAB__: phys_addr.construct_addr_4K.OFFSET = la.decomp4K.PGOFFSET;}
    printf("bout to return helper\n");
    return phys_addr;
  }

  // Recurse.
  printf("We exist, but we're not a leaf, recurse!\n");
  return getPhysAddrRecHelper(la, nextTableOrFrame(root, la[lvl], lvl), lvl-1);

}

phys_addr UmPgTblMgr::getPhysAddrRec(lin_addr la, simple_pte* root /*=nullptr*/, unsigned char lvl /*=4*/) {
  // Error checks and hands off to helper.

  // Valid range for 4 level paging.
  kassert(lvl >= TAB__ && lvl <= PML4__);

  if (root == nullptr) {
    // If coming from top, gotta get root from CR3.
    if (lvl == PML4__) {
      root = getPML4Root();
    }
  } else {
    // If have root, better be below pml4.
    kassert(lvl < PML4__);
  }

  // Better have a root to follow.
  kassert(root != nullptr);

  return getPhysAddrRecHelper(la, root, lvl);
}

simple_pte* UmPgTblMgr::getSlotPDPTRoot(){
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
      printf("1 ");
    else
      printf("0 ");
  }
  if (val & (1ULL << len))
    printf("1");
  else
    printf("0");
}

void simple_pte::printCommon() {
  printf("\t|                       |     1G                                    |                                 |P|     | |M| | |P|P|U|R|S|\n");
  printf("\t|       Reserved        |               2M          Address of PTE Table              |               |A| IGN |G|A|D|A|C|W|/|/|E|\n");
  printf("\t|                       |                                  4K                                         |T|     | |P| | |D|T|S|W|L|\n");
  printf("\t|");
  printBits(decompCommon.RES, 12); printf("|");
  printBits(decompCommon.PG_TBL_ADDR, 40); printf("|");
  printBits(decompCommon.WHOCARES2, 4); printf("|");
  printBits(decompCommon.MAPS, 1); printf("|");
  printBits(decompCommon.DIRTY, 1); printf("|");
  printBits(decompCommon.A, 1); printf("|");
  printBits(decompCommon.PCD, 1); printf("|");
  printBits(decompCommon.PWT, 1); printf("|");
  printBits(decompCommon.US, 1); printf("|");
  printBits(decompCommon.RW, 1); printf("|");
  printBits(decompCommon.SEL, 1); printf("|\n");
  underlineNibbles();
  printNibblesHex();
}
lin_addr simple_pte::pageTabEntToAddr(unsigned char lvl) {
  lin_addr la;
  // Refer to figure 4-11 in Vol 3A of the intel man.
  // printf("in page tab ent to addr lvl is %d, maps is %u \n", lvl, decompCommon.MAPS);
  switch (lvl) {
  case PML4__:
    // printf("PML4\n");
    // This PML4E points to a PDPT
    la.raw = (uint64_t)decompCommon.PG_TBL_ADDR << SMALL_PG_SHIFT;
    break;

  case PDPT__:
    // printf("PDPT\n");
    if (decompCommon.MAPS) {
      // This PDPTE maps a 1GB page.
      la.raw = (uint64_t)decomp1G.PAGE_NUMBER << LG_PG_SHIFT;
    } else {
      // This PDPT points to a DIR.
      la.raw = (uint64_t)decompCommon.PG_TBL_ADDR << SMALL_PG_SHIFT;
    }
    break;

  case DIR__:
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

  case TAB__:
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

simple_pte* UmPgTblMgr::nextTableOrFrame(simple_pte* pg_tbl_start, uint64_t pg_tbl_offset, unsigned char lvl) {
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

simple_pte* UmPgTblMgr::getPML4Root(){
  simple_pte cr3;
  asm volatile("mov %%cr3, %[cr3]" : [cr3] "=r"(cr3));
  return (simple_pte *) cr3.cr3ToAddr().raw;
}

bool UmPgTblMgr::exists(simple_pte *pte){
  if(pte->decompCommon.SEL == 1)
    return true;
  return false;
}

bool UmPgTblMgr::isWritable(simple_pte *pte){
  if(pte->decompCommon.RW == 1){
    return true;
  }
  return false;
}

bool UmPgTblMgr::isReadOnly(simple_pte *pte){
  if(pte->decompCommon.RW == 0){
    return true;
  }
  return false;
}

bool UmPgTblMgr::isDirty(simple_pte *pte){
  if(pte->decompCommon.DIRTY){
    return true;
  }
  return false;
}

bool UmPgTblMgr::isAccessed(simple_pte *pte){
  if(pte->decompCommon.A){
    return true;
  }
  return false;
}

bool UmPgTblMgr::isLeaf(simple_pte *pte, unsigned char lvl){
  if(lvl == TAB__){
    return true;
  }

  // No worries about PML4, reserved to 0;
  if(pte->decompCommon.MAPS){
    return true;
  }
  return false;
}

simple_pte * UmPgTblMgr::walkPgTblCopyDirty(simple_pte *root, simple_pte *copy) {
  // Entry 0 is bogus and unused
  uint64_t counts[5] = {0};
  uint64_t idx[5] = {0};

  // simple_pte *root = (simple_pte *) cr3.cr3ToAddr().raw;
  // simple_pte *root = getPML4Root();
  // unsigned char lvl = 4;
  if (root == nullptr)
    root = getSlotPDPTRoot();

  unsigned char lvl = 3;
  // HACK(tommyu): This is actually critical.
  idx[4] = SLOT_PML4_NUM;

  simple_pte *ret = walkPgTblCopyDirtyHelper(counts, root, copy, lvl, idx);

  for (int i=0; i<5; i++){
    printf("counts[%d] = %lu\n", i, counts[i]);
  }
  return ret;
}

lin_addr UmPgTblMgr::reconstructLinAddrPgFromOffsets(uint64_t *idx){
  lin_addr la;
  la.raw = 0;
  la.tblOffsets.PML4 = idx[PML4__];
  la.tblOffsets.PDPT = idx[PDPT__];
  la.tblOffsets.DIR = idx[DIR__];
  la.tblOffsets.TAB = idx[TAB__];

  // HACK(tommyu): Something about cannonical addressing? Need to do some reading.
  // Think this should only be applied if pml4 num >= 256 aka 0x100, half 512, 0x200.
  if (idx[PML4__] >= 0x100)
    la.raw |= 0xffffUL << 48;
  return la;
}

lin_addr UmPgTblMgr::copyDirtyPage(lin_addr src, unsigned char lvl){
  auto page = ebbrt::page_allocator->Alloc();
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

void simple_pte::tableOrFramePtrToPte(simple_pte *tab){
  // TODO(tommyu): generalize to all levels.
  kassert((uint64_t)tab % (1 << 12) == 0);
  // printf("Have addr, returning pte\n");
  // printf("Addr is %p\n", tab);

  simple_pte pte; pte.raw = 0;
  pte.decompCommon.SEL = 1;
  pte.decompCommon.RW = 1;
  pte.decompCommon.US = 1;
  pte.decompCommon.PWT = 0;
  pte.decompCommon.PCD = 0;
  pte.decompCommon.A = 0;

  pte.decompCommon.DIRTY = 0;
  pte.decompCommon.MAPS = 0;
  pte.decompCommon.WHOCARES2 = 0;
  pte.decompCommon.PG_TBL_ADDR = (uint64_t)tab >> 12;
  pte.decompCommon.RES = 0;

  raw = pte.raw;
}

simple_pte *UmPgTblMgr::mapIntoPgTbl(simple_pte *root, lin_addr phys,
                                     lin_addr virt, unsigned char rootLvl,
                                     unsigned char mapLvl,
                                     unsigned char curLvl) {
  kassert(rootLvl >= mapLvl);
  kassert(rootLvl >= curLvl);
  kassert(rootLvl <= PML4_LEVEL && rootLvl >= PT_LEVEL);
  kassert(mapLvl <= PDPT_LEVEL);

  // printf(
  //     YELLOW
  //     "root %lx, phys %lx, virt %lx, rootLvl %u, mapLvl %u, curLvl %u\n" RESET,
  //     root, phys, virt, rootLvl, mapLvl, curLvl);

  if (root == nullptr) {
    // printf("Root is null, must allocate page and zero. \n");
    auto page = ebbrt::page_allocator->Alloc();
    kbugon(page == Pfn::None());
    auto page_addr = page.ToAddr();
    // printf(BLUE "Allocating PT at %p\n" RESET, page_addr);
    memset((void *)page_addr, 0, SMALL_PG_BYTES);
    root = (simple_pte *)page_addr;

    if(curLvl == rootLvl){
      printf("Created and zeroed root at %p\n", page_addr);
    }
  }

  // Get offset using custom indexing operator.
  simple_pte *pte_ptr = root + virt[curLvl];
  if (curLvl == mapLvl) {
    // printf("At the mapping level\n");
    pte_ptr->tableOrFramePtrToPte((simple_pte *)phys.raw);
  } else {
    // printf("Must recurse, offset at lvl %u is %u \n", curLvl, virt[curLvl]);
    if (exists(pte_ptr)) {
      // printf("Next level entry exists, we'll go there.\n");
      mapIntoPgTbl(root, phys, virt, rootLvl, mapLvl, curLvl - 1);
    } else {
      // printf("Next level doesn't exist, we'll get entry from the recursion.\n");
      simple_pte *ret =
          mapIntoPgTbl(nullptr, phys, virt, rootLvl, mapLvl, curLvl - 1);
      // pte_ptr->printCommon();
      pte_ptr->tableOrFramePtrToPte(ret);
      // pte_ptr->printCommon();
    }
  }
  // printf(CYAN "Returning\n" RESET);
  return root;
}

simple_pte *UmPgTblMgr::walkPgTblCopyDirtyHelper(uint64_t *counts,
                                                 simple_pte *root,
                                                 simple_pte *copy,
                                                 unsigned char lvl,
                                                 uint64_t *idx) {
  // HACK(tommyu): This is a dirty hack to reconstruct the linear addr that got you to this dirty page. I think we can do better.
  // Scan all 512 entries, if predicate holds, recurse.
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i)) continue;
    idx[lvl] = i;

    // Count if at leaf.
    if(isLeaf(root + i, lvl)){
      if((root + i)->decompCommon.DIRTY){
        // printf("Found dirty page. Pte at %p\n", root + i);
        // for (int i = 4; i>0; i--){printf("lvl[%d] =  %ld\n", i, idx[i]);}

        // (root + i)->printCommon();
        lin_addr backing;
        backing.raw = (root + i)->pageTabEntToAddr(lvl).raw;

        // printf("Phys addr is %p\n", backing.raw);
        // printf("First 64bits %lx\n", *(uint64_t*)backing.raw);

        // printf("\nReconstruct page Lin. Addr.\n");
        lin_addr virt = reconstructLinAddrPgFromOffsets(idx);

        // printf("Lin addr is %p\n", virt.raw);
        // printf("First 64bits %lx\n", *(uint64_t *)(virt.raw));

        // printf("\nCopy physical page\n");
        lin_addr phys = copyDirtyPage(backing, lvl);
        // printf("New Phys addr is %p\n", phys.raw);
        // printf("First 64bits %lx\n", *(uint64_t*)phys.raw);

        // printf(RED "mapIntoPgTbl(%p, %p, %p, %u, %u, %u)\n" RESET, copy, phys.raw, virt.raw, PDPT_LEVEL, lvl, PDPT_LEVEL);
        copy = mapIntoPgTbl(copy, phys, virt, PDPT_LEVEL, lvl, PDPT_LEVEL);
        // printf("New page table at %p", copy);

        // printf("Phys from new table %p \n", getPhysAddrRec(virt, copy, 3).raw);

        counts[lvl]++;
      }
    } else {
      // Don't alter your own root, or you will break on the next goaround.
      copy = walkPgTblCopyDirtyHelper(counts, nextTableOrFrame(root, i, lvl), copy, lvl - 1, idx);
    }
  }
  return copy;
}

// void UmPgTblMgr::Init() {
//   printf(YELLOW "Initializing UmPgTblMgr\n" RESET);
// }
#if 0
// simple_pte* nextTableOrFrame(simple_pte* pg_tbl_start, uint64_t pg_tbl_offset, unsigned char lvl) {
//   // Takes ptr to start of page table and offset, returns ptr to next table.
//   simple_pte *pte_ptr;

//   // printf(YELLOW "%s Table starts at %p, offset is 0x%llx \n" RESET, level_names[lvl],  pg_tbl_start, pg_tbl_offset);

//   // Add the offset to get ptr to relevant PTE in this table.
//   pte_ptr = pg_tbl_start + pg_tbl_offset;
//   // printf(YELLOW "PTE is at %p, it's value is %p\n" RESET, pte_ptr, pte_ptr->raw);

//   // Deref gives the relevant PTE of the current table.
//   simple_pte pte = *pte_ptr;

//   // Guarantee it actually exists.
//   kassert(pte.decompCommon.SEL == 1);

//   // Convert page number of PTE to an address. This is a pointer to the next level page table.
//   simple_pte *ret = (simple_pte *)pte.pageTabEntToAddr(lvl).raw;
//   // printf("lvl is %d Next table starts at %p\n" RESET, lvl, ret);

//   return ret;
// }


void pgTabTool::countAllocatedPagesHelperImp(uint64_t* counts, simple_pte* root,
                                                 unsigned char lvl) {
  // This isn't fully functional, doesn't look for leaves etc.
  simple_pte *sp_arr[5] = {0};

  // Scan all 512 entries, if predicate holds, recurse.
  // printf("Scanning pml4\n");
  for (int i = 0; i < 512; i++) {
    // Only deal with the slot.
    if (!exists(root + i)) continue;
    if (lvl == PML4__ && i != 384) continue;
    counts[4] += 1;

    // printf("Entry %x exists\n", i);
    // root->printCommon();

    sp_arr[4] = nextTableOrFrame(root, i, 4);

    // printf("Scanning pdpt\n");
    for(int j = 0; j<512; j++){
      // printf("checking %p\n", root + j);
      if (!exists(sp_arr[4] + j)) continue;
      counts[3] += 1;
      // printf("Entry %x exists\n", j);
      // root->printCommon();

      sp_arr[3] = nextTableOrFrame(sp_arr[4], j, 3);


      // printf("Scanning dir\n");
      for(int k=0; k<512; k++){
        if (!exists(sp_arr[3] + k)) continue;
        // printf("Entry (%d,%d,%d) exists\n",i,j,k);
        counts[2] += 1;
        // printf("Entry %x exists\n", k);
        // root->printCommon();

        sp_arr[2] = nextTableOrFrame(sp_arr[3], k, 2);

        // printf("Scanning tab\n");
        for(int m=0; m<512; m++){
          if (!exists(sp_arr[2] + m)) continue;
          // if (!(sp_arr[2] + m)->decompCommon.DIRTY) continue;
            counts[1] += 1;
          // printf("Entry (%d,%d,%d,%d) exists\n",i,j,k,m);
          //   root->printCommon();
          // root = nextTableOrFrame(root, m, 2);
        }
      }
    }
    // while(1);
  }
}

// std::vector<int> dbVec;
// void printdbVec(){
//   kassert(dbVec.size() <= 4);
//   printf("( ");
//   for(const auto &e : dbVec)
//     printf("%d, ", e);
//   printf(" ) ");
// }

// bool pgTabTool::isLeaf(simple_pte *pte, unsigned char lvl){
//   if(lvl == TAB__)
//     return true;

//   // No worries about PML4, reserved to 0;
//   if(pte->decompCommon.MAPS)
//     return true;
//   return false;
// }

void pgTabTool::countAllocatedPagesHelper(uint64_t *counts, simple_pte *root,
                                          unsigned char lvl) {
  // Scan all 512 entries, if predicate holds, recurse.
  for (int i = 0; i < 512; i++) {
    if (!exists(root + i)) continue;

    // Count if at leaf.
    if(isLeaf(root + i, lvl)){
      // if((root + i)->decompCommon.DIRTY)
        counts[lvl]++;
    } else {
    // Don't alter your own root, or you will break on the next goaround.
      countAllocatedPagesHelper(counts, nextTableOrFrame(root, i, lvl), lvl - 1);
    }
  }
}

void pgTabTool::countAllocatedPages(){
  // Entry 0 is bogus and unused
  uint64_t counts[5] = {0};

  // simple_pte *root = (simple_pte *) cr3.cr3ToAddr().raw;
  simple_pte *root = getPML4Root();

  unsigned char lvl = 4;
  countAllocatedPagesHelper(counts, root, lvl);

  for (int i=0; i<5; i++){
    printf("counts[%d] = %lu\n", i, counts[i]);
  }
}

void pgTabTool::countAllocatedPagesImp(){
  // Entry 0 is bogus and unused
  uint64_t counts[5] = {0};

  // simple_pte cr3; cr3.raw = 0;
  // asm volatile("mov %%cr3, %[cr3]" : [cr3] "=r"(cr3.raw));

  // simple_pte *root = (simple_pte *) cr3.cr3ToAddr().raw;
  simple_pte *root = getSlotPDPTRoot();

  unsigned char lvl = 4;
  countAllocatedPagesHelperImp(counts, root, lvl);
  for(int n=0; n<5; n++){
    printf("counts[%d]=%lu\n", n, counts[n]);
  }
}


// simple_pte* pgTabTool::getPML4Root(){
//   simple_pte cr3;
//   asm volatile("mov %%cr3, %[cr3]" : [cr3] "=r"(cr3));
//   return (simple_pte *) cr3.cr3ToAddr().raw;
// }

// simple_pte *pgTabTool::getPDPTRoot(){
//   // Root of slot.
//   return nextTableOrFrame(getPML4Root(), SLOT_PML4_NUM, 4);
// }


// lin_addr simple_pte::cr3ToAddr(){
//   // Not easy to integrate into pageTabEntToAddr.
//   lin_addr la;
//   la.raw = (uint64_t)decompCommon.PG_TBL_ADDR << SMALL_PG_SHIFT;

//   return la;
// }
// bool pgTabTool::exists(simple_pte *pte){
//   if(pte->decompCommon.SEL == 1)
//     return true;
//   return false;
// }



// void count() {
//   std::unordered_map<std::string, ebbrt::perf::PerfCounter> counters;

//   counters.emplace(std::make_pair(std::string("instructions"),
//         std::move(ebbrt::perf::PerfCounter{ebbrt::perf::PerfEvent::fixed_instructions})));

//   printf("\n Nil counter:\n");
//   for( auto& i : counters ){
//     i.second.Clear();
//     i.second.Start();
//     // Your timed code here.
//     pgTabTool ptt;
//     // ptt.countAllocatedPagesImp();
//     ptt.countAllocatedPages();

//     // End your timed code;
//     i.second.Stop();
//     printf("%s\t\t%lu\n", i.first.c_str(), i.second.Read());
//     printf("done\n");
//     // This GEFaults for some reason.
//     while(1);
//   }
// }

// NOTE(tommyu): These were abandoned.
// simple_pte* pgTabTool::linAddrToPTEGetOrCreateHelper(lin_addr la, uint64_t *offsets, simple_pte* root, unsigned char lvl) {
//   printf("la is %#0lx, offset is %lu, root is %p, lvl is %u\n", la.raw, offsets[lvl], root, lvl);

//   if(!exists(root + offsets[lvl])){
//     printf("Doesn't exist, allocate necessary page! \n");
//     // auto page =  page_allocator->Alloc();
//     // kassert(page != Pfn::None());
//     // auto page_addr = page.ToAddr();

//     // Zero new page
//     // memset(page_addr, 0, BYTES_4K);


//   }

//   if (isLeaf(root + offsets[lvl], lvl)) {
//     printf("This guy is a leaf! Return the addr \n");
//     return root + offsets[lvl];
//   }

//   // Recurse.
//   printf("We exist, but we're not a leaf, recurse!\n");
//   return linAddrToPTEHelper(la, offsets, nextTableOrFrame(root, offsets[lvl], lvl), lvl-1);
// }

// simple_pte* pgTabTool::linAddrToPTEGetOrCreate(lin_addr la, simple_pte* root /*=nullptr*/, unsigned char lvl /*=4*/) {
//   // Error checks and hands off to helper.
//   // Valid range for 4 level paging.
//   kassert(lvl >= TAB__ && lvl <= PML4__);

//   if (root == nullptr) {
//     // If coming from top, gotta get root from CR3.
//     if (lvl == PML4__) {
//       root = getPML4Root();
//     }
//   } else {
//     // If have root, better be below pml4.
//     kassert(lvl < PML4__);
//   }

//   // Better have a root to follow.
//   kassert(root != nullptr);

//   uint64_t offsets[5];
//   offsets[4] = la.tblOffsets.PML4;
//   offsets[3] = la.tblOffsets.PDPT;
//   offsets[2] = la.tblOffsets.DIR;
//   offsets[1] = la.tblOffsets.TAB;

//   return linAddrToPTEGetOrCreateHelper(la, offsets, root, lvl);
// }

// This is super close to getting pte don't add offset.

simple_pte* pgTabTool::linAddrToPTEHelper(lin_addr la, uint64_t *offsets, simple_pte* root, unsigned char lvl) {
  printf("la is %#0lx, offset is %lu, root is %p, lvl is %u\n", la.raw, offsets[lvl], root, lvl);

  if(!exists(root + offsets[lvl])){
    printf("Doesn't exist, our work is done! \n");
    kassert(false);
  }

  if (isLeaf(root + offsets[lvl], lvl)) {
    printf("This guy is a leaf! Return the addr \n");
    return root + offsets[lvl];
  }

  // Recurse.
  printf("We exist, but we're not a leaf, recurse!\n");
  return linAddrToPTEHelper(la, offsets, nextTableOrFrame(root, offsets[lvl], lvl), lvl-1);
}

simple_pte* pgTabTool::linAddrToPTE(lin_addr la, simple_pte* root /*=nullptr*/, unsigned char lvl /*=4*/) {
  // Error checks and hands off to helper.
  // Valid range for 4 level paging.
  kassert(lvl >= TAB__ && lvl <= PML4__);

  if (root == nullptr) {
    // If coming from top, gotta get root from CR3.
    if (lvl == PML4__) {
      root = getPML4Root();
    }
  } else {
    // If have root, better be below pml4.
    kassert(lvl < PML4__);
  }

  // Better have a root to follow.
  kassert(root != nullptr);

  uint64_t offsets[5];
  offsets[4] = la.tblOffsets.PML4;
  offsets[3] = la.tblOffsets.PDPT;
  offsets[2] = la.tblOffsets.DIR;
  offsets[1] = la.tblOffsets.TAB;

  return linAddrToPTEHelper(la, offsets, root, lvl);
}


// void test_walk_pg_table_rec(lin_addr myVarLA) {

//   printf("Linear address is %lu\n", myVarLA.raw);
//   phys_addr phys_addr = myVarLA.getPhysAddrRec();

//   printf("Physical address is %p \n", phys_addr.raw);
//   printf("Value is %d \n", *((uint64_t*)phys_addr.raw));
// }

// void testPrintAllPgCts(){
//   pgTabTool ptt;
//   ptt.countAllocatedPages();
// }

void testPrintSlotPgCts(){
  pgTabTool ptt;

  uint64_t ctr[5] = {0};
  ptt.countAllocatedPagesHelper(ctr, ptt.getSlotPDPTRoot(), 3);
  ptt.countAllocatedPages();
  for (int i=0; i<5; i++){
    printf("counts[%d] = %lu\n", i, ctr[i]);
  }
}

void testResolveGoodAddr(){
  int my_var = 666;
  lin_addr myVarLA;
  myVarLA.raw = (uint64_t) &my_var;

  pgTabTool ptt;
  phys_addr pa = ptt.getPhysAddrRec(myVarLA);
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

// void simpleTest(){

//   // Initialize the UmManager
//   umm::UmManager::Init();

//   // Generated UM Instance from the linked in Elf
//   printf("_sv_start = %p\n", &_sv_start);
//   auto snap = umm::ElfLoader::CreateInstanceFromElf(&_sv_start);
//   umm::manager->Load(std::move(snap));
//   umm::manager->Start();
//   printf("done\n");

// }

// void testGetOrCreatePTE(){
//   lin_addr myVarLA;
//   myVarLA.raw = (uint64_t) 0xffffc00000000000 - 1;

//   pgTabTool ptt;
//   simple_pte *sa = ptt.linAddrToPTEGetOrCreate(myVarLA);
//   printf("PTE is at %p\n", sa);
//   // sa->printCommon();

// }

// void AppMain() {
// void runMe(){
  // testPrintAllPgCts();
  // testPrintSlotPgCts();

  // printf(GREEN "walk\n");
  // testResolveGoodAddr();
  // testGetPTE();

  // printf(RESET "Try to get PTE that doesn't exist\n");
  // testGetNonExistPTE();
  // testGetOrCreatePTE();

// }
#endif

