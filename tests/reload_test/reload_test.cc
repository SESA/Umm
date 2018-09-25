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

const std::string my_cmd = R"({"cmdline":"bin/node-default /nodejsActionBase/tu_app.js"})";

const umm::UmSV* getSnap(){
  // auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

  // Create the Um Instance and set boot configuration
  // ebbrt::kprintf_force(CYAN "Getting umi sv\n" RESET);
  auto umi = std::make_unique<umm::UmInstance>(umm::ElfLoader::createSVFromElf(&_sv_start));
  // ebbrt::kprintf_force(CYAN "Got umi sv\n" RESET);

  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(umi->sv_.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE, my_cmd);

  umi->SetArguments(argc);

  // Get snap future.
  auto snap_f = umm::manager->SetCheckpoint(
                                            // umm::ElfLoader::GetSymbolAddress("solo5_app_main"));
  // umm::ElfLoader::GetSymbolAddress("rumprun_main1"));
                                            umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  umm::manager->Load(std::move(umi));
  umm::manager->runSV();
  umm::manager->Unload();

  // assuming this doesn't change. add const.
  return snap_f.Get();
}

void loadFromSnapTest(int numRuns){
  auto snap = getSnap();

  for(int i = 0; i < numRuns; i++){
    printf(RED "\n\n***** Deploying snapshot %d\n" RESET, i);

    // Expect deep copy
    auto umi2 = std::make_unique<umm::UmInstance>(*snap);
    umm::manager->Load(std::move(umi2));
    umm::manager->runSV();
    umm::manager->Unload();
  }
}

void AppMain() {
  // Initialize the UmManager
  umm::UmManager::Init();
  loadFromSnapTest(1<<12);

}
