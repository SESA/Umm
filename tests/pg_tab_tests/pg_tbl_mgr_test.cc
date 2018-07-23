//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>

#include <ebbrt/native/PageAllocator.h>

#include <UmPgTblMgr.h>
#include <Umm.h>


using umm::lin_addr;
using umm::simple_pte;
using umm::UmPgTblMgr;


lin_addr getPhysPg(uint8_t sz){
  // Grab an aligned contiguous pg_sz chunk of memory.
  // Link up with order. NYI
  kassert(sz == 1);

  auto page = ebbrt::page_allocator->Alloc();
  kbugon(page == Pfn::None());
  auto page_addr = page.ToAddr();
  lin_addr la; la.raw = page_addr;
  return la;
}

void printCounts(std::vector<uint64_t> &counts){
  // Print count array.
  kassert(counts.size() == 5);
  for (int i=4; i>0; i--){
    printf("counts[%s] = %lu\n", level_names[i], counts[i]);
  }
}

void runHelloWorld() {
  // Run rumprun's helloworld.
  printf(YELLOW "%s\n" RESET, __func__);
  // Generated UM Instance from the linked in Elf
  umm::UmManager::Init();
  auto snap = umm::ElfLoader::CreateInstanceFromElf(&_sv_start);
  umm::manager->Load(std::move(snap));
  umm::manager->Start();
}

void testCountValidPages(){
  // Traverse page table and count leaf PTEs.
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
void testCountValidPagesLamb(){
  // Traverse page table and count leaf PTEs.
  printf(YELLOW "%s\n" RESET, __func__);
  printf(RED "Note, this test is intended to be run in isolation.\n" RESET);
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.

  UmPgTblMgr::countValidPagesLamb(counts, UmPgTblMgr::getPML4Root(), PML4_LEVEL);
  printf("Valid pages before running target:\n");
  printCounts(counts);

  runHelloWorld();

  std::fill(counts.begin(), counts.end(), 0);

  printf("\nValid pages after running target:\n");
  UmPgTblMgr::countValidPagesLamb(counts, UmPgTblMgr::getPML4Root(), PML4_LEVEL);
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

void testCountAccessedPagesLamb(){
  printf(YELLOW "%s\n" RESET, __func__);
  printf(RED "Note, this test is intended to be run in isolation.\n" RESET);
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.

  UmPgTblMgr::countAccessedPagesLamb(counts, UmPgTblMgr::getPML4Root(), PML4_LEVEL);
  printf("Accessed pages before running target:\n");
  printCounts(counts);

  runHelloWorld();

  std::fill(counts.begin(), counts.end(), 0);

  printf("\nAccessed pages after running target:\n");
  UmPgTblMgr::countAccessedPagesLamb(counts, UmPgTblMgr::getPML4Root(), PML4_LEVEL);
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

void testCountDirtyPagesLamb(){
  printf(YELLOW "%s\n" RESET, __func__);
  printf(RED "Note, this test is intended to be run in isolation.\n" RESET);
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.

  UmPgTblMgr::countDirtyPagesLamb(counts, UmPgTblMgr::getPML4Root(), PML4_LEVEL);
  printf("Dirty pages before running target:\n");
  printCounts(counts);

  runHelloWorld();

  std::fill(counts.begin(), counts.end(), 0);

  printf("\nDirty pages after running target:\n");
  UmPgTblMgr::countDirtyPagesLamb(counts, UmPgTblMgr::getPML4Root(), PML4_LEVEL);
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

void map4K(){
  printf(YELLOW "%s\n" RESET, __func__);

  lin_addr phys           = getPhysPg(_4K__);   // Get physical addr.
  simple_pte *root        = 0x0;                // Not given, create new one.
  lin_addr virt; virt.raw = 0xffffc00000fd7000; // Somewhere in the slot.
  uint8_t rootLvl         = PDPT_LEVEL;         // Lvl the table starts at.
  uint8_t mapLvl          = TBL_LEVEL;           // Lvl mapping occurs at.
  uint8_t curLvl          = rootLvl;            // Tmp variable for traversing PT

  printf("Phys is %lx\n", phys.raw);

  // The mapping happens here.
  simple_pte *table_top = UmPgTblMgr::mapIntoPgTbl(root, phys, virt, rootLvl, mapLvl, curLvl);

  lin_addr pa = UmPgTblMgr::getPhysAddrRec(virt, table_top, 3);

  printf(GREEN "Query for backing page: virt %lx -> %lx\n", virt.raw, pa.raw);

  printf("Table top at %p\n" RESET, table_top);
  kassert(phys.raw == pa.raw);
  printf("MAP 4K PAGE TEST PASSED\n" RESET);
}

simple_pte*
testMapping4kHelper(simple_pte *root, lin_addr phys, lin_addr virt){

  printf(YELLOW "This bad boy should get mapped by: ");
  for (int i=4; i>0; i--){
    printf(CYAN "%u ", virt[i]);
  }
  printf(RESET " ->" RED "%p\n" RESET, phys);

  root = UmPgTblMgr::mapIntoPgTbl(root, phys, virt,
                           (unsigned char)PDPT_LEVEL, (unsigned char)TBL_LEVEL, (unsigned char)PDPT_LEVEL);

  printf(YELLOW "Sanity check, dump counts of valid PTEs\n" RESET);
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.

  UmPgTblMgr::countValidPTEs(counts, root, PDPT_LEVEL);
  printCounts(counts);

  printf(YELLOW "Dump all branches of page table\n" RESET);
  UmPgTblMgr::dumpFullTableAddrs(root, PDPT_LEVEL);

  return root;
}

simple_pte*
testMapping4K(){
  // All don't exist.
  // 384, 0, 7, 472,
  lin_addr virt;

  // simple_pte *root = (simple_pte *) getPhysPg(_4K__).raw;
  simple_pte *root = nullptr;

  // Run to allocate pages.
  lin_addr phys = getPhysPg(_4K__);   // Get physical addr.
  virt.raw = 0xffffc00000fd8000;

  root = testMapping4kHelper(root, phys, virt);

  // All do exist.
  // 384, 0, 7, 502,
  phys = getPhysPg(_4K__);
  virt.raw = 0xffffc00000ff6000;
  root = testMapping4kHelper(root, phys, virt);

  // pdpt exists, not dir or pt
  // 384, 0, 8, 0,
  phys = getPhysPg(_4K__);
  virt.raw = 0xffffc00001000000;
  return testMapping4kHelper(root, phys, virt);
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

void testCopyDirtyPages4K(){
  runHelloWorld();

  std::vector<uint64_t> ValidPTECounts(5); // Vec of size 5, zero elements.

  // UmPgTblMgr::countValidPTEs(ValidPTECounts, UmPgTblMgr::getSlotPDPTRoot(), PDPT_LEVEL); //, UmPgTblMgr::getSlotPDPTRoot(), PDPT_LEVEL);
  UmPgTblMgr::countDirtyPages(ValidPTECounts, UmPgTblMgr::getSlotPDPTRoot(), PDPT_LEVEL); //, UmPgTblMgr::getSlotPDPTRoot(), PDPT_LEVEL);
  printf("After HW, # dirty pages in slot \n");
  printCounts(ValidPTECounts);

  printf(CYAN "Bout to start copy of dirty pages\n" RESET);
  simple_pte *copyPT = UmPgTblMgr::walkPgTblCopyDirty(nullptr);
  printf(CYAN "Done copy of dirty pages\n" RESET);

  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  printf("Look at Valid Pages\n");
  UmPgTblMgr::countValidPages(counts, copyPT, PDPT_LEVEL);
  printCounts(counts);
}

simple_pte*
testMapping2MHelper(simple_pte *root, lin_addr phys, lin_addr virt){

  printf(YELLOW "This bad boy should get mapped by: ");
  for (int i=3; i>0; i--){
    printf(CYAN "%u ", virt[i]);
  }
  printf(RESET " ->" RED "%p\n" RESET, phys);

  root = UmPgTblMgr::mapIntoPgTbl(root, phys, virt,
                                  (unsigned char)PDPT_LEVEL, (unsigned char)DIR_LEVEL, (unsigned char)PDPT_LEVEL);

  printf(YELLOW "Sanity check, dump counts of valid PTEs\n" RESET);

  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  UmPgTblMgr::countValidPTEs(counts, root, PDPT_LEVEL);
  printCounts(counts);

  printf(YELLOW "Dump all branches of page table\n" RESET);
  UmPgTblMgr::dumpFullTableAddrs(root, PDPT_LEVEL);

  return root;
}

void testMapping2M(){
  // All don't exist.
  // 384, 0, 7, 472,
  lin_addr phys, virt;

  phys.raw = 0x23a38f000;
  virt.raw = 0xffffc00000fd8000;
  simple_pte *root = nullptr;
  root = testMapping2MHelper(root, phys, virt);

  // All do exist.
  // 384, 0, 7, 502,
  phys.raw = 0x23a391000;
  virt.raw = 0xffffc00000ff6000;
  root = testMapping4kHelper(root, phys, virt);

  // pdpt exists, not dir or pt
  // 384, 0, 8, 0,
  phys.raw = 0x23a39b000;
  virt.raw = 0xffffc00001000000;
  root = testMapping4kHelper(root, phys, virt);
}

void testDumpSystemValidPages(){
  UmPgTblMgr::dumpFullTableAddrs(UmPgTblMgr::getPML4Root(), PML4_LEVEL);
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  UmPgTblMgr::countValidPages(counts);
  printCounts(counts);
}

void testCopyDirtyPages(){
  std::vector<uint64_t> ValidPTECounts(5); // Vec of size 5, zero elements.
  UmPgTblMgr::countDirtyPages(ValidPTECounts, UmPgTblMgr::getPML4Root(), PML4_LEVEL);
  printf("No HW, # dirty pages in slot: \n");
  printCounts(ValidPTECounts);

  printf(CYAN "Bout to start copy of dirty pages\n" RESET);
  simple_pte *copyPT = UmPgTblMgr::walkPgTblCopyDirty(nullptr);
  printf(CYAN "Done copy of dirty pages\n" RESET);

  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  printf("Look at Valid Pages\n");
  UmPgTblMgr::countValidPages(counts, copyPT, PDPT_LEVEL);
  printCounts(counts);
}

void testReclaimAllPages(simple_pte *root){
  printf(YELLOW "%s\n" RESET, __func__);
  UmPgTblMgr::dumpFullTableAddrs(root, PDPT_LEVEL);

  UmPgTblMgr::reclaimAllPages(root, PDPT_LEVEL);
}

void AppMain() {
  // Note looks like Valid pages count works.
  // testCountValidPages();
  testCountValidPagesLamb();

  // Note looks like Accessed pages count works.
  // testCountAccessedPages();
  // testCountAccessedPagesLamb();


  // Note looks like Dirty pages count works.
  // testCountDirtyPages();
  // testCountDirtyPages();

  // testMapping4K();


  // // Allocate a page.

  // simple_pte *root = testMapping4K();
  // // testReclaimAllPages(root);
  // // UmPgTblMgr::dumpFullTableAddrs(root, PDPT_LEVEL);

  // UmPgTblMgr::printTraversalLamb(root, PDPT_LEVEL);

  while(1);

  // std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  // UmPgTblMgr::countValidPages(counts, root, PDPT_LEVEL);
  // printf("Non Lambda\n");
  // printCounts(counts);

  // std::vector<uint64_t> counts2(5); // Vec of size 5, zero elements.
  // UmPgTblMgr::countValidPagesLamb(counts2, root, PDPT_LEVEL);
  // printf("Lambda\n");
  // printCounts(counts2);

  printf(RED "Done %s\n", __func__);
}

  // UmPgTblMgr::traverseValidPages(root, PDPT_LEVEL,
  //                                []() -> void { printf("Rec\n\n");},
  //                                []() -> void { printf("Leaf\n");},
  //                                [&]() -> void { printf("PostLoop, %s\n",
  //                                                      level_names[lvl]);}
  //                                );

