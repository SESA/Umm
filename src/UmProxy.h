//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_PROXY_H_
#define UMM_UM_PROXY_H_

#include <queue>
#include <cstdint>

#include <ebbrt/Cpu.h>
#include <ebbrt/EbbId.h>
#include <ebbrt/GlobalStaticIds.h>
#include <ebbrt/MulticoreEbb.h>
#include <ebbrt/UniqueIOBuf.h>
#include <ebbrt/native/NetChecksum.h>
#include <ebbrt/native/NetTcpHandler.h>
#include <ebbrt/native/NetUdp.h>

#include <boost/bimap.hpp>
#include <boost/icl/interval.hpp>
#include <boost/icl/interval_set.hpp>

#include "LoopbackDriver.h"
#include "UmInstance.h"
#include "umm-common.h"

namespace umm {

typedef uint16_t external_port_t;
typedef std::tuple<umi::id, umi::core, uint16_t> internal_port_t;

namespace {
typedef boost::icl::interval_set<external_port_t> port_set_t;
typedef boost::bimap<external_port_t, internal_port_t> port_map_t;
typedef port_map_t::left_map::const_iterator port_map_left_iterator_t;
typedef port_map_t::right_map::const_iterator port_map_right_iterator_t;
typedef std::unordered_multimap<umi::id, external_port_t> port_owner_map_t;
typedef port_owner_map_t::iterator port_owner_map_iterator_t;

const internal_port_t null_port_mapping_(0, 0, 0);
const uint16_t port_min = 256;
const uint16_t port_max = 32000;
}

class ProxyRoot {
public:
  typedef std::pair<size_t, uint64_t> umi_location; // <core, umiID>
  explicit ProxyRoot(const LoopbackDriver &root)
      : lo(const_cast<LoopbackDriver &>(root)) {
    port_set_ += boost::icl::interval<uint16_t>::type(port_min, port_max);
  }

  /* Allocate and register a new port for this Um Instance */
  uint16_t SetupExternalPortMapping(umi::id, uint16_t);
  void SetupInternalPortMapping(umi::id, uint16_t);
  internal_port_t ExternalPortLookup(external_port_t);
  umi::id InternalPortLookup(uint16_t);
	/* Free the associated ports of umi */
	void FreePorts(umi::id);

private:
  uint16_t allocate_port();
  void free_port(uint16_t);
  LoopbackDriver &lo;
  ebbrt::SpinLock port_lock_;
  ebbrt::SpinLock host_map_lock_;

  /** NAT state */
  port_set_t port_set_; /* set of allocatable ports */
  port_map_t master_port_map_; /* map of allocated ports */
  port_owner_map_t port_owner_map_; /* map of allocated ports */
  
  std::unordered_map<uint16_t, umi::id> host_src_port_map_;

  friend class UmProxy;
};

/**
 *  UmProxy - Ebb that manages per-core network IO of SV instances
 */
class UmProxy : public ebbrt::MulticoreEbb<UmProxy, ProxyRoot> {
public:
  /** Class-wide UmProxy state & initialization */
  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("UmProxy");
  static void Init();
  static void DebugPrint(ebbrt::IOBuf::DataPointer dp);
	// TODO: make private?
  static std::unique_ptr<ebbrt::MutIOBuf> raw_to_iobuf(const void *data,
                                                    const size_t len);
  static internal_port_t internal_port(uint16_t port, umi::id id, 
                                size_t core = (size_t)ebbrt::Cpu::GetMine()) {
    return std::make_tuple(id, core, port);
  }

	// TODO: make private?
  static ebbrt::EthernetAddress client_internal_macaddr(){
    return {{0x06, 0xfe, 0x00, 0x00, 0x00, 0x00}};
  };

	// TODO: make private?
  static ebbrt::Ipv4Address client_internal_ipv4() {
    return {{169, 254, 1, 0}};
  };

	// TODO: make private?
  static ebbrt::Ipv4Address host_internal_ipv4() { return {{169, 254, 0, 1}}; }

	// TODO: make private?
  ebbrt::EthernetAddress host_internal_macaddr(){
    return root_.lo.GetMacAddress();
  }

	// TODO: make private?
  ebbrt::Ipv4Address host_external_ipv4() {
    return ebbrt::network_manager->IpAddress();
  }

  explicit UmProxy(const ProxyRoot &root) : root_(const_cast<ProxyRoot&>(root)) {}

  /** ProcessOutgoing
   *  Process outgoing packet for an UMI source
   */
  void ProcessOutgoing(std::unique_ptr<ebbrt::MutIOBuf> buf);

  /** ProcessIncoming
   *  Process incoming packer for an UMI destination
   */
  void ProcessIncoming(std::unique_ptr<ebbrt::IOBuf>, ebbrt::PacketInfo pinfo);

  /** InstanceRead - Read packet (Called by instance)
   *  returns the amount of data read
   */
  uint32_t InstanceRead(void *data, const size_t len);

  /** InstanceHasData - Return true if there is data 
   */
  bool InstanceHasData();

  /** SetActiveInstance - Clears transient state and sets a "loaded" umi_id */ 
  void SetActiveInstance(umm::umi::id id);

  /** RemoveInstanceState - Clears all proxy state for a given instance */
  void RemoveInstanceState(umm::umi::id id); 
  

private:
  /* Translate nat port to internal src port */
  uint16_t swizzle_port_in(uint16_t);
  /* Translate internal src port to nat port */
  uint16_t swizzle_port_out(uint16_t);
  //ProxyRoot::umi_location nat_get_info(std::unique_ptr<ebbrt::MutIOBuf> &);

  bool nat_preprocess_in(std::unique_ptr<ebbrt::MutIOBuf> &);
  bool nat_preprocess_out(std::unique_ptr<ebbrt::MutIOBuf> &);
  /* Overwrite the destination IP and MAC with their internal values, apply port swizzle for external */
  bool nat_masquerade_in(std::unique_ptr<ebbrt::MutIOBuf> &);
  bool nat_masquerade_out(std::unique_ptr<ebbrt::MutIOBuf> &);
  bool nat_postprocess_in(std::unique_ptr<ebbrt::MutIOBuf> &);
  bool nat_postprocess_out(std::unique_ptr<ebbrt::MutIOBuf> &);
  void nat_connection_reset(std::unique_ptr<ebbrt::MutIOBuf>& buf);
  /* Return true if destination is (host) internal */
  bool internal_destination(std::unique_ptr<ebbrt::MutIOBuf> &);
  /* Return true if source is (host) internal */
  bool internal_source(std::unique_ptr<ebbrt::MutIOBuf> &);
  /* Return internal port identifier for this core/umi */

  /* Instance IO state */
  umm::umi::id umi_id_;
  //std::unordered_map<umm::umi::id, std::queue<std::unique_ptr<ebbrt::IOBuf>>>
  //  umi_recv_queue_;

  ProxyRoot &root_;
  port_map_t port_map_cache_; /* core-local port map cache */
};

constexpr auto proxy = ebbrt::EbbRef<UmProxy>(UmProxy::global_id);

}
#endif // UMM_UM_PROXY_H_
