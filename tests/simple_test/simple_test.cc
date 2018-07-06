//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>

#include <Umm.h>

void AppMain() {

  // Initialize the UmManager
  umm::UmManager::Init();
  
  // Generated UM Instance from the linked in Elf 
  kprintf("_sv_start = %p\n", &_sv_start);
  auto snap = umm::ElfLoader::CreateInstanceFromElf(&_sv_start);
  umm::manager->Load(std::move(snap));
  umm::manager->Start();
	ebbrt::acpi::PowerOff();

}
