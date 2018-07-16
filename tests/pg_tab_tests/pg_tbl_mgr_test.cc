//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>

#include <ebbrt/native/PageAllocator.h>

#include <UmPgTblMgr.h>
#include <Umm.h>

#define printf ebbrt::kprintf_force

using umm::lin_addr;
using umm::simple_pte;
using umm::UmPgTblMgr;
using umm::phys_addr;

void runHelloWorld() {
  printf(YELLOW "%s\n" RESET, __func__);
  // Generated UM Instance from the linked in Elf
  umm::UmManager::Init();
  auto snap = umm::ElfLoader::CreateInstanceFromElf(&_sv_start);
  umm::manager->Load(std::move(snap));
  umm::manager->Start();
}


lin_addr getPhysPg(uint8_t sz){
  printf(YELLOW "%s\n" RESET, __func__);
  // Link up with order. NYI
  kassert(sz == 1);

  auto page = ebbrt::page_allocator->Alloc();
  kbugon(page == Pfn::None());
  auto page_addr = page.ToAddr();
  lin_addr la; la.raw = page_addr;
  return la;
}

void testMapping4K(){
  printf(YELLOW "%s\n" RESET, __func__);

  lin_addr phys           = getPhysPg(_4K__);   // Get physical addr.
  simple_pte *root        = 0x0;                // Not given, create new one.
  lin_addr virt; virt.raw = 0xffffc00000fd7000; // Somewhere in the slot.
  uint8_t rootLvl         = PDPT_LEVEL;         // Lvl the table starts at.
  uint8_t mapLvl          = PT_LEVEL;           // Lvl mapping occurs at.
  uint8_t curLvl          = rootLvl;            // Tmp variable for traversing PT

  printf("Phys is %lx\n", phys.raw);

  // The mapping happens here.
  simple_pte *table_top = UmPgTblMgr::mapIntoPgTbl(root, phys, virt, rootLvl, mapLvl, curLvl);

  phys_addr pa = UmPgTblMgr::getPhysAddrRec(virt, table_top, 3);

  printf(GREEN "Query for backing page: virt %lx -> %lx\n", virt.raw, pa.raw);

  printf("Table top at %p\n" RESET, table_top);
  kassert(phys.raw == pa.raw);
  printf("MAP 4K PAGE TEST PASSED\n" RESET);
}

void testRemapping4k(){
  printf(YELLOW "%s\n" RESET, __func__);
  testMapping4K();
  testMapping4K();
}

void testMultipleMappings(){
  printf(YELLOW "%s\n" RESET, __func__);
  testMapping4K();
}

void copyOnWrite(){
  printf(YELLOW "%s\n" RESET, __func__);
}

void printCounts(std::vector<uint64_t> &counts){
  kassert(counts.size() == 5);
  for (int i=4; i>0; i--){
    printf("counts[%s] = %lu\n", level_names[i], counts[i]);
  }
}

void testCountValidPages(){
  printf(YELLOW "%s\n" RESET, __func__);
  printf(RED "Note, this test is intended to be run in isolation.\n" RESET);
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.

  UmPgTblMgr::countValidPages(counts);
  printf("Valid pages before running target:\n");
  printCounts(counts);

  runHelloWorld();

  std::fill(counts.begin(), counts.end(), 0);

  printf("\nValid pages after running target:\n");
  UmPgTblMgr::countValidPages(counts);
  printCounts(counts);
}

void testCountAccessedPages(){
  printf(YELLOW "%s\n" RESET, __func__);
  printf(RED "Note, this test is intended to be run in isolation.\n" RESET);
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.

  UmPgTblMgr::countAccessedPages(counts);
  printf("Accessed pages before running target:\n");
  printCounts(counts);

  runHelloWorld();

  std::fill(counts.begin(), counts.end(), 0);

  printf("\nAccessed pages after running target:\n");
  UmPgTblMgr::countAccessedPages(counts);
  printCounts(counts);
}

void testCountDirtyPages(){
  printf(YELLOW "%s\n" RESET, __func__);
  printf(RED "Note, this test is intended to be run in isolation.\n" RESET);
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.

  UmPgTblMgr::countDirtyPages(counts);
  printf("Dirty pages before running target:\n");
  printCounts(counts);

  runHelloWorld();

  std::fill(counts.begin(), counts.end(), 0);

  printf("\nDirty pages after running target:\n");
  UmPgTblMgr::countDirtyPages(counts);
  printCounts(counts);
}

void testCountDirtyPagesInSlot(){
  printf(YELLOW "%s\n" RESET, __func__);
  printf(RED "Note, this test is intended to be run in isolation.\n" RESET);

  runHelloWorld();

  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  std::fill(counts.begin(), counts.end(), 0);

  printf("\nDirty pages in Slot after running target:\n");
  UmPgTblMgr::countDirtyPages(counts, UmPgTblMgr::getSlotPDPTRoot(), PDPT_LEVEL);
  printCounts(counts);
}

void testTraverseVaildPages(){
  printf(YELLOW "%s\n" RESET, __func__);
  UmPgTblMgr::traverseValidPages();
}

void AppMain() {

  testCountDirtyPagesInSlot();

  while(1);
  printf(CYAN "Bout to start copy of dirty pages\n" RESET);
  simple_pte *copyPT = UmPgTblMgr::walkPgTblCopyDirty(nullptr);
  printf(CYAN "Done copy of dirty pages\n" RESET);

  printf("Copy of dirty pages at %p\n", copyPT);

  // printf("result is %d \n", foo(result));
}
