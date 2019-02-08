//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_INSTANCE_H_
#define UMM_UM_INSTANCE_H_

#include <ebbrt/Timer.h>

#include "Counter.h"

#include "umm-common.h"
#include "util/x86_64.h"

#include "UmSV.h"

namespace umm {

namespace umi {

// umi::core
typedef size_t core;
// umi::id
typedef uint32_t id;
// umi::exec_location
typedef std::pair<umi::id, umi::core> exec_location; // e.g., (ID, core)

const id null_id = 0;
}

/**
 * UmInstance - Executable unikernel monitor (Um) instance
 *
 * An UmInstance is the native object that is executable by the Manager.
 *
 * future, the UmInstance maybe typed with a particular instance-type (for
 * example, A Solo5-instances which conforms to the solo5 boot parameters). In
 * this way, the UmSV can remain target-agnonstic, only presented the 'raw'
 * state of the process.
 *
 */
class UmInstance : public ebbrt::Timer::Hook {
public:
  /** Page Fault counters */
  struct PgFtCtrs {
    void dump_ctrs();
    void zero_ctrs();
    uint64_t pgFaults = 0;
    uint64_t rdFaults = 0;
    uint64_t wrFaults = 0;
    uint64_t cowFaults = 0;
  };

  // IP/MAC are provided here (and not in UmProxy) to allow apps access to them
  static ebbrt::Ipv4Address CoreLocalIp() {
    size_t core = ebbrt::Cpu::GetMine();
    return {{169, 254, 254, (uint8_t)core}};
  };

  static ebbrt::EthernetAddress CoreLocalMac() {
    size_t core = ebbrt::Cpu::GetMine();
    return {{0x06, 0xfe, 0x01, 0x02, 0x03, (uint8_t)core}};
  };

  UmInstance() = delete;
  // Using a reference so we don't make a redundant copy.
  // This is where the argument page table is copied.
  explicit UmInstance(const UmSV &sv) : sv_(sv){};
  ~UmInstance(){ disable_timer(); }
  /** Timer event handler */
  void Fire() override;
  /** Resolve phyical page for virtual address */
  uintptr_t GetBackingPage(uintptr_t vaddr, x86_64::PgFaultErrorCode ec);
  /** Log PageFault to internal counter */
  void logFault(x86_64::PgFaultErrorCode ec);

  // TODO(jmcadden): Move this interface into the UmSV
  void SetArguments(const uint64_t argc, const char *argv[] = nullptr);

  /** Trigger SV creation at the elf symbol located at vaddr */
  ebbrt::Future<UmSV*> SetCheckpoint(uintptr_t vaddr);

  /* Stack Management */
  void Activate();
  void Deactivate();
  void Block(size_t ns);

  /* Preemption Management */

  /* Return true if this instance can be preempted, false otherwise 
    A preemptable instance... 
      1. If active, will be yielded on next request
      2. If idle, will not be scheduled back in
  */
  bool Yieldable(); 

  /* Make this instance yieldable */
  void EnableYield(); 

  /* Make this instance non-yieldable */
  void DisableYield(); 

  /* IO Management */
  std::unique_ptr<ebbrt::IOBuf> ReadPacket();
  void WritePacket(std::unique_ptr<ebbrt::IOBuf>);
  bool HasData() { return (!umi_recv_queue_.empty()); };

  /** Register instance as destination for internal src port */
  void RegisterPort(uint16_t port); 

  /* Dump state of the Instance */
  void ZeroPFCs();
  void Print();
  umi::id Id(){ return id_; }

  // generic boot info structure
  void* bi; // FIXME(jmcadden): lil memory leak
  UmSV sv_;
  PgFtCtrs pfc; // Page fault counters
  ExceptionFrame caller_restore_frame_; 
  uintptr_t fnStack;
  /* IO state */
  std::vector<uint16_t> src_ports_;
  /** Snapshot */
  uintptr_t snap_addr = 0; // TODO: Multiple snap locations
  ebbrt::Promise<UmSV *> *snap_p;

private:
  /** Timing */
  void enable_timer(ebbrt::clock::Wall::time_point now);
  void disable_timer(); 
  bool timer_set = false;
  ebbrt::clock::Wall::time_point clock_;
  ebbrt::clock::Wall::time_point time_wait; // block until this time
  // TODO: Computing runtime duration within the instance
  // runtime_ += std::chrono::duration_cast<std::chrono::milliseconds>(now -
  // clock_).count();

  /* Internal state */
  std::queue<std::unique_ptr<ebbrt::IOBuf>> umi_recv_queue_;
  bool active_ = true;
  ebbrt::EventManager::EventContext *context_; // blocking context
  umi::id id_ = ebbrt::ebb_allocator->AllocateLocal();
  uint64_t runtime_ = 0;
  bool yield_flag_ = false;  // shows the instance can be or is yielded
  bool resume_flag_ = false; // shows that the instance has requested to be resumed
}; // umm::UmInstance
}

#endif // UMM_UM_INSTANCE_H_
