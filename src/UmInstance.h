//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_INSTANCE_H_
#define UMM_UM_INSTANCE_H_

#include "umm-common.h"
#include "util/x86_64.h"

#include "UmSV.h"

#include <unordered_map>

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
class UmInstance {
public:
  /** Page Fault counters */
  struct PgFtCtrs {
    void zero_ctrs();
    void dump_ctrs();
    uint64_t pgFaults = 0;
    uint64_t rdFaults = 0;
    uint64_t wrFaults = 0;
    uint64_t cowFaults = 0;
  };
  // IP/MAC are provided here (and not in UmProxy) to allow applications access
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
  uintptr_t GetBackingPage(uintptr_t vaddr);
  uintptr_t GetBackingPageCOW(uintptr_t vaddr);
  /** Log PageFault to internal counter */
  void logFault(x86_64::PgFaultErrorCode ec);

  // TODO(jmcadden): Move this interface into the UmSV
  void SetArguments(const uint64_t argc, const char *argv[] = nullptr);

  /* Dump state of the instance*/
  void Print();

  // TODO: Add runtime duration into the instance
  size_t page_count = 0; // TODO: replace with region-specific counter
  umi::id Id(){ return id_; }

  // generic boot info structure
  void* bi; // FIXME(jmcadden): lil memory leak
  UmSV sv_;
  PgFtCtrs pfc; // Page fault counters

private:
  umi::id id_ = ebbrt::ebb_allocator->AllocateLocal(); 

}; // umm::UmInstance
}

#endif // UMM_UM_INSTANCE_H_
