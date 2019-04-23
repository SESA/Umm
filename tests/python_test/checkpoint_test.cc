//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/Cpu.h>
#include <ebbrt/UniqueIOBuf.h>
#include <ebbrt/EventManager.h>
#include <ebbrt/Future.h>
#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Clock.h>
#include <ebbrt/native/Debug.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/NetChecksum.h>
#include <ebbrt/native/NetTcpHandler.h>
#include <ebbrt/native/NetUdp.h>

#include <UmProxy.h>
#include <Umm.h>
#include <InvocationSession.h>

#include <chrono> 
#include <iostream> 
#include <list>

using namespace std; 
using namespace std::chrono; 

const std::string my_cmd = R"({"cmdline":"poopthon -m main","env":"PYTHONHOME=/python", "net":{"if":"ukvmif0","cloner":"true","type":"inet","method":"static","addr":"169.254.1.0","mask":"16"}})";

const std::string code =R"(print('this is some python code\n'))";
const std::string args = std::string(R"({"OK":True})");


umm::UmSV* snap_sv;


uint16_t port_ = 0;
uint16_t get_internal_port() {
  port_ += ebbrt::Cpu::Count();
  size_t port_offset = (size_t)ebbrt::Cpu::GetMine();
  return 49160 + (port_ + port_offset);
}


uint64_t tid = 0;
size_t fid = 0;
umm::InvocationSession* create_session(uint64_t tid, size_t fid) {
  auto pcb = new ebbrt::NetworkManager::TcpPcb;
  auto isp = new umm::InvocationStats;

  *isp = {0};
  // (*isp).transaction_id = tid;
  // (*isp).function_id = fid;
  // TODO: leak
  return new umm::InvocationSession(std::move(*pcb), *isp);
}


std::unique_ptr<umm::UmInstance> getUMIFromSV(umm::UmSV& sv){
  return std::make_unique<umm::UmInstance>(sv);
}


std::unique_ptr<umm::UmInstance> initInstance(){
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);
  auto umi = std::make_unique<umm::UmInstance>(sv);

  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE, my_cmd);
  umi->SetArguments(argc);

  return std::move(umi);
}


void deployUntilBase() {
  auto umi = initInstance();

  ebbrt::Future<umm::UmSV *> snap_f = umi->SetCheckpoint(umm::ElfLoader::GetSymbolAddress("__gmtime50"));

  snap_f.Then([](ebbrt::Future<umm::UmSV *> snap_f) {
    snap_sv = snap_f.Get();
    ebbrt::kprintf_force(YELLOW "Hit checkpoint, got snapshot...\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [=]() {
                ebbrt::kprintf_force(YELLOW "About to call halt...\n" RESET);
                umm::manager->Halt(); }, true);
  });

  umm::manager->Run(std::move(umi));
}


void deployFromBase() {
  auto umi = getUMIFromSV( *snap_sv );

  auto umsesh = create_session(tid, fid);
  auto umsesh2 = create_session(tid, fid);

  ebbrt::event_manager->SpawnLocal(
      [umsesh] {
        ebbrt::kprintf_force(YELLOW "Trying to connect to Umi for init...\n" RESET);
        uint16_t port = get_internal_port();
        umsesh->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 5000, port);
      }, true);

  umsesh->WhenConnected().Then([umsesh](auto f) {
    ebbrt::kprintf_force(YELLOW "Connected, can send init...\n" RESET);
    umsesh->SendHttpRequest("/init", code, true);
  });


  umsesh->WhenClosed().Then([umsesh2](auto f) {
    ebbrt::kprintf_force(YELLOW "Initialized, can send run...\n" RESET);
    ebbrt::kprintf_force(YELLOW "Connection closed after init...\n" RESET);

    ebbrt::event_manager->SpawnLocal([umsesh2] {
      ebbrt::kprintf_force(YELLOW "Trying to connect to Umi for run...\n" RESET);
      uint16_t port = get_internal_port();
      umsesh2->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 5000, port);
    }, true);

    umsesh2->WhenConnected().Then([umsesh2](auto f) {
      ebbrt::kprintf_force(YELLOW "Connected, can send run...\n" RESET);
      umsesh2->SendHttpRequest("/run", args, true);
    });

    umsesh2->WhenClosed().Then([](auto f) {
      ebbrt::kprintf_force(YELLOW "Connection closed after run...\n" RESET);
      ebbrt::event_manager->SpawnLocal(
          [] { umm::manager->Halt(); }, true);
    });

  });


  ebbrt::kprintf_force(YELLOW "About to deploy from base...\n" RESET);
  umm::manager->Run(std::move(umi));

  delete umsesh;
  delete umsesh2;
  ebbrt::kprintf_force(YELLOW "Done...\n" RESET);
}


void AppMain() {
  umm::UmManager::Init();

  deployUntilBase();
  deployFromBase();

  cout << "powering off: " << endl;
  ebbrt::acpi::PowerOff();
}
