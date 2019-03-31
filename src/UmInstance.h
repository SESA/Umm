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

  /* Block for (at least) `ns` nanoseconds. Inactive instance will be
   * unloaded. Execution will be yielded. */
  void Sleep(size_t ns);

  /* Preemption Management */

  /* An UM instance is either Active or Inactive, effecting its preemption
behaviour within the execution slot.

  Active:
       - If loaded, an active instance can not be unloaded
       - If not loaded, an active instance can be loaded in
       - When blocking, instance remains loaded and a timer is set
  Inactive:
       - If loaded, inactive instance can be unloaded
       - If not loaded, an inactive will not be swapped in
       - When blocking, instance will be unloaded
  */

  void Kick();

  /* Mark instance as active, if not already */
  void SetActive(); 

  /* Mark instance as inactive, if not already */
  void SetInactive(); 

  bool IsActive() { return active_; };
  bool IsInactive() { return !active_; };

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
  /* Status flags */
  bool active_ = true; // UMI is either Active or Inactive
  bool blocked_ = false; // UMI (active or inactive) can be Blocked/Unblocked

  /* Execution Management - these control the underlying event context */
  void block_execution();
  void unblock_execution();

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
  ebbrt::EventManager::EventContext *context_; // blocking context
  umi::id id_ = ebbrt::ebb_allocator->AllocateLocal();
  std::queue<std::unique_ptr<ebbrt::IOBuf>> umi_recv_queue_;
}; // end umm::UmInstance
}

#endif // UMM_UM_INSTANCE_H_
