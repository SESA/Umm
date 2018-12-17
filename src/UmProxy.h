//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_PROXY_H_
#define UMM_UM_PROXY_H_

#include <queue>

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

namespace {
typedef boost::bimap<uint16_t, uint16_t> port_map_t; /* <internal, nat port>*/
}

class ProxyRoot {
public:
  typedef std::pair<size_t, uint64_t> umi_location; // <core, umiID>
  explicit ProxyRoot(const LoopbackDriver &root)
      : lo(const_cast<LoopbackDriver &>(root)) {
    port_set_ += boost::icl::interval<uint16_t>::type(256, 49151);
  }
  umm::umi::exec_location GetLocationFromPort(uint16_t);
  /* Allocate and register a new port for this Um Instance */
  uint16_t RegisterPortMask(umi::id);

private:
  uint16_t allocate_port();
  void free_port(uint16_t);
  typedef uint16_t iport, nport; /* internal src port, natted src ports */
  typedef boost::bimap<nport, umm::umi::exec_location> nat_port_map_t;
  typedef nat_port_map_t::left_map::const_iterator nat_port_map_left_iterator_t;
  typedef nat_port_map_t::right_map::const_iterator nat_port_map_right_iterator_t;
  LoopbackDriver &lo;
  ebbrt::SpinLock lock_;
  /** NAT state */
  boost::icl::interval_set<nport> port_set_; /* set of allocatable ports */
  nat_port_map_t nport_to_umi_map_; /* map of allocated ports */
  port_map_t umi_port_map_; /* master port map cache */

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
  static std::unique_ptr<ebbrt::MutIOBuf> raw_to_iobuf(const void *data,
                                                    const size_t len);

  static ebbrt::EthernetAddress client_internal_macaddr(){
    return {{0x06, 0xfe, 0x00, 0x00, 0x00, 0x00}};
  };

  static ebbrt::Ipv4Address client_internal_ipv4() {
    return {{169, 254, 1, 0}};
  };

  static ebbrt::Ipv4Address host_internal_ipv4() { return {{169, 254, 0, 1}}; }

  ebbrt::EthernetAddress host_internal_macaddr(){
    return root_.lo.GetMacAddress();
  }

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

  /** UmRead - Incoming data read to UM instance 
   *  returns the amount of data read
   */
  uint32_t UmRead(void *data, const size_t len);

  /**	UmHasData - Returns 'true' if there is data to be read */
  bool UmHasData() { return !um_recv_queue_.empty(); }

  /** LoadUmi - Clears transient state and sets a "loaded" umi_id */ 
  void LoadUmi(umm::umi::id id) {
    // TODO(jmcadden): Here we start considering unloading a running umi
    clear_proxy_state();
    umi_id_ = id;
  };

private:
  /* Translate nat port to internal src port */
  uint16_t swizzle_port_in(uint16_t);
  /* Translate internal src port to nat port */
  uint16_t swizzle_port_out(uint16_t);
  ProxyRoot::umi_location nat_get_info(std::unique_ptr<ebbrt::MutIOBuf> &);
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
  void clear_proxy_state() {
    um_recv_queue_ = std::queue<std::unique_ptr<ebbrt::IOBuf>>();
    umi_port_map_cache_.clear();
    umi_id_ = 0;
  }
  umm::umi::id umi_id_;
  std::queue<std::unique_ptr<ebbrt::IOBuf>> um_recv_queue_;
  ProxyRoot &root_;
  port_map_t umi_port_map_cache_; /* core-local port map cache */
};

constexpr auto proxy = ebbrt::EbbRef<UmProxy>(UmProxy::global_id);

}
#endif // UMM_UM_PROXY_H_
