//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/UniqueIOBuf.h>
#include <ebbrt/native/Acpi.h>
#include <ebbrt/native/Debug.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/NetChecksum.h>
#include <ebbrt/native/NetTcpHandler.h>
#include <ebbrt/native/NetUdp.h>

#include <Umm.h>
#include <UmProxy.h>

/** AppTcpSession handler
 * 	Manages a TCP connection between the Um instance and the UmProxy
 */
class AppTcpSession : public ebbrt::TcpHandler {
public:
  AppTcpSession(ebbrt::NetworkManager::TcpPcb pcb)
      : ebbrt::TcpHandler(std::move(pcb)) {
    ebbrt::kprintf_force("UmProxy starting TCP connection \n");
  }
  void Close() {
    ebbrt::kprintf_force("UmProxy TCP connection closed \n");
  }
  void Connected() {
    const std::string code =
        R"({"value": {"main":"main", "code":"function main(msg){console.log(msg);}"}})";
    kprintf("LEN OF CODE: %d\n", code.size());
    auto header = std::string("POST /init HTTP/1.0\r\n"
                              "Content-Type:application/json\r\n"
                              "Connection: keep-alive\r\n"
                              "content-length: 74\r\n\r\n") +
                  code;
    auto buf = ebbrt::MakeUniqueIOBuf(header.size());
    auto dp = buf->GetMutDataPointer();
    auto str_ptr = reinterpret_cast<char *>(dp.Data());
    header.copy(str_ptr, header.size());
    Send(std::move(buf));
  }
  void Abort() {
    ebbrt::kprintf_force("UmProxy TCP connection aborted \n");
  }
  void Receive(std::unique_ptr<ebbrt::MutIOBuf> b) {
    ebbrt::kprintf_force("UmProxy received data : len=%d\n", 
                         b->ComputeChainDataLength());
    const std::string run_arg =
        R"({"value": {"payload":"HOLY F*CKING SH*T!!"}})";
    kprintf("LEN OF RUN CODE: %d\n", run_arg.size());
    auto run = std::string("POST /run HTTP/1.0\r\n"
                           "Content-Type:application/json\r\n"
                           "Connection: keep-alive\r\n"
                           "content-length: 44\r\n\r\n") + run_arg;
    auto buf = ebbrt::MakeUniqueIOBuf(run.size());
    auto dp = buf->GetMutDataPointer();
    auto str_ptr = reinterpret_cast<char *>(dp.Data());
    run.copy(str_ptr, run.size());
    Send(std::move(buf));
  };
}; // end TcpSession


AppTcpSession* app_session;

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
    ebbrt::NetworkManager::TcpPcb pcb;
    std::array<uint8_t, 4> umip = {{169, 254, 1, 0}};
    pcb.Connect(ebbrt::Ipv4Address(umip), 8080);
    auto app_session = new AppTcpSession(std::move(pcb));
    app_session->Install();
  }); // End snap_f.Then(...)

  // Start the execution
  umm::manager->Kickoff();
}
