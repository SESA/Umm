//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>

#include <Umm.h>

auto initInstance(){
  // Create sv.
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

  // Create instance.
  auto umi = std::make_unique<umm::UmInstance>(sv);

  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
  umi->SetArguments(argc);

  return umi;
}

void AppMain() {

  // Initialize the UmManager
  umm::UmManager::Init();

  // Create instance.
  auto umi = initInstance();

  umm::manager->Run(std::move(umi));
	ebbrt::acpi::PowerOff();
}
