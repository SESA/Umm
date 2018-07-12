//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>
#include <ebbrt/native/Cpu.h>

#include <Umm.h>

void AppMain() {

  // Initialize the UmManager
  umm::UmManager::Init();

  // Generated UM Instance from the linked in Elf 
  auto uni = umm::ElfLoader::CreateInstanceFromElf(&_sv_start);
  umm::manager->Load(std::move(uni));

  ebbrt::Future<umm::UmState> snap_f = umm::manager->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("solo5_app_main"));

  umm::manager->Start();

  snap_f.Then([](ebbrt::Future<umm::UmState> snap_f) {
    auto snap = snap_f.Block().Get();
    // Deploy this snapshot on next core
    size_t core = ebbrt::Cpu::GetMine() + 1;
    ebbrt::event_manager->SpawnRemote(
        [snap] {
          auto umi = std::make_unique<umm::UmInstance>(snap);
          umm::manager->Load(std::move(umi));
          umm::manager->Start();
        },
        core);
  }); // End snap_f.Then(...)
  kprintf("App... Returned from initial execution\n");
}
