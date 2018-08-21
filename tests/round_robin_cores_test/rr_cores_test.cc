//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// NOTE: Select only one.

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>

#include <Umm.h>

// Debugging
#include "../../src/UmSV.h"
#include "../../src/UmRegion.h"

// NOTE: this fails at rump baremetal boostrap time.
void loadFromElfTest(int numRuns){

  for(int i = 0; i < numRuns; i++){
    // Generate sv from the linked in Elf every round.
    auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

    // Create the Um Instance and set boot configuration
    auto umi = std::make_unique<umm::UmInstance>(sv);

    // Configure solo5 boot arguments
    uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
    umi->SetArguments(argc);

    umm::manager->Load(std::move(umi));
    umm::manager->runSV();
    umm::manager->Unload();
  }

}

void loadFromSVTest(int numRuns){
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

  for(int i = 0; i < numRuns; i++){
    // Create the Um Instance and set boot configuration
    auto umi = std::make_unique<umm::UmInstance>(sv);

    // Configure solo5 boot arguments
    uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
    umi->SetArguments(argc);

    umm::manager->Load(std::move(umi));
    umm::manager->runSV();
    umm::manager->Unload();

  }
}

void loadFromSnapTest(int numRuns){
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

  // Create the Um Instance and set boot configuration
  kprintf(BLUE "Getting umi sv\n" RESET);
  auto umi = std::make_unique<umm::UmInstance>(sv);

  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
  umi->SetArguments(argc);

  // Get snap future.
  auto snap_f = umm::manager->SetCheckpoint(
                                            umm::ElfLoader::GetSymbolAddress("rumprun_main1"));
                                            // umm::ElfLoader::GetSymbolAddress("solo5_app_main"));
                                            // umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  umm::manager->Load(std::move(umi));
  umm::manager->runSV();
  umm::manager->Unload();

  // assuming this doesn't change. add const.
  kprintf("Grabbing snapshot from future\n");
  const umm::UmSV snap = snap_f.Get();

  // NOTE: Starts at 1 unlike others.
  for(int i = 1; i < numRuns; i++){
    printf(RED "\n\n***** Deploying snapshot %d\n" RESET, i);

    kprintf("Use snap to create instance, this does the page table copy. \n");
    auto umi2 = std::make_unique<umm::UmInstance>(snap);
    // int db = 1; while(db);
    umm::manager->Load(std::move(umi2));
    umm::manager->runSV();
    umm::manager->Unload();
  }
}

void multicoreReload(int numRounds, int numRepeats){
  size_t my_cpu = ebbrt::Cpu::GetMine();
  size_t num_cpus = ebbrt::Cpu::Count();

  // for (auto i = my_cpu; i < num_cpus * numRounds; i++) {
  for (auto i = my_cpu + 1; i < my_cpu + 2; i++) {
    ebbrt::event_manager->SpawnRemote(
        [i, numRepeats]() {
          // size_t my_cpu = ebbrt::Cpu::GetMine();
          // Stagger running to keep output readable.
          int sleep = (1ULL << 34) * i; while (sleep--) ;

          // kprintf("runSV instance on core #%d\n", my_cpu);
          loadFromSnapTest(numRepeats);
        },
        i % num_cpus);
  }
}

void AppMain() {
  // Initialize the UmManager
  umm::UmManager::Init();


  multicoreReload(1, 9);

  kprintf(RED "Done AppMain()\n" RESET);
}
