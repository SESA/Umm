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
class UmInstance  {
public:
  /** Page Fault counters */
  struct PgFtCtrs {
    void dump_ctrs();
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
  /** Resolve phyical page for virtual address */
  uintptr_t GetBackingPage(uintptr_t vaddr, bool cow);
  /** Log PageFault to internal counter */
  void logFault(x86_64::PgFaultErrorCode ec);

  // TODO(jmcadden): Move this interface into the UmSV
  void SetArguments(const uint64_t argc, const char *argv[] = nullptr);

  /** Trigger SV creation at the elf symbol located at vaddr */
  ebbrt::Future<UmSV*> SetCheckpoint(uintptr_t vaddr);

  /* Stack Management */
  void Activate();
  void Deactivate();

  /* Premption Management */
  void EnableYield(); 
  void DisableYield(); 
  bool CanYield() { return yield_flag_; }

  /* IO */
  std::unique_ptr<ebbrt::IOBuf> ReadPacket();
  void WritePacket(std::unique_ptr<ebbrt::IOBuf>);
  bool HasData() { return (!umi_recv_queue_.empty()); };

  /* Dump state of the Instance */
  void Print();
  umi::id Id(){ return id_; }

  // generic boot info structure
  void* bi; // FIXME(jmcadden): lil memory leak
  UmSV sv_;
  PgFtCtrs pfc; // Page fault counters
  ExceptionFrame caller_restore_frame_; 
  uintptr_t fnStack;
  /** Snapshot */
  uintptr_t snap_addr = 0; // TODO: Multiple snap locations
  ebbrt::Promise<UmSV *> *snap_p;

private:
  std::queue<std::unique_ptr<ebbrt::IOBuf>> umi_recv_queue_;
  bool active_ = true;
  ebbrt::EventManager::EventContext *context_; // blocking context
  umi::id id_ = ebbrt::ebb_allocator->AllocateLocal();
  size_t page_count = 0; // TODO: replace with region-specific counter
  uint64_t runtime_ = 0;
  bool yield_flag_ = false; // signal that the instance can be yielded 
  // TODO: Computing runtime duration within the instance
  // runtime_ += std::chrono::duration_cast<std::chrono::milliseconds>(now -
  // clock_).count();
}; // umm::UmInstance
}

#endif // UMM_UM_INSTANCE_H_
