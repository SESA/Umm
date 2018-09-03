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
    ebbrt::kprintf_force("Kicking off\n");
    umm::manager->runSV();
    umm::manager->Unload();

  }
}

const umm::UmSV getSnap(){
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

  // Create the Um Instance and set boot configuration
  ebbrt::kprintf_force(BLUE "Getting umi sv\n" RESET);
  auto umi = std::make_unique<umm::UmInstance>(sv);

  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
  umi->SetArguments(argc);

  // Get snap future.
  auto snap_f = umm::manager->SetCheckpoint(
                                            // umm::ElfLoader::GetSymbolAddress("rumprun_main1"));
                                            // umm::ElfLoader::GetSymbolAddress("solo5_app_main"));
                                            umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  umm::manager->Load(std::move(umi));
  umm::manager->runSV();
  umm::manager->Unload();

  // assuming this doesn't change. add const.
  ebbrt::kprintf_force("Grabbing snapshot from future\n");
  return snap_f.Get();
}

void loadFromSnapTest(int numRuns){
  auto snap = getSnap();

  // NOTE: Starts at 1 unlike others.
  for(int i = 1; i < numRuns; i++){
    printf(RED "\n\n***** Deploying snapshot %d\n" RESET, i);

    auto umi2 = std::make_unique<umm::UmInstance>(snap);
    umm::manager->Load(std::move(umi2));
    umm::manager->runSV();
    umm::manager->Unload();
  }
}

void AppMain() {
  // Initialize the UmManager
  umm::UmManager::Init();
  // loadFromElfTest(1);
  // loadFromSVTest(2);
  loadFromSnapTest(1<<12);

}
