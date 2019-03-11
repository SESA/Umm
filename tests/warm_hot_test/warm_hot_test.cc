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
#include <InvocationSession.h>

#include <chrono> 
#include <iostream> 
using namespace std; 
using namespace std::chrono; 

class AppTcpSession;

AppTcpSession* app_session;

int
hexdump(const unsigned char *start, const int lines) {
  int j;
  for (j=0; j<lines; j++){
    int offset=j*16;
    ebbrt::kprintf_force("%08x:  ", offset);
    for (int i=0;i<16;i++){
      ebbrt::kprintf_force("%02x  ", start[offset+i]);
    }
    ebbrt::kprintf_force("|");
    for (int i=0;i<16;i++){
      unsigned char c=start[offset+i];
      if (c>=' ' && c<='~')  ebbrt::kprintf_force("%c", c);
      else  ebbrt::kprintf_force(".");
    }
    ebbrt::kprintf_force("|\n");
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
    ebbrt::kprintf_force("**********************\n");
    hexdump(str_ptr, lines);
    ebbrt::kprintf_force("**********************\n");
  };

  ebbrt::Future<void> is_connected;

private:
  ebbrt::Promise<void> set_connected;
}; // end TcpSession

const std::string my_cmd = R"({"cmdline":"bin/node-default /nodejsActionBase/app.js",
 "net":{"if":"ukvmif0","cloner":"true","type":"inet","method":"static","addr":"169.254.1.0","mask":"16", "gw":"169.254.1.0"}})";
umm::UmSV* snap_sv;
umm::UmSV* hot_sv;

umm::UmSV& getSVFromElf(){
  // Generated UM Instance from the linked in Elf
  ebbrt::kprintf_force(YELLOW "Creating first sv starts at %p...\n" RESET, &_sv_start);
  return umm::ElfLoader::createSVFromElf(&_sv_start);
}

std::unique_ptr<umm::UmInstance> getUMIFromSV(umm::UmSV& sv){
  // Create instance.
  // ebbrt::kprintf_force(YELLOW "Creating first umi...\n" RESET);
  return std::make_unique<umm::UmInstance>(sv);
}

void generateBaseEnvtSnapshotSV(){

  // Use ELF to make SV, use sv to make umi.
  std::unique_ptr<umm::UmInstance> umi = getUMIFromSV( getSVFromElf() );
  // for (auto &reg : umi->sv_.region_list_)
  //   reg.Print();
  // while(1);

  // Configure solo5 boot arguments
  {
    uint64_t argc = Solo5BootArguments(umi->sv_.GetRegionByName("usr").start,
                                       SOLO5_USR_REGION_SIZE, my_cmd);
    umi->SetArguments(argc);
  }

  // Set breakpoint for snapshot
  ebbrt::Future<umm::UmSV *> snap_f =
      umi->SetCheckpoint(umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Halt sv after future is fulfilled.
  snap_f.Then([](ebbrt::Future<umm::UmSV *> snap_f) {
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
  umm::manager->Run(std::move(umi));
}

void deployServerFromSnap(){
  // ebbrt::kprintf_force(YELLOW "Creating 2nd umi...\n" RESET);
  auto umi2 = getUMIFromSV( *snap_sv );

  ebbrt::event_manager->SpawnLocal(
      [=]() {
        // ebbrt::kprintf_force(YELLOW "New spawn local\n" RESET);
        ebbrt::NetworkManager::TcpPcb pcb;
        app_session = new AppTcpSession(std::move(pcb));
        app_session->Install();
        app_session->is_connected.Then([=](auto f) {
          // ebbrt::kprintf_force(GREEN "Alright, we are connected!\n" RESET);
        });

        // ebbrt::kprintf_force(YELLOW "Attempting connection\n" RESET);
        app_session->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080);
      },
      /* force_async = */ true);

  umm::manager->Run(std::move(umi2));
}

umm::InvocationSession* create_session(uint64_t tid, size_t fid) {
  // NOTE: old way of stack allocating pcb and istats.
  // ebbrt::NetworkManager::TcpPcb pcb;
  // InvocationStats istats = {0};

  auto pcb = new ebbrt::NetworkManager::TcpPcb;
  auto isp = new umm::InvocationStats;

  *isp = {0};
  // (*isp).transaction_id = tid;
  // (*isp).function_id = fid;
  // TODO: leak
  return new umm::InvocationSession(std::move(*pcb), *isp);
}

void deployAndInitServerAndRun(){
  // kprintf_force(YELLOW "Processing WARM start \n" RESET);
  uint64_t tid = 0;
  size_t fid = 0;
  const std::string args = std::string(R"({"spin":"0"})");
  // const std::string code =R"(function main(args) { var count=args.foo; console.log(count); return {done:true, c:count}; })";

  const std::string code =R"(function spin(pow2_count){ var count = 0; var max = 1<<pow2_count; for (var line=1; line<max; line++) { count++; } return {done:true, c:count}; }; function httpget(url) { return new Promise((resolve, reject) => { setTimeout(function() { resolve({ done: true }); }, 10000); const lib = require('http'); const request = lib.get(url, (response) => { if (response.statusCode < 200 || response.statusCode > 299) { reject(new Error('Failed to load page, status code: ' + response.statusCode)); } const body = []; response.on('data', (chunk) => body.push(chunk)); response.on('end', () => resolve({done:true, code:response.statusCode, msg:body.join('')})); }); request.on('error', (err) => reject(err)) }) }; function main(args) { if(!args){ return spin(0); } else{ if('spin' in args){ return spin(args.spin); } if('url' in args){ return httpget(args.url);}return spin(0);}};)";

  /* Load up the base snapshot environment */
  ebbrt::kprintf_force(YELLOW "Loading from snap\n" RESET);

  auto umi = getUMIFromSV( *snap_sv );

  ebbrt::Future<umm::UmSV*> hot_sv_f = umi->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // HACK: subvert SetCheckpoint

  // x86_64::DR0 dr0;
  // dr0.get();
  // dr0.val = 0;
  // dr0.set();

  // auto umi_id = umi->Id();
  auto umsesh = create_session(tid, fid);

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [umsesh] {
        // Start a new TCP connection with the http request
        uint16_t port = 0;
        umsesh->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
      },
      /* force async */ true);

  /* Setup the asyncronous operations on the InvocationSession */
  umsesh->WhenConnected().Then([umsesh, code](auto f) {
    // ebbrt::kprintf_force(YELLOW "Connected, sending init\n" RESET);
    umsesh->SendHttpRequest("/init", code, true /* keep_alive */);
  });

  umsesh->WhenInitialized().Then([umsesh, args](auto f) {
    umsesh->SendHttpRequest("/run", args, false);
  });

  // Halt when closed or aborted
  umsesh->WhenClosed().Then([](auto f) {
    // ebbrt::kprintf_force(YELLOW "Connection Closed...\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [] {
          // ebbrt::kprintf_force(YELLOW "Halting...\n" RESET);
          // umm::manager->SignalHalt(umi_id); /* Return to back to
          // init_code_and_snap */
          umm::manager->Halt();
        },
        /* force async */ true);
  });

  /* Boot the snapshot */
  ebbrt::kprintf_force(YELLOW "About to run umi\n" RESET);
  umm::manager->ctr_list.clear();

  // umm::manager->Run(std::move(umi));
  auto start_run = high_resolution_clock::now();
  umi = std::move(umm::manager->Run(std::move(umi)));
  auto end_run = high_resolution_clock::now();

  umi->pfc.dump_ctrs();
  umm::manager->ctr.dump_list(umm::manager->ctr_list);

  auto run_duration = duration_cast<microseconds>(end_run - start_run);
  cout << "Run duration: " << run_duration.count() << " microseconds" << endl;
  // cout << "Snapshot is this many pages: " << umm::manager->num_cp_pgs << endl;

  /* RETURN HERE AFTER HALT */
  delete umsesh;
  //kprintf_force(YELLOW "Finished WARM start \n" RESET);
}

void generateHotSnapshotSV(){
  // kprintf_force(YELLOW "Processing WARM start \n" RESET);

  const std::string code =R"(function spin(pow2_count){ var count = 0; var max = 1<<pow2_count; for (var line=1; line<max; line++) { count++; } return {done:true, c:count}; }; function httpget(url) { return new Promise((resolve, reject) => { setTimeout(function() { resolve({ done: true }); }, 10000); const lib = require('http'); const request = lib.get(url, (response) => { if (response.statusCode < 200 || response.statusCode > 299) { reject(new Error('Failed to load page, status code: ' + response.statusCode)); } const body = []; response.on('data', (chunk) => body.push(chunk)); response.on('end', () => resolve({done:true, code:response.statusCode, msg:body.join('')})); }); request.on('error', (err) => reject(err)) }) }; function main(args) { if(!args){ return spin(0); } else{ if('spin' in args){ return spin(args.spin); } if('url' in args){ return httpget(args.url);}return spin(0);}};)";

  /* Load up the base snapshot environment */
  ebbrt::kprintf_force(YELLOW "Loading base env from snap\n" RESET);

  auto umi = getUMIFromSV( *snap_sv );

  ebbrt::Future<umm::UmSV*> hot_sv_f = umi->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Halt sv after future is fulfilled.
  hot_sv_f.Then([](ebbrt::Future<umm::UmSV *> hot_sv_f) {
    // Spawn asyncronously allows the debug context clean up correctly
    hot_sv = hot_sv_f.Get();
    // ebbrt::kprintf_force(YELLOW "Got snapshot, about to call halt\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [=]() {
          ebbrt::kprintf_force(YELLOW "Halting first execution...\n" RESET);
          umm::manager->Halt(); /* Does not return */
        },
        /* force_async = */ true);
  }); // End snap_f.Then(...)

  // HACK: subvert SetCheckpoint
  // x86_64::DR0 dr0;
  // dr0.get();
  // dr0.val = 0;
  // dr0.set();

  uint64_t tid = 0;
  size_t fid = 0;
  auto umsesh = create_session(tid, fid);

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [umsesh] {
        // Start a new TCP connection with the http request
        uint16_t port = 0;
        umsesh->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
      },
      /* force async */ true);

  /* Setup the asyncronous operations on the InvocationSession */
  umsesh->WhenConnected().Then(
      [umsesh, code](auto f) {
        // ebbrt::kprintf_force(YELLOW "Connected, sending init\n" RESET);
        umsesh->SendHttpRequest("/init", code, true /* keep_alive */);
      });

  // Halt when closed or aborted
  umsesh->WhenClosed().Then([](auto f) {
    // ebbrt::kprintf_force(YELLOW "Connection Closed...\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [] {
          ebbrt::kprintf_force(YELLOW "Halting...\n" RESET);
          // umm::manager->SignalHalt(umi_id); /* Return to back to
          // init_code_and_snap */
          umm::manager->Halt();
        },
        /* force async */ true);
  });

  umsesh->WhenAborted().Then([](auto f) {
    ebbrt::event_manager->SpawnLocal(
        [] {
          ebbrt::kprintf_force(RED "SESSION ABORTED...\n" RESET);
          umm::manager->Halt(); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });

  /* Boot the snapshot */

  ebbrt::kprintf_force(YELLOW "About to run umi\n" RESET);
  umm::manager->ctr_list.clear();

  // umm::manager->Run(std::move(umi));
  auto start_run = high_resolution_clock::now();
  umi = std::move(umm::manager->Run(std::move(umi)));
  auto end_run = high_resolution_clock::now();

  // umi->pfc.dump_ctrs();
  // umm::manager->ctr.dump_list(umm::manager->ctr_list);

  auto run_duration = duration_cast<microseconds>(end_run - start_run);
  // cout << "Run duration: " << run_duration.count() << " microseconds" << endl;
  // cout << "Snapshot is this many pages: " << umm::manager->num_cp_pgs << endl;

  /* RETURN HERE AFTER HALT */
  // delete umsesh;
 
}

void deployHotSnapshotSV(){
  // void deployHotSnapshotSV(){
  ebbrt::kprintf_force(CYAN "Deploy hot snap sv \n" RESET);
  
  auto umi2 = getUMIFromSV( *hot_sv );
  const std::string args = std::string(R"({"spin":"0"})");

  uint64_t tid = 0;
  size_t fid = 0;
  auto umsesh = create_session(tid, fid);

  ebbrt::event_manager->SpawnLocal(
      [umsesh] {
        uint16_t port = 49161;
        umsesh->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
      },
      /* force async */ true);

  umsesh->WhenConnected().Then([umsesh, args](auto f) {
    umsesh->SendHttpRequest("/run", args, false);
  });
  // Halt when closed or aborted

  umsesh->WhenClosed().Then([](auto f) {
    ebbrt::event_manager->SpawnLocal([] { umm::manager->Halt(); },
                                     /* force async */ true);
  });

  umm::manager->ctr_list.clear();
  auto start_run = high_resolution_clock::now();
  umi2 = std::move(umm::manager->Run(std::move(umi2)));
  auto end_run = high_resolution_clock::now();

  umi2->pfc.dump_ctrs();
  umm::manager->ctr.dump_list(umm::manager->ctr_list);

  ebbrt::kprintf_force(YELLOW "Run finished.\n" RESET);
  auto run_duration = duration_cast<microseconds>(end_run - start_run);
  cout << "Run duration: " << run_duration.count() << " microseconds" << endl;

}
#if 0
void registerHotConnect(auto umsesh) {
  ebbrt::event_manager->SpawnLocal(
      [umsesh] {
        // Start a new TCP connection with the http request
        ebbrt::kprintf_force(YELLOW "Hot start connect \n" RESET);
        uint16_t port = 49161;
        umsesh->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
      },
      /* force async */ true);
}

void registerHotSendRun(auto umsesh) {
  umsesh->WhenConnected().Then([umsesh](auto f) {
    ebbrt::kprintf_force(YELLOW "Connected, sending run\n" RESET);
    const std::string args = std::string(R"({"spin":"0"})");
    umsesh->SendHttpRequest("/run", args, false);
  });
}
#endif

// void registerSpicyRun(auto umsesh2, auto start_run, auto spicy_record){
// umsesh2->WhenConnected().Then([umsesh2, &start_run, &spicy_record](auto f) {
//                                 ebbrt::kprintf_force(YELLOW "Connected, sending run\n" RESET);

//                                 start_run = high_resolution_clock::now();
//                                 auto this_umi = umm::manager->ActiveInstance();
//                                 kassert( this_umi != nullptr );
//                                 this_umi->pfc.zero_ctrs();
//                                 spicy_record = umm::manager->ctr.CreateTimeRecord(std::string("Spicy"));

//                                 const std::string args = std::string(R"({"spin":"0"})");
//                                 umsesh2->SendHttpRequest("/run", args, false);
//                               });
// }

// void registerSpicyHalt(auto umsesh2,
//                        std::chrono::high_resolution_clock::time_point spicy_start;
//                        auto spicy_record, auto spicy_end) {

//   ebbrt::kprintf_force(RED "Halt without clock read\n" RESET);

//   umsesh2->WhenClosed().Then([spicy_record, &spicy_end](auto f) {
//     umm::manager->ctr.add_to_list(umm::manager->ctr_list, spicy_record);
//     spicy_end = high_resolution_clock::now();

//     ebbrt::event_manager->SpawnLocal([] { umm::manager->Halt(); },
//                                      /* force async */ true);
//                              });
// }
uint16_t port_ = 0;

uint16_t get_internal_port() {
  port_ += ebbrt::Cpu::Count();
  size_t port_offset = (size_t)ebbrt::Cpu::GetMine();
  return 49160 + (port_ + port_offset);
}

void deploySpicy(){
  // umm::count::Counter::TimeRecord *spicy_record = umm::manager->ctr.CreateTimeRecord(std::string("Don't print"));

  auto spicy_record = new umm::count::Counter::TimeRecord();
  auto spicy_start = new high_resolution_clock::time_point();
  auto spicy_end = new high_resolution_clock::time_point();

    ebbrt::kprintf_force(YELLOW "Hot Connection Closed...\n" RESET);
    // Start a new TCP connection for Spicy start.
    auto umsesh2 = create_session(0, 0);

    ebbrt::kprintf_force(YELLOW "Spicy Hot start connect \n" RESET);

    // When connected for spicy, send run.
    umsesh2->WhenConnected().Then([umsesh2, spicy_start, spicy_record](auto f) {
      ebbrt::kprintf_force(YELLOW "Connected, sending run\n" RESET);

      auto this_umi = umm::manager->ActiveInstance();
      kassert( this_umi != nullptr );
      this_umi->pfc.zero_ctrs();

      umm::manager->ctr_list.clear(); // Clear placeholder.
      *spicy_record = umm::manager->ctr.CreateTimeRecord(std::string("Spicy"));
      *spicy_start = high_resolution_clock::now();

      const std::string args = std::string(R"({"spin":"0"})");
      umsesh2->SendHttpRequest("/run", args, false);
    });

    umsesh2->WhenFinished().Then([spicy_record, spicy_end,
                                  spicy_start](auto f) {
      ebbrt::kprintf_force(RED "Finished baby!\n" RESET);
      umm::manager->ctr.add_to_list(umm::manager->ctr_list, *spicy_record);
      *spicy_end = high_resolution_clock::now();
      ebbrt::kprintf_force(MAGENTA "start = %d , end = %d \n" RESET, *spicy_start, *spicy_end);
      auto run_duration = duration_cast<microseconds>(*spicy_end - *spicy_start);
      umm::manager->ctr.dump_list(umm::manager->ctr_list);
      cout << "Run duration: " << run_duration.count() << " microseconds" << endl;

    });

    // When spicy finished, halt.
    umsesh2->WhenClosed().Then([](auto f) {
      ebbrt::event_manager->SpawnLocal([] {
            umm::manager->Halt();
          },
          /* force async */ true);
    });

    // registerSpicyHalt(umsesh2, spicy_record, &spicy_end);

    // Connect!
    {
      uint16_t port = get_internal_port();
      umsesh2->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
    }

}

void deploySpicyHotSnapshotSV(){
  // Run a hot start, then a spicy hot start.

  auto umi2 = getUMIFromSV( *hot_sv );

  // Hot start session.
  auto umsesh = create_session(0, 0);

  ebbrt::event_manager->SpawnLocal(
      [umsesh] {
        // Start a new TCP connection with the http request
        ebbrt::kprintf_force(YELLOW "Hot start connect \n" RESET);
        uint16_t port = get_internal_port();
        umsesh->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
      },
      /* force async */ true);

  umsesh->WhenConnected().Then([umsesh](auto f) {
    ebbrt::kprintf_force(YELLOW "Connected, sending run\n" RESET);
    const std::string args = std::string(R"({"spin":"0"})");
    umsesh->SendHttpRequest("/run", args, false);
  });

  // Stack allocated placeholders for our counters
  // auto spicy_record = umm::manager->ctr.CreateTimeRecord(std::string("Don't print"));
  // high_resolution_clock::time_point spicy_start;
  // high_resolution_clock::time_point spicy_end;
  // auto spicy_end = high_resolution_clock::now();
  // auto spicy_start = high_resolution_clock::now();

  // ebbrt::kprintf_force(MAGENTA "declare start = %d , end = %d \n" RESET, spicy_start, spicy_end);
  // auto r_d = duration_cast<microseconds>(spicy_end - spicy_start);
  // ebbrt::kprintf_force(MAGENTA "start duration %d\n" RESET, r_d.count());


  // When the hot start connection is closed, launch Spicy.
  umsesh->WhenClosed().Then([](auto f) {
    deploySpicy();
    // deploySpicy();
    // deploySpicy();
    // deploySpicy();
  });

  // Run spicy hot start.
  umi2 = std::move(umm::manager->Run(std::move(umi2)));
  umi2->pfc.dump_ctrs();

  ebbrt::kprintf_force(YELLOW "Run finished.\n" RESET);
}

void AppMain() {

  // Initialize the UmManager
  umm::UmManager::Init();

  // This populates the global variable snap_sv.
  ebbrt::kprintf_force(YELLOW "Generating Snap!\n" RESET);
  generateBaseEnvtSnapshotSV();

  // ebbrt::kprintf_force(YELLOW "Run server and init!\n" RESET);
  // deployAndInitServerAndRun();
  generateHotSnapshotSV();

  // deployHotSnapshotSV();
  deploySpicyHotSnapshotSV();

  ebbrt::kprintf_force(CYAN "Done!\n" RESET);
}
