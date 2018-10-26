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

ebbrt::Future<umm::UmSV*> snap_f[3];

umm::UmSV* getSnap(){
  // Create the Um Instance and set boot configuration
  ebbrt::kprintf_force(CYAN "Getting umi sv\n" RESET);
  auto umi = std::make_unique<umm::UmInstance>(umm::ElfLoader::createSVFromElf(&_sv_start));
  ebbrt::kprintf_force(CYAN "Got umi sv\n" RESET);

  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(umi->sv_.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);

  umi->SetArguments(argc);

  // Get snap future.
  snap_f[ebbrt::Cpu::GetMine()] = umi->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("solo5_app_main"));

  umm::manager->Run(std::move(umi));

  // assuming this doesn't change. add const.
  return snap_f[ebbrt::Cpu::GetMine()].Get();
}


umm::UmSV *snap_arr[3];
void doWork(){
  auto umi2 = std::make_unique<umm::UmInstance>(*snap_arr[ebbrt::Cpu::GetMine()]);
  umm::manager->Run(std::move(umi2));
}

void rrCores(int numRounds){
    size_t my_cpu = ebbrt::Cpu::GetMine();
    size_t num_cpus = ebbrt::Cpu::Count();

    for (size_t i = my_cpu; i < num_cpus; i++) {
      snap_arr[i] = getSnap();

      int sleep = (1ULL << 34) * i; while (sleep--) ;
    }

    for (int i = my_cpu; i < numRounds; i++) {
      ebbrt::event_manager->SpawnRemote(
          // Get snap by ref, we'll copy in the instance constructor.
          [i]() {
            ebbrt::kprintf_force(RED "\nRun SV instance %d, on core %d\n", i,
                                 (size_t)ebbrt::Cpu::GetMine());
            // Run an instance.
            doWork();
            int sleep = (1ULL << 34) * i; while (sleep--) ;

          },
          i % num_cpus);
    }
}

void AppMain() {
  // Initialize the UmManager
  umm::UmManager::Init();

  rrCores(1<<12);

  ebbrt::kprintf_force(RED "Done AppMain()\n" RESET);
  ebbrt::acpi::PowerOff();
}
