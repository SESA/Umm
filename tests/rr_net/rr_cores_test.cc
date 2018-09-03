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
// #include "../../src/umm-common.h"

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

void multicoreReloadElf(int numRounds, int numRepeats){
  size_t my_cpu = ebbrt::Cpu::GetMine();
  size_t num_cpus = ebbrt::Cpu::Count();

  kassert(my_cpu == 0);
  for (auto i = my_cpu; i < num_cpus * numRounds; i++) {
    ebbrt::kprintf_force(YELLOW "starting core %d\n" RESET, i);

    // Use to prevent concurrency & make output readable.
    ebbrt::Promise<void> p;
    auto f = p.GetFuture();

    ebbrt::event_manager->SpawnRemote(
        [i, numRepeats, &p]() {
          loadFromElfTest(numRepeats);
          p.SetValue();
        },
        i % num_cpus);
    f.Block();
  }
}

void AppMain() {
  // Initialize the UmManager
  umm::UmManager::Init();

  multicoreReloadElf(2, 2);

  ebbrt::kprintf_force(RED "Done AppMain()\n" RESET);
}
