//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>
#include <ebbrt/native/Cpu.h>

#include <Umm.h>

std::unique_ptr<umm::UmInstance> initInstance(){
  // Create sv.
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

  // Create instance.
  auto umi = std::make_unique<umm::UmInstance>(sv);

  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
  umi->SetArguments(argc);

  return std::move(umi);
}

void twoCoreTest(){
  // Create instance.
  auto umi = initInstance();

  // Generated UM Instance from the linked in Elf
  umm::manager->Load(std::move(umi));

  ebbrt::Future<umm::UmSV> snap_f = umm::manager->SetCheckpoint(
                                                                umm::ElfLoader::GetSymbolAddress("uv_uptime"));
  // umm::ElfLoader::GetSymbolAddress("rumprun_main1"));
  // umm::ElfLoader::GetSymbolAddress("solo5_app_main"));

  // NOTE: Using kickoff here, start on other core.
  umm::manager->runSV();

  snap_f.Then([](ebbrt::Future<umm::UmSV> snap_f) {
      umm::UmSV snap = snap_f.Block().Get();
      // Deploy this snapshot on next core
      ebbrt::event_manager->SpawnRemote(
          [snap]() {
            auto umi = std::make_unique<umm::UmInstance>(snap);
            umm::manager->Load(std::move(umi));
            umm::manager->runSV();
          },
          ebbrt::Cpu::GetMine() + 1);
  }); // End snap_f.Then(...)
  kprintf("App... Returned from initial execution\n");

}

void singleCoreTest(){
  // Create instance.
  auto umi = initInstance();

  // Get snap future.
  auto snap_f = umm::manager->SetCheckpoint(
                                            umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Generated UM Instance from the linked in Elf
  umm::manager->Load(std::move(umi));
  // NOTE: Using kickoff here, start on other core.
  umm::manager->runSV();
  umm::manager->Unload();

  umm::UmSV snap = snap_f.Get();
  auto umi2 = std::make_unique<umm::UmInstance>(snap);
  umm::manager->Load(std::move(umi2));
  umm::manager->runSV();
  umm::manager->Unload();
}

void AppMain() {

  // Initialize the UmManager
  umm::UmManager::Init();
  twoCoreTest();
  // singleCoreTest();

  // ebbrt::acpi::PowerOff();

}
