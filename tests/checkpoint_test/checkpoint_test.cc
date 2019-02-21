//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>
#include <ebbrt/native/Cpu.h>

#include <Umm.h>
#include <chrono> 
#include <iostream> 

using namespace std; 
using namespace std::chrono; 

  // Generated UM Instance from the linked in Elf
std::unique_ptr<umm::UmInstance> initInstance(){
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

  // Create instance.
  auto umi = std::make_unique<umm::UmInstance>(sv);
  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
  umi->SetArguments(argc);

  return std::move(umi);
}

#if 0
void twoCoreTest(){
  // Create instance.
  auto umi = initInstance();

  // Generated UM Instance from the linked in Elf

  ebbrt::Future<umm::UmSV *> snap_f = umi->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("solo5_app_main"));

  // NOTE: Using kickoff here, start on other core.
  umm::manager->Run();

  snap_f.Then([](ebbrt::Future<umm::UmSV*> snap_f) {
      umm::UmSV* snap = snap_f.Get();
      // Deploy this snapshot on next core
      ebbrt::event_manager->SpawnRemote(
          [snap]() {
            auto umi = std::make_unique<umm::UmInstance>(*snap);
            umm::manager->Run(std::move(umi));
          },
          ebbrt::Cpu::GetMine() + 1);
  }); // End snap_f.Then(...)
  ebbrt::kprintf_force("App... Returned from initial execution\n");
}
#endif

void singleCoreTest(){
  // Create instance.
  auto umi = initInstance();
  // Get snap future.
  ebbrt::Future<umm::UmSV *> snap_f = umi->SetCheckpoint(
               // umm::ElfLoader::GetSymbolAddress("solo5_app_main"));
               // umm::ElfLoader::GetSymbolAddress("rumprun_test"));
  umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  umi = std::move(umm::manager->Run(std::move(umi)));
  // umm::manager->ctr.dump_list(umm::manager->ctr_list);

  umm::UmSV* snap = snap_f.Get();

  auto start_umi_create = high_resolution_clock::now();
  auto umi2 = std::make_unique<umm::UmInstance>(*snap);

  auto end_umi_create = high_resolution_clock::now();

  // ebbrt::kprintf_force("ctr_list has %d entries \n", umm::manager->ctr_list.size());
  umm::manager->ctr_list.clear();


  auto start_run = high_resolution_clock::now();
  umi2 = std::move(umm::manager->Run(std::move(umi2)));
  auto end_run = high_resolution_clock::now();

  umi2->pfc.dump_ctrs();
  umm::manager->ctr.dump_list(umm::manager->ctr_list);

  auto umi_create_duration = duration_cast<microseconds>(end_umi_create - start_umi_create);
  cout << "Umi_Create duration: " << umi_create_duration.count() << " microseconds" << endl;

  auto run_duration = duration_cast<microseconds>(end_run - start_run);
  cout << "Run duration: " << run_duration.count() << " microseconds" << endl;

  cout << "Snapshot is this many pages: " << umm::manager->num_cp_pgs << endl;

}

void AppMain() {

  // Initialize the UmManager
  umm::UmManager::Init();
  singleCoreTest();
  // twoCoreTest();
  cout << "powering off: " << endl;
  ebbrt::acpi::PowerOff();
}
