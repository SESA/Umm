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

const std::string my_cmd = R"({"cmdline":"bin/node-default /nodejsActionBase/app.js",
 "net":{"if":"ukvmif0","cloner":"true","type":"inet","method":"static","addr":"169.254.1.0","mask":"16"}})";
umm::UmSV* snap_sv;

umm::UmSV& getSVFromElf(){
  // Generated UM Instance from the linked in Elf
  // ebbrt::kprintf_force(YELLOW "Creating first sv...\n" RESET);
  return umm::ElfLoader::createSVFromElf(&_sv_start);
}

std::unique_ptr<umm::UmInstance> getUMIFromSV(umm::UmSV& sv){
  // Create instance.
  // ebbrt::kprintf_force(YELLOW "Creating first umi...\n" RESET);
  return std::make_unique<umm::UmInstance>(sv);
}

void generateSnapshotSV(){

  // Use ELF to make SV, use sv to make umi.
  std::unique_ptr<umm::UmInstance> umi = getUMIFromSV( getSVFromElf() );

  // Configure solo5 boot arguments
  {
    uint64_t argc = Solo5BootArguments(umi->sv_.GetRegionByName("usr").start,
                                       SOLO5_USR_REGION_SIZE, my_cmd);
    umi->SetArguments(argc);
  }

  // Set breakpoint for snapshot
  ebbrt::Future<umm::UmSV*> snap_f = umi->SetCheckpoint(
                                                        umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Halt sv after future is fulfilled.
  snap_f.Then([](ebbrt::Future<umm::UmSV*> snap_f) {
    // Spawn asyncronously allows the debug context clean up correctly
    snap_sv = snap_f.Get();
    // ebbrt::kprintf_force(YELLOW "Got snapshot, about to call halt\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [=]() {
          // ebbrt::kprintf_force(YELLOW "Halting first execution...\n" RESET);
          umm::manager->Halt(); /* Does not return */
        },
        /* force_async = */ true);
  }); // End snap_f.Then(...)

  // Start the first execution.
  // ebbrt::kprintf_force(YELLOW "Running orig execution\n" RESET);
  umm::manager->Run(std::move(umi));
  // ebbrt::kprintf_force(YELLOW "We've populated the snapshot sv, snap_sv\n");
}

void deployServerFromSnap(){
  // ebbrt::kprintf_force(YELLOW "Sleeping for 5 seconds...\n" RESET);
  ebbrt::clock::SleepMilli(2000);

  // ebbrt::kprintf_force(YELLOW "Creating 2nd umi...\n" RESET);
  auto umi2 = getUMIFromSV( *snap_sv );

  ebbrt::event_manager->SpawnLocal(
      [=]() {
        // ebbrt::kprintf_force(YELLOW "New spawn local\n" RESET);
        ebbrt::NetworkManager::TcpPcb pcb;
        app_session = new AppTcpSession(std::move(pcb));
        app_session->Install();
        app_session->is_connected.Then([=](auto f) {
          ebbrt::kprintf_force(GREEN "Alright, we are connected!\n" RESET);
        });

        // ebbrt::kprintf_force(YELLOW "Attempting connection\n" RESET);
        app_session->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080);
      },
      /* force_async = */ true);

  // ebbrt::kprintf_force(YELLOW "Run 2nd umi!\n" RESET);
  umm::manager->Run(std::move(umi2));
}

void AppMain() {
  // Initialize the UmManager
  umm::UmManager::Init();

  // This populates the global variable snap_sv.
  generateSnapshotSV();

  deployServerFromSnap();


  // ebbrt::kprintf("Done!\n");
}
