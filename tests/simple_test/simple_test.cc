//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>

#include <Umm.h>
#include <chrono> 
#include <iostream> 

using namespace std; 
using namespace std::chrono; 

auto initInstance(){
  auto start = high_resolution_clock::now();

  // Create sv.
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);
  auto create_sv = high_resolution_clock::now();

  // Create instance.
  auto umi = std::make_unique<umm::UmInstance>(sv);
  auto create_umi = high_resolution_clock::now();

  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
  umi->SetArguments(argc);
  auto config_args = high_resolution_clock::now();

  auto sv_create_duration = duration_cast<microseconds>(create_sv - start);
  cout << "SV create duration: " << sv_create_duration.count() << " microseconds" << endl;

  auto umi_create_duration = duration_cast<microseconds>(create_umi - create_sv);
  cout << "UMI create duration: " << umi_create_duration.count() << " microseconds" << endl;

  auto args_create_duration = duration_cast<microseconds>(config_args - create_umi);
  cout << "Arg pass duration: " << args_create_duration.count() << " microseconds" << endl;

  return umi;
}

void AppMain() {

  // Initialize the UmManager
  umm::UmManager::Init();

  // Create instance.
  auto umi = initInstance();

  auto start = high_resolution_clock::now();

  umi = std::move(umm::manager->Run(std::move(umi)));

  auto end = high_resolution_clock::now();

  auto run_duration = duration_cast<microseconds>(end - start);

  umi->pfc.dump_ctrs();
  // (*umi).~UmInstance();
  umm::manager->ctr.dump_list(umm::manager->ctr_list);
  cout << "Run duration: " << run_duration.count() << " microseconds" << endl;
  cout << "powering off: " << endl;
	ebbrt::acpi::PowerOff();
}
