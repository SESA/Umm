//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Debug.h>

#include <Umm.h>
#include <UmProxy.h>

umm::UmProxy::TcpSession* app_session;

void AppMain() {

  // Initialize the UmManager
  umm::UmManager::Init();
  // Generated UM Instance from the linked in Elf 
  auto snap = umm::ElfLoader::CreateInstanceFromElf(&_sv_start);
  umm::manager->Load(std::move(snap));

  // Set breakpoint for snapshot 
  ebbrt::Future<umm::UmSV> snap_f = umm::manager->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Start TCP connection after snapshot breakpoint
  snap_f.Then([](ebbrt::Future<umm::UmSV> snap_f) {
    app_session = umm::proxy->Connect(8080);
  }); // End snap_f.Then(...)

  // Start the execution	
  umm::manager->Kickoff();
}
