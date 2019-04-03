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
// #include <ebbrt/Future.h>

#include <UmProxy.h>
#include <Umm.h>
#include <InvocationSession.h>

#include <chrono>
#include <iostream>
#include <list>
std::list<int> u_sec_list;

using namespace std;
using namespace std::chrono;

class AppTcpSession;

uint16_t port_ = 0;

uint16_t get_internal_port() {
  port_ += ebbrt::Cpu::Count();
  size_t port_offset = (size_t)ebbrt::Cpu::GetMine();
  return 49160 + (port_ + port_offset);
}
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
umm::UmSV* opt_base_sv;
umm::UmSV* warm_sv;

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

umm::InvocationSession* create_session() {
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

const std::string getDummyCode() {
  // Our javascript function to send in.
  return std::string(R"(
function main(args) {
  console.log('dummy print statement');
  return {done : true};
};
)"
                     );
}

const std::string getCode(){
  // Our javascript function to send in.
  return std::string(R"(
function spin(pow2_count) {
  var count = 0;
  var max = 1 << pow2_count;
  for (var line = 1; line < max; line++) {
    count++;
  }
  return {done : true, c : count};
};

function httpget(url){return new Promise((resolve, reject) => {
  setTimeout(function() { resolve({done : true}); }, 10000);
  const lib = require('http');
  const request = lib.get(url, (response) => {
    if (response.statusCode < 200 || response.statusCode > 299) {
      reject(new Error('Failed to load page, status code: ' +
                       response.statusCode));
    }
    const body = [];
    response.on('data', (chunk) => body.push(chunk));
    response.on(
        'end',
        () => resolve(
            {done : true, code : response.statusCode, msg : body.join('')}));
  });
  request.on('error', (err) => reject(err))
})};

function main(args) {
  if (!args) {
    return spin(0);
  } else {
    if ('spin' in args) {
      return spin(args.spin);
    }
    if ('url' in args) {
      return httpget(args.url);
    }
    return spin(0);
  }
};
)"
                     );

}

const std::string getArgs(){
  return std::string(R"({"spin":"0"})");
}

void regSnapshot(umm::UmSV **snap, ebbrt::Future<umm::UmSV *> *warm_sv_f){
    // Halt sv after future is fulfilled.
    warm_sv_f->Then([snap](ebbrt::Future<umm::UmSV *> warm_sv_f) {
      // Spawn asyncronously allows the debug context clean up correctly
      *snap = warm_sv_f.Get();
      // ebbrt::kprintf_force(RED "BaseEnvtSnap owns %d pages\n" RESET,
      // snap_sv->CountOwnedPages());
      ebbrt::event_manager->SpawnLocal(
          [=]() {
            umm::manager->Halt(); /* Does not return */
          },
          /* force_async = */ true);
    }); // End snap_f.Then(...)
}

void regConnect(auto umsesh){
  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [umsesh] {
        // Start a new TCP connection with the http request
        // ebbrt::kprintf_force(YELLOW "Connecting \n" RESET);
        // uint16_t port = 0;
        uint16_t port = get_internal_port();
        (*umsesh)->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
      },
      /* force async */ true);
}

void regSendInitOnConnect(auto umsesh, bool keepAlive){
  /* Setup the asyncronous operations on the InvocationSession */
  (*umsesh)->WhenConnected().Then([umsesh, keepAlive](auto f) {
    // ebbrt::kprintf_force(YELLOW "Connected, sending init\n" RESET);
      (*umsesh)->SendHttpRequest("/init", getCode(), keepAlive /* keep_alive */);
  });
}

void regSendRunOnConnect(auto umsesh){
  (*umsesh)->WhenConnected().Then([umsesh](auto f) {
    // ebbrt::kprintf_force(YELLOW "Connected, sending run\n" RESET);
    (*umsesh)->SendHttpRequest("/run", getArgs(), false);
  });
}

void regSendRunOnInit(auto umsesh){
  (*umsesh)->WhenInitialized().Then([umsesh](auto f) {
    // ebbrt::kprintf_force(YELLOW "Initted sending run\n" RESET);
    (*umsesh)->SendHttpRequest("/run", getArgs(), false);
  });
}

void regHaltOnClose(auto umsesh){
  // Halt when closed or aborted
  (*umsesh)->WhenClosed().Then([](auto f) {
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
}

void generateBaseEnvtSnapshot(){

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

  // Uses global snap_sv
  regSnapshot(&snap_sv, &snap_f);

  umm::manager->Run(std::move(umi));
}

void generateBaseEnvtSnapshotOpt(){
  // Set base snap here.
  generateBaseEnvtSnapshot();

  // Deploy from orig snap.
  auto umi = getUMIFromSV( *snap_sv );

  // Optimized snap
  ebbrt::Future<umm::UmSV *> opt_base_f =
    umi->SetCheckpoint(umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // using global opt_base_sv pointer.
  regSnapshot(&opt_base_sv, &opt_base_f);

  auto umsesh = create_session();
  regConnect(&umsesh);


  umsesh->WhenConnected().Then([umsesh](auto f) {
                                 // ebbrt::kprintf_force(CYAN "Connected, sending nopInit\n" RESET);
                                 bool keepAlive = true;
                                 // bool keepAlive = false;
                                 umsesh->SendHttpRequest(
                                                         "/preInit",
                                                         // "/init",
                                                         getDummyCode(),
                                                         keepAlive /* keep_alive */);
                               });

  umsesh->WhenInitialized().Then([umsesh](auto f) {
                                   // ebbrt::kprintf_force(CYAN "Initted sending nopRun\n" RESET);
                                   umsesh->SendHttpRequest(
                                                           "/preRun",
                                                           // "/run",
                                                           getArgs(), false);
                                 });

  umm::manager->Run(std::move(umi));
}

void generateWarmSnapshot(bool optimize) {
  /* Load up the base snapshot environment */
  // ebbrt::kprintf_force(YELLOW "Loading base env from snap\n" RESET);
  // auto umi;

  umm::UmSV *snap;
  {
    if (optimize) {
      generateBaseEnvtSnapshotOpt();
      snap = opt_base_sv;
    } else {
      generateBaseEnvtSnapshot();
      snap = snap_sv;
    }
  }
  auto umi = getUMIFromSV( *snap );


  ebbrt::Future<umm::UmSV *> warm_sv_f =
    umi->SetCheckpoint(umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Uses global warm_sv.
  regSnapshot(&warm_sv, &warm_sv_f);

  auto umsesh = create_session();

  regConnect(&umsesh);

  bool optimizeStart = false;
  regSendInitOnConnect(&umsesh, !optimizeStart);

  regHaltOnClose(&umsesh);

  /* Boot the snapshot */

  // ebbrt::kprintf_force(YELLOW "About to run umi\n" RESET);
  umi = std::move(umm::manager->Run(std::move(umi)));
}

auto initInstance(){
  // Create sv.
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);

  // Create instance.
  auto umi = std::make_unique<umm::UmInstance>(sv);

  // Configure solo5 boot arguments
  // Nop without listener.
  // const std::string the_cmd = R"({"cmdline":"bin/node-default /nodejsActionBase/nop.js"})";

  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE, my_cmd);
  umi->SetArguments(argc);

  return umi;
}

void deployFullBoot(){
  // This is for timing the listener bringup
  auto start = high_resolution_clock::now();
  auto umi = initInstance();
  umi = std::move(umm::manager->Run(std::move(umi)));
  auto end = high_resolution_clock::now();

  umi->pfc.dump_ctrs();
  // umm::manager->ctr.dump_list(umm::manager->ctr_list);
  auto run_duration = duration_cast<microseconds>(end - start);
  u_sec_list.push_back(run_duration.count());

  ebbrt::kprintf_force(YELLOW "Done cold start.\n" RESET);
}

void deployColdStart(bool useOpt){
  /* Load up the base snapshot environment */
  // ebbrt::kprintf_force(MAGENTA "Deploying Cold Start\n" RESET);
  umm::UmSV *snap;
  {
    if(useOpt){
      snap = opt_base_sv;
    }else{
      snap = snap_sv;
    }
  }
  auto umi = getUMIFromSV(*snap);

  auto umsesh = create_session();

  /* Spawn a new event to make a connection with the instance */
  regConnect(&umsesh);

  /* Setup the asyncronous operations on the InvocationSession */
  bool keepAlive = true;
  regSendInitOnConnect(&umsesh, keepAlive);

  regSendRunOnInit(&umsesh);

  // Halt when closed or aborted
  regHaltOnClose(&umsesh);

  /* Boot the snapshot */
  // ebbrt::kprintf_force(YELLOW "About to run umi\n" RESET);
  // umm::manager->ctr_list.clear();

  // umm::manager->Run(std::move(umi));
  auto start_run = high_resolution_clock::now();
  umi = std::move(umm::manager->Run(std::move(umi)));
  auto end_run = high_resolution_clock::now();

  umi->pfc.dump_ctrs();
  // umm::manager->ctr.dump_list(umm::manager->ctr_list);

  auto run_duration = duration_cast<microseconds>(end_run - start_run);
  // ebbrt::kprintf_force("%d us\n", run_duration.count());
  u_sec_list.push_back(run_duration.count());
}

void deployWarmSnapshot(){
  // void deployHotSnapshotSV(){
  // ebbrt::kprintf_force(CYAN "Deploy hot snap sv \n" RESET);

  auto umi2 = getUMIFromSV( *warm_sv );

  auto umsesh = create_session();

  regConnect(&umsesh);

  // umsesh->WhenConnected().Then(
  //     [umsesh](auto f) { umsesh->SendHttpRequest("/run", getArgs(), false); });
  regSendRunOnConnect(&umsesh);
  // Halt when closed or aborted

  regHaltOnClose(&umsesh);

  umi2->ZeroPFCs();
  umm::manager->ctr_list.clear();

  auto start_run = high_resolution_clock::now();
  umi2 = std::move(umm::manager->Run(std::move(umi2)));
  auto end_run = high_resolution_clock::now();

  // umi2->pfc.dump_ctrs();
  // umm::manager->ctr.dump_list(umm::manager->ctr_list);

  // ebbrt::kprintf_force(YELLOW "Run finished.\n" RESET);
  auto run_duration = duration_cast<microseconds>(end_run - start_run);
  // ebbrt::kprintf_force("zztu%d \n", run_duration.count());
  u_sec_list.push_back(run_duration.count());
}

void deployHotHelper(){
  // umm::count::Counter::TimeRecord *spicy_record = umm::manager->ctr.CreateTimeRecord(std::string("Don't print"));
  ebbrt::Promise<void> done_spicy;
  auto spicy_f = done_spicy.GetFuture();

  auto spicy_record = new umm::count::Counter::TimeRecord();
  auto spicy_start = new high_resolution_clock::time_point();
  auto spicy_end = new high_resolution_clock::time_point();

    // Start a new TCP connection for Spicy start.
    auto umsesh2 = create_session();

    // When connected for spicy, send run.
    umsesh2->WhenConnected().Then([umsesh2, spicy_record](auto f) {

      auto this_umi = umm::manager->ActiveInstance();
      kassert( this_umi != nullptr );

      this_umi->ZeroPFCs();
      umm::manager->ctr_list.clear(); // Clear placeholder.
      *spicy_record = umm::manager->ctr.CreateTimeRecord(std::string("Spicy"));

      umsesh2->SendHttpRequest("/run", getArgs(), false);
    });

    // When spicy finished, halt.
    umsesh2->WhenClosed().Then([&done_spicy, spicy_end, spicy_start](auto f) {
      // Set value
      *spicy_end = high_resolution_clock::now();
      auto run_duration =
          duration_cast<microseconds>(*spicy_end - *spicy_start);
      u_sec_list.push_back(run_duration.count());
      // auto this_umi = umm::manager->ActiveInstance();
      // ebbrt::kprintf_force(CYAN "%d us\n" RESET, run_duration.count());
      // this_umi->pfc.dump_ctrs();
      done_spicy.SetValue();
    });

    // Connect!
    {
      uint16_t port = get_internal_port();

      *spicy_start = high_resolution_clock::now();
      umsesh2->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
    }
    spicy_f.Block();
}

void deployHotSnapshot(){
  ebbrt::kprintf_force(GREEN "Hot start then Spicy\n" RESET);
  // Run a hot start, then a spicy hot start.

  auto umi2 = getUMIFromSV( *warm_sv );

  // Hot start session.
  auto umsesh = create_session();

  regConnect(&umsesh);

  regSendRunOnConnect(&umsesh);
  // umsesh->WhenConnected().Then([umsesh](auto f) {
  //   // ebbrt::kprintf_force(YELLOW "Connected, sending run\n" RESET);
  //   umsesh->SendHttpRequest("/run", getArgs(), false);
  // });

  // When the hot start connection is closed, launch Spicy.
  umsesh->WhenClosed().Then([](auto f) {
    ebbrt::kprintf_force(MAGENTA "Hot closed, deploy spicy.\n" RESET);
    int numRuns = 475;
    for (int i = 0; i < numRuns; i++) {
      deployHotHelper();

      if (i == numRuns - 1) {
        int ctr = 0;
        ebbrt::kprintf_force("Count u_sec\n");
        for (const auto &val : u_sec_list) {
          ebbrt::kprintf_force("latency %d\t%d\n", ctr++, val);
        }
        ebbrt::acpi::PowerOff();
      }
    }

  });

  // Run spicy hot start.
  umi2 = std::move(umm::manager->Run(std::move(umi2)));
  // umi2->pfc.dump_ctrs();

  ebbrt::kprintf_force(YELLOW "Run finished.\n" RESET);
}

void timeFullBoot(){
  deployFullBoot();
  int ctr = 0;
  ebbrt::kprintf_force("Count u_sec\n");
  for (const auto &val : u_sec_list) {
    ebbrt::kprintf_force("latency %d\t%d\n", ctr++, val);
  }
}

void coldTest(){
  ebbrt::kprintf_force(CYAN "Running Cold Starts!\n" RESET);

  bool useOpt = true;
  if(useOpt){
    generateBaseEnvtSnapshotOpt();
  }else{
    generateBaseEnvtSnapshot();
  }

  int numRuns = 1;
  for (int i = 0; i < numRuns; i++) {
    deployColdStart(useOpt);
    // ebbrt::kprintf_force(MAGENTA "Next cold start\n" RESET);
    // deployColdStartOpt();
  }

  int ctr = 0;
  ebbrt::kprintf_force("Count u_sec\n");
  for (const auto &val : u_sec_list) {
    ebbrt::kprintf_force("latency %d\t%d\n", ctr++, val);
  }
}

void warmTest(){
  ebbrt::kprintf_force(YELLOW "Running Warm Starts!\n" RESET);

  // This buries complexity in base envt.
  bool useOpt = true;
  generateWarmSnapshot(useOpt);

  // This bloats warm snap.
  // Have to modify js code to use this one.
  // This is the worse way to optimize latency.
  // generateWarmSnapshotOptimized();


  // Deploy.
  int numRuns = 50;
  for (int i = 0; i < numRuns; i++) {
      deployWarmSnapshot();
  }

  int ctr = 0;
  ebbrt::kprintf_force("Count u_sec\n");
  for (const auto &val : u_sec_list) {
    ebbrt::kprintf_force("latency %d\t%d\n", ctr++, val);
  }

}

void hotTest(){
  ebbrt::kprintf_force(RED "Running Hot Starts!\n" RESET);
  // Not sure which to use.
  generateWarmSnapshot(true);
  deployHotSnapshot();

}

void AppMain() {
  umm::UmManager::Init();
  // timeFullBoot();
  coldTest();
  // warmTest();
  // hotTest();

  ebbrt::kprintf_force(CYAN "Done!\n" RESET);
  ebbrt::acpi::PowerOff();
}
