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
    kprintf("%08x:  ", offset);
    for (int i=0;i<16;i++){
      kprintf("%02x  ", start[offset+i]);
    }
    kprintf("|");
    for (int i=0;i<16;i++){
      unsigned char c=start[offset+i];
      if (c>=' ' && c<='~')  kprintf("%c", c);
      else  kprintf(".");
    }
    kprintf("|\n");
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

    //InitCodeTest();
    NullCodeTest();

    size_t mycpu = ebbrt::Cpu::GetMine();
    auto ncpus = ebbrt::Cpu::Count();

    ebbrt::event_manager->SpawnRemote(
        []() {
          ebbrt::clock::SleepMilli(100);
          app_session->InitCodeTest();
        },
        (mycpu+1)%ncpus);
  }

  void Abort() { ebbrt::kprintf_force("UmProxy TCP connection aborted \n"); }

  void Receive(std::unique_ptr<ebbrt::MutIOBuf> b) {
    ebbrt::kprintf_force("UmProxy received data : len=%d\n",
                         b->ComputeChainDataLength());
    auto str_ptr = reinterpret_cast<const unsigned char *>(b->Data());
    int lines = (b->ComputeChainDataLength() / 16)+1;
    kprintf("**********************\n");
    hexdump(str_ptr, lines);
    kprintf("**********************\n");
  };
  
  void NullCodeTest(){
    auto buf = ebbrt::MakeUniqueIOBuf(0);
    Send(std::move(buf));
  }

  void InitCodeTest() {
    
    size_t mycpu = ebbrt::Cpu::GetMine();
    kprintf(YELLOW "KICKING OFF InitCode Test on code #%d\n" RESET,(size_t)mycpu);
    const std::string code =
        R"({"value": {"main":"main", "code":"function main(msg){console.log(msg);}"}})";
    auto header = std::string("POST /init HTTP/1.0\r\n"
                              "Content-Type:application/json\r\n"
                              "content-length: 74\r\n\r\n") +
                  code;

                              //"Connection: keep-alive\r\n"
    kprintf("Attempt Code Initialization: code_len=%d msg_len=%d\n",
            code.size(), header.size());
    auto buf = ebbrt::MakeUniqueIOBuf(header.size());
    auto dp = buf->GetMutDataPointer();
    auto str_ptr = reinterpret_cast<char *>(dp.Data());
    header.copy(str_ptr, header.size());
    Send(std::move(buf));
  }

  void TestRunCode() {
    const std::string run_arg =
        R"({"value": {"payload":"WELL FINALLY WORKS!"}})";
    kprintf("LEN OF RUN CODE: %d\n", run_arg.size());
    auto run = std::string("POST /run HTTP/1.0\r\n"
                           "Content-Type:application/json\r\n"
                           "Connection: keep-alive\r\n"
                           "content-length: 44\r\n\r\n") +
               run_arg;
    auto buf = ebbrt::MakeUniqueIOBuf(run.size());
    auto dp = buf->GetMutDataPointer();
    auto str_ptr = reinterpret_cast<char *>(dp.Data());
    run.copy(str_ptr, run.size());
    Send(std::move(buf));
  }
  ebbrt::Future<void> is_connected;

private:
  ebbrt::Promise<void> set_connected;
}; // end TcpSession


void AppMain() {
  // Initialize the UmManager
  umm::UmManager::Init();
  // Generated UM Instance from the linked in Elf
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);
  // Create instance.
  auto umi = std::make_unique<umm::UmInstance>(sv);
  // Configure solo5 boot arguments
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
  umi->SetArguments(argc);
  umm::manager->Load(std::move(umi));

  // Set breakpoint for snapshot
  ebbrt::Future<umm::UmSV> snap_f = umm::manager->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Start TCP connection after snapshot breakpoint
  snap_f.Then([](ebbrt::Future<umm::UmSV> snap_f) {
    ebbrt::NetworkManager::TcpPcb pcb;
    app_session = new AppTcpSession(std::move(pcb));
    app_session->Install();
    std::array<uint8_t, 4> umip = {{169, 254, 1, 0}};
    app_session->Pcb().Connect(ebbrt::Ipv4Address(umip), 8080);
  }); // End snap_f.Then(...)

  // Start the execution
  umm::manager->runSV();
}
