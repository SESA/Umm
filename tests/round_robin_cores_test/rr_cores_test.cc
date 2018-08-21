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

// This isn't what I'm after, but may be useful later.
// void multicoreReload(int numRounds, int numRepeats){
//   size_t my_cpu = ebbrt::Cpu::GetMine();
//   size_t num_cpus = ebbrt::Cpu::Count();

//   for (auto i = my_cpu; i < num_cpus * numRounds; i++) {
//   // for (auto i = my_cpu + 1; i < my_cpu + numRounds; i++) {
//     ebbrt::event_manager->SpawnRemote(
//         [i, numRepeats]() {
//           // Stagger running to keep output readable.
//           int sleep = (1ULL << 34) * i; while (sleep--) ;

//           // kprintf("runSV instance on core #%d\n", my_cpu);
//           loadFromSnapTest(numRepeats);
//         },
//         i % num_cpus);
//   }
// }

const umm::UmSV getSnap(){
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

  // Create the Um Instance and set boot configuration
  kprintf(BLUE "Getting umi sv\n" RESET);
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
  kprintf("Grabbing snapshot from future\n");
  return snap_f.Get();
}

void reloadSingleCore(int numRuns) {
  // Idea, take one snap, deploy on many cores.
  // assuming this doesn't change. add const.
  kprintf("Grabbing snapshot from future\n");

  auto snap = getSnap();

  // NOTE: Starts at 1 unlike others.
  for(int i = 1; i < numRuns; i++){
    size_t my_cpu = ebbrt::Cpu::GetMine();
    printf(RED "\n\n***** Deploying snapshot %d on core %d\n" RESET, i, my_cpu);

    auto umi2 = std::make_unique<umm::UmInstance>(snap);
    umm::manager->Load(std::move(umi2));
    umm::manager->runSV();
    umm::manager->Unload();
  }
}

void doWork(const umm::UmSV &snap){
  auto umi2 = std::make_unique<umm::UmInstance>(snap);
  umm::manager->Load(std::move(umi2));
  umm::manager->runSV();
  umm::manager->Unload();
}

void rrCores(int numRounds, int numRuns){

    size_t my_cpu = ebbrt::Cpu::GetMine();
    size_t num_cpus = ebbrt::Cpu::Count();

    // Get that sweet snapshot.
    auto snap = getSnap();
    for (size_t i = my_cpu; i < num_cpus * numRounds; i++) {
      ebbrt::event_manager->SpawnRemote(
          // Get snap by ref, we'll copy in the instance constructor.
          [i, snap, numRuns]() {
            // Stagger running to keep output readable.
            uint64_t sleep = (1ULL << 30) * i;
            while (sleep--)
              ;

            for (int j = 0; j < numRuns; j++) {
              kprintf(RED "\nRun SV instance %d, run %d, on core %d\n", i, j,
                      (size_t)ebbrt::Cpu::GetMine());
              // Run an instance.
              doWork(snap);
            }
          },
          i % num_cpus);
    }
}

void AppMain() {
  // Initialize the UmManager
  umm::UmManager::Init();

  rrCores(3, 4);

  kprintf(RED "Done AppMain()\n" RESET);
}
