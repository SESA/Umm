//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>
#include <ebbrt/native/Cpu.h>
#include <ebbrt/native/Debug.h>

#include "net_test.h"

TcpSession *tcp_session_;

void TcpSession::Connected() {
  ebbrt::kprintf_force("App TCP connected!!!!!! \n");
}

void TcpSession::Receive(std::unique_ptr<ebbrt::MutIOBuf> b) {
  ebbrt::kprintf_force("App TCP receive len=%d chain_len=%d\n",
                       b->ComputeChainDataLength(), b->CountChainElements());
}

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
    //umm::UmSV snap = snap_f.Block().Get();
    ebbrt::NetworkManager::TcpPcb pcb;
    std::array<uint8_t, 4> foo = {{169, 254, 1, 0}};
    pcb.Connect(ebbrt::Ipv4Address(foo), 8080);
    tcp_session_ = new TcpSession(std::move(pcb));
    tcp_session_->Install();
  }); // End snap_f.Then(...)

  // Start the execution	
  umm::manager->Start();
}
