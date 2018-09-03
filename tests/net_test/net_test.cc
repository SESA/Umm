//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/Cpu.h>
#include <ebbrt/UniqueIOBuf.h>
#include <ebbrt/EventManager.h>
#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>
#include <ebbrt/native/Debug.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/NetChecksum.h>
#include <ebbrt/native/NetTcpHandler.h>
#include <ebbrt/native/NetUdp.h>

#include <UmProxy.h>
#include <Umm.h>

class AppTcpSession;

AppTcpSession* app_session;

int
hexdump(const unsigned char *start, const int lines) {
  int j;
  for (j=0; j<lines; j++){
    int offset=j*16;
    ebbrt::kprintf("%08x:  ", offset);
    for (int i=0;i<16;i++){
      ebbrt::kprintf("%02x  ", start[offset+i]);
    }
    ebbrt::kprintf("|");
    for (int i=0;i<16;i++){
      unsigned char c=start[offset+i];
      if (c>=' ' && c<='~')  ebbrt::kprintf("%c", c);
      else  ebbrt::kprintf(".");
    }
    ebbrt::kprintf("|\n");
  }
  return j;
}

/** AppTcpSession handler
 * 	Manages a TCP connection between the Um instance and the UmProxy
 */
class AppTcpSession : public ebbrt::TcpHandler {
public:
  AppTcpSession(ebbrt::NetworkManager::TcpPcb pcb)
      : ebbrt::TcpHandler(std::move(pcb)) {
    ebbrt::kprintf_force("UmProxy starting TCP connection \n");
    is_connected = set_connected.GetFuture();
  }

  void Close() { ebbrt::kprintf_force("UmProxy TCP connection closed \n"); }

  void Connected() {
    ebbrt::kprintf_force("UmProxy TCP connection established  \n");
    set_connected.SetValue();
  }

  void Abort() { ebbrt::kprintf_force("UmProxy TCP connection aborted \n"); }

  void Receive(std::unique_ptr<ebbrt::MutIOBuf> b) {
    ebbrt::kprintf_force("UmProxy received data : len=%d\n",
                         b->ComputeChainDataLength());
    auto str_ptr = reinterpret_cast<const unsigned char *>(b->Data());
    int lines = (b->ComputeChainDataLength() / 16)+1;
    ebbrt::kprintf("**********************\n");
    hexdump(str_ptr, lines);
    ebbrt::kprintf("**********************\n");
  };
  
  ebbrt::Future<void> is_connected;

private:
  ebbrt::Promise<void> set_connected;
}; // end TcpSession


umm::UmSV snap_sv;

void AppMain() {
  // Initialize the UmManager
  umm::UmManager::Init();
  // Generated UM Instance from the linked in Elf
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);
  // Create instance.
  auto umi = std::make_unique<umm::UmInstance>(sv);
  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start,
                                     SOLO5_USR_REGION_SIZE);
  umi->SetArguments(argc);
  umm::manager->Load(std::move(umi));

  // Set breakpoint for snapshot
  ebbrt::Future<umm::UmSV> snap_f = umm::manager->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));
  snap_f.Then([](ebbrt::Future<umm::UmSV> snap_f) {
    // Spawn asyncronously allows the debug context clean up correctly
    snap_sv = snap_f.Get();
    ebbrt::event_manager->SpawnLocal(
        [=]() {
          umm::manager->Halt(); /* Does not return */
        },
        /* force_async = */ true);
  }); // End snap_f.Then(...)

  // Start the execution
  umm::manager->runSV();
  umm::manager->Unload();
  ebbrt::kprintf_force("returned from instance... Redeploying snapshop & connecting in 5 seconds..\n");
  ebbrt::clock::SleepMilli(5000);
  auto umi2 = std::make_unique<umm::UmInstance>(snap_sv);
  umm::manager->Load(std::move(umi2));
  ebbrt::event_manager->SpawnLocal(
      [=]() {
        ebbrt::NetworkManager::TcpPcb pcb;
        app_session = new AppTcpSession(std::move(pcb));
        app_session->Install();
        app_session->is_connected.Then([=](auto f) {
          ebbrt::kprintf_force("Alright, we are connected!\n");
        });
        std::array<uint8_t, 4> umip = {{169, 254, 1, 0}};
        app_session->Pcb().Connect(ebbrt::Ipv4Address(umip), 8080);
      },
      /* force_async = */ true);
  umm::manager->runSV();
  umm::manager->Unload();
  ebbrt::kprintf("Done!\n");
}
