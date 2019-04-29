//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <cstdint>
#include <string.h> // memcpy

#include "UmProxy.h"
#include "UmInstance.h"
#include "UmManager.h"
#include "umm-internal.h"

// TOGGLE DEBUG PRINT  
#define DEBUG_PRINT_IO  0
// protocol-specific toggles 
#define DEBUG_PRINT_ETH 0
#define DEBUG_PRINT_ARP 0
#define DEBUG_PRINT_IP  0
#define DEBUG_PRINT_TCP 0
#define DEBUG_PRINT_UDP 0

void umm::UmProxy::Init() {
  // Setup Ebb translations
  auto lo_dev = new LoopbackDriver();
  auto proxy_root = new ProxyRoot(*lo_dev);
  Create(proxy_root, UmProxy::global_id);
}

umm::internal_port_t umm::ProxyRoot::ExternalPortLookup(external_port_t nport){
  std::lock_guard<ebbrt::SpinLock> guard(nat_map_lock_);
  //kprintf_force(CYAN "C%d:EP=%u " RESET, (size_t)ebbrt::Cpu::GetMine(), nport);
  port_map_left_iterator_t it = master_port_map_.left.find(nport);
  if (it != master_port_map_.left.end()) {
    // Hit
    return it->second;
  } else {
    // Miss
    return null_port_mapping_;
  }
}

uint16_t umm::ProxyRoot::SetupExternalPortMapping(umi::id id, uint16_t port) {
  std::lock_guard<ebbrt::SpinLock> guard(nat_map_lock_);
  auto iport = umm::UmProxy::internal_port(port, id);
  port_map_right_iterator_t it = master_port_map_.right.find(iport);
  if (it != master_port_map_.right.end()) {
    return it->second;
  }
  auto nport = allocate_port();
  master_port_map_.insert(port_map_t::value_type(nport, iport));
#if DEBUG_PRINT_IO
  kprintf(CYAN "C%dU%d:NAT_XPORT=%u " RESET, std::get<1>(iport), std::get<0>(iport), nport);
#endif
  return nport;
}

void umm::ProxyRoot::FreePorts(umi::id target_umi){
  return;
}


uint16_t umm::ProxyRoot::allocate_port() {
  std::lock_guard<ebbrt::SpinLock> guard(port_lock_);
  if (unlikely(port_set_.empty())) {
   // kabort("ProxyRoot: ERROR no NAT ports remaining! (because we never free any! lol.) \n");
    kprintf_force("ProxyRoot: ERROR no NAT ports remaining! (because we never free any! lol.) \n");
		return 0;
  }
  uint16_t ret = boost::icl::first(port_set_);
  port_set_.subtract(ret);
  return ret;
}

void umm::ProxyRoot::free_port(uint16_t val) {
  std::lock_guard<ebbrt::SpinLock> guard(port_lock_);
  master_port_map_.left.erase(val);
  // XXX: (_HACK_) I've disabled freeing ports because it was causing the external
  // HTTP server to reject new connections if the same port was used in quick
  // succession. By not freeing used ports we guarantee a unique port each time

  /* port_set_ += val; // Free the port */
}

void umm::UmProxy::RegisterInternalPort(umi::id id, uint16_t src_port) {
  auto it = host_src_port_map_cache_.find(src_port);
  if(it != host_src_port_map_cache_.end()){
    if( id != it->second ){
      kprintf_force(RED "ProxyRoot: ERROR conflict on mapping internal src_port=%u for "
                        "UMI #%d, conflict with UMI #%d \n", src_port, id, it->second);
			return;
      //kabort();
    }
  }
#if DEBUG_PRINT_IO
  kprintf_force(CYAN "C%dU%d:NAT_IPORT=%u " RESET, (size_t)ebbrt::Cpu::GetMine(), id, src_port);
#endif
   host_src_port_map_cache_.emplace(src_port, id);
   return;
}

umm::umi::id umm::UmProxy::internal_port_lookup(uint16_t host_src_port){
  /* check core-local cache */
  auto it = host_src_port_map_cache_.find(host_src_port);
  if (it == host_src_port_map_cache_.end()) {
    kprintf(YELLOW
                  "ProxyRoot: WARNING no mapping found for host src port %u\n",
                  host_src_port);
    return umi::null_id; 
  }
  return it->second;
  //host_src_port_map_cache_.emplace(host_src_port, umi_id);
  //return umi_id;
}

umm::internal_port_t umm::UmProxy::external_portmap_lookup(external_port_t nport) {
  /* check core-local cache */
  auto it = port_map_cache_.left.find(nport);
  if (it != port_map_cache_.left.end()){
    // Cache Hit
    return it->second;
  } else {
    // Cache Miss, get the mapping from the root
    auto mapping = root_.ExternalPortLookup(nport);
    kassert(mapping != null_port_mapping_);
    //if(umi_id_ == std::get<0>(mapping)){
    port_map_cache_.insert(port_map_t::value_type(nport, mapping));
    //}
    return mapping;
  }
}

bool umm::UmProxy::internal_source(std::unique_ptr<ebbrt::MutIOBuf>& buf){
  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  auto dp = buf->GetMutDataPointer();
  auto& eh = dp.Get<ebbrt::EthernetHeader>();
  ebbrt::EthernetAddress src_eth = {
      {eh.src[0], eh.src[1], eh.src[2], eh.src[3], eh.src[4], eh.src[5]}};
  if( src_eth == host_internal_macaddr()){
    return true;
  }
  return false;
}

bool umm::UmProxy::internal_destination(std::unique_ptr<ebbrt::MutIOBuf>& buf){
  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  auto dp = buf->GetMutDataPointer();
  auto& eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);
  if (ethtype == ebbrt::kEthTypeArp) {
    // All ARP requests are handled internally
    return true;
  }
  kassert(ethtype == ebbrt::kEthTypeIp);
  auto iph = dp.Get<ebbrt::Ipv4Header>();
  auto dst = iph.dst.toArray();
  auto iip = host_internal_ipv4().toArray();
  if (dst != iip) {
    return false;
  }
  return true;
}

bool umm::UmProxy::nat_preprocess_in(std::unique_ptr<ebbrt::MutIOBuf>& buf){
  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  auto dp = buf->GetMutDataPointer();
  auto &eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);

  // Don't overwrite the destination mac of ARP messages
  if (ethtype == ebbrt::kEthTypeArp) {
    auto &arp = dp.Get<ebbrt::ArpPacket>();
    auto oper = ebbrt::ntohs(arp.oper);
    if (oper == 0x1) { // ARP REQUEST lo-umi
      arp.tpa = client_internal_ipv4();
    } else if (oper == 0x2) { // ARP REPLY lo->umi
      arp.tpa = client_internal_ipv4();
      arp.tha = client_internal_macaddr();
      eth.dst = client_internal_macaddr();
    }
  } else if (ethtype == ebbrt::kEthTypeIp) {
    eth.dst = client_internal_macaddr();
    auto &ip = dp.Get<ebbrt::Ipv4Header>();
    ip.dst = client_internal_ipv4();
    ip.chksum = 0;
    ip.chksum = ip.ComputeChecksum();
  }
  return true;
}


bool umm::UmProxy::nat_postprocess_in(std::unique_ptr<ebbrt::MutIOBuf>& buf){
  kassert(buf->Length() >= sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
  return true;
}


bool umm::UmProxy::nat_masquerade_in(std::unique_ptr<ebbrt::MutIOBuf> &buf) {
  kassert(buf->Length() >= sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
  auto dp = buf->GetMutDataPointer();
  auto &eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);
  kassert(ethtype == ebbrt::kEthTypeIp);
  auto &ip = dp.Get<ebbrt::Ipv4Header>();
  kassert(ip.proto == 0x6);
  auto &tcp = dp.Get<ebbrt::TcpHeader>();
  auto nport = ebbrt::ntohs(tcp.dst_port);
  // Set the correct destination port
  tcp.dst_port = ebbrt::htons(swizzle_port_in(nport));
  return true;
}


// Reset an external connection
void umm::UmProxy::nat_connection_reset(std::unique_ptr<ebbrt::MutIOBuf> &buf) {
  if (buf->Length() >=
      (sizeof(ebbrt::EthernetHeader) + sizeof(ebbrt::Ipv4Header) +
       sizeof(ebbrt::TcpHeader))) {
    // TCP RESET?
    auto dp = buf->GetMutDataPointer();
    dp.Advance(sizeof(ebbrt::EthernetHeader));
    auto &ip = dp.Get<ebbrt::Ipv4Header>();
    auto &tcp = dp.Get<ebbrt::TcpHeader>();
    uint32_t ackno = ebbrt::ntohl(tcp.ackno);
    uint16_t local_port = ebbrt::ntohs(tcp.dst_port);
    uint16_t remote_port = ebbrt::ntohs(tcp.src_port);
    ebbrt::Ipv4Address local_ip = host_external_ipv4();
    ebbrt::Ipv4Address remote_ip = ip.src;
    ebbrt::network_manager->TcpReset(false, ackno, 0, local_ip, remote_ip,
                                     local_port, remote_port);
  }
  return;
}

bool umm::UmProxy::nat_masquerade_out(std::unique_ptr<ebbrt::MutIOBuf>& buf){
  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  auto dp = buf->GetMutDataPointer();
  auto& eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);

  if (internal_destination(buf)) { /* INTERNAL DESTINATION */
    // Mask ethernet
    eth.src = umm::UmInstance::CoreLocalMac();
    if (ethtype == ebbrt::kEthTypeArp) {
      auto &arp = dp.Get<ebbrt::ArpPacket>();
      arp.sha = umm::UmInstance::CoreLocalMac();
      arp.spa = umm::UmInstance::CoreLocalIp();
    } else if (ethtype == ebbrt::kEthTypeIp) {
      auto &ip = dp.Get<ebbrt::Ipv4Header>();
      ip.src = umm::UmInstance::CoreLocalIp();
      ip.chksum = 0;
      ip.chksum = ip.ComputeChecksum();
      kassert(ip.ComputeChecksum() == 0);
    }
  } else { /* EXTERNAL DESTINATION */
    if (ethtype == ebbrt::kEthTypeIp) {

      // XXX: we arn't masking internal macaddr, is this ok?

      /* apply host mask to ipv4 header */
      auto &ip = dp.Get<ebbrt::Ipv4Header>();
      ip.src = host_external_ipv4();
      ip.chksum = 0;
      ip.chksum = ip.ComputeChecksum();
      kassert(ip.ComputeChecksum() == 0);
      /* apply port mask to tcp source port */
      if (ip.proto == 0x6) {
        auto &tcp = dp.Get<ebbrt::TcpHeader>();
        auto iport = ebbrt::ntohs(tcp.src_port);
        tcp.src_port = ebbrt::htons(swizzle_port_out(iport));  
      }
    } else {
      kprintf_force("UmProxy: NAT not yet implemented for protocol type: %d\n",
              ethtype);
      return false;
    }
  }
  return true;
}


bool umm::UmProxy::nat_preprocess_out(std::unique_ptr<ebbrt::MutIOBuf> &buf) {
  auto dp = buf->GetMutDataPointer();
  auto &eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);
  if (ethtype == ebbrt::kEthTypeArp) {
    auto arp = dp.GetNoAdvance<ebbrt::ArpPacket>();
    if (arp.spa == arp.tpa) { // Ignore Gratuitous ARP
      return false;
    }
  //TODO: more protocols to ignore..?
  } else if (ethtype == 0x86dd) { // Ignore IPv6
    return false;
  }
  return true;
}


bool umm::UmProxy::nat_postprocess_out(std::unique_ptr<ebbrt::MutIOBuf> &buf) {
  return true;
}


/** Process an incoming packet with an UMI target destination */
void umm::UmProxy::ProcessIncoming(std::unique_ptr<ebbrt::IOBuf> buf,
                                   ebbrt::PacketInfo pinfo) {
#if DEBUG_PRINT_IO /// DEBUG OUTPUT
  ebbrt::kprintf_force("\nC%lu:NAT_IN(->umi) len=%d\n",
                       (size_t)ebbrt::Cpu::GetMine(),
                    buf->ComputeChainDataLength());
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif

  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  // Cast buf into an mbuf so we can overwrite the fields
  // Usage of `buf` will segfault after this
  auto mbuf = std::unique_ptr<ebbrt::MutIOBuf>(
      static_cast<ebbrt::MutIOBuf *>(buf.release()));

  // To start, assume this is for active UMI on current core
  size_t current_core = (size_t)ebbrt::Cpu::GetMine();
  auto target_umi = umi_id_;

  // Preprocess
  if (!umm::UmProxy::nat_preprocess_in(mbuf)) {
    return;
  }

  if (internal_source(mbuf)) { // If source is internal
    if (mbuf->Length() >=
        (sizeof(ebbrt::EthernetHeader) + sizeof(ebbrt::Ipv4Header) +
         sizeof(ebbrt::TcpHeader))) {
      auto dp = mbuf->GetDataPointer();
      dp.Advance(sizeof(ebbrt::EthernetHeader));
      auto ip = dp.Get<ebbrt::Ipv4Header>();
      if (ip.proto != 0x6) {
        goto post_masq;
      }
      auto tcp = dp.Get<ebbrt::TcpHeader>();
      // Confirm the destination instance for this packet 
      auto host_src_port = ebbrt::ntohs(tcp.src_port);
      if (tcp.Flags() & ebbrt::kTcpSyn) {
        auto tmp_target = internal_port_lookup(host_src_port);
        if (tmp_target) {
          target_umi = tmp_target;
        } else {
          if (target_umi) {
            // Setup a new mapping for internal TCP connection to the active instance
            kprintf(YELLOW "C%lu:NAT_IN(->umi): WARNING implicit assignment of "
                           "src port %d to UMI%u\n" RESET,
                    (size_t)ebbrt::Cpu::GetMine(), host_src_port, target_umi);
            RegisterInternalPort(target_umi, host_src_port);
          }else{
            kprintf(YELLOW "C%lu:NAT_IN(->umi): WARNING TCP_SYN but no loaded instance!\n");
            return;
          }
        }
      } else {
        target_umi = internal_port_lookup(host_src_port);
      }
    } // end internal TCP
  } else { // If source is external
    // Confirm that the message is TCP
    if (mbuf->Length() >=
        (sizeof(ebbrt::EthernetHeader) + sizeof(ebbrt::Ipv4Header) +
         sizeof(ebbrt::TcpHeader))) {
      auto dp = mbuf->GetDataPointer();
      dp.Advance(sizeof(ebbrt::EthernetHeader));
      auto ip = dp.Get<ebbrt::Ipv4Header>();
      if (ip.proto != 0x6) {
        goto post_masq;
      }
      auto tcp = dp.Get<ebbrt::TcpHeader>();
      auto payload_len = mbuf->Length() - tcp.HdrLen() -
                         sizeof(ebbrt::Ipv4Header) -
                         sizeof(ebbrt::EthernetHeader);

      auto nport = ebbrt::ntohs(tcp.dst_port);
      auto mapping = external_portmap_lookup(nport);
      kassert(mapping != null_port_mapping_);
      target_umi = std::get<0>(mapping);
      auto target_cpu = std::get<1>(mapping);

      // Confirm mapping is still valid
      // Confirm the UMI still valid
      if (mapping == null_port_mapping_) {
        kprintf(YELLOW "Warning: Core %u received packet for nonexistant "
                       "mapping (nport=%d). DROPPING...\n" RESET,
                (size_t)ebbrt::Cpu::GetMine(), nport);
        nat_connection_reset(mbuf);
        return;
        }

        // Check if this is the correct core for this UMI
        // If not, spawn processing on the corresponding core
        if (current_core != target_cpu) {
          kassert(target_cpu < ebbrt::Cpu::Count());
          buf = std::unique_ptr<ebbrt::IOBuf>(
              static_cast<ebbrt::IOBuf *>(mbuf.release()));
          ebbrt::event_manager->SpawnRemote(
              [ this, b = std::move(buf), pinfo ]() mutable {
                umm::proxy->ProcessIncoming(std::move(b), std::move(pinfo));
              },
              target_cpu);
          return;
        }

        // Confirm the target_umi is valid with the UmManager
        auto umi_ref = umm::manager->GetInstance(target_umi);
        if (!umi_ref) {
          kprintf(YELLOW "Core %u received packet for halted/nonexistant "
                         "UMI #%u. SENDING RESET\n" RESET,
                  (size_t)ebbrt::Cpu::GetMine(), target_umi);
          nat_connection_reset(mbuf);
          return;
        }

        // Appy TCP port masquerading (this modifies the buffer)
        if (!umm::UmProxy::nat_masquerade_in(mbuf)) {
          return;
        }

        // Reactivate instance when external TCP packet has non-zero payload
        if (payload_len > 0) {
          // Signal yield for external TCP packet with non-zero payload
          auto umi_ref = umm::manager->GetInstance(target_umi);
          kbugon(!umi_ref);
          umi_ref->SetActive();
        }

      } // End external TCP processing
    }   // End if external

post_masq:
  /** Set the TCP/UDP checksum for incoming packets */
  // TODO: Move below into nat_postprocess_in
  if (pinfo.flags & ebbrt::PacketInfo::kNeedsCsum) {
    if (pinfo.csum_offset == 6) { // UDP
      /* UDP header checksum is optional so we clear the value */
      kabort("UDP checksum not yet supported\n");
      //mbuf->Advance(sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
      //auto udp_header = reinterpret_cast<ebbrt::UdpHeader *>(mbuf->MutData());
      //udp_header->checksum = 0;
      //mbuf->Retreat(sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
    } else if (pinfo.csum_offset == 16) { // TCP
      /* For TCP we compute the checksum with a pseudo IP header */
      mbuf->Advance(sizeof(ebbrt::EthernetHeader));
      auto ip_header = reinterpret_cast<const ebbrt::Ipv4Header *>(mbuf->Data());
      auto src_addr = ip_header->src;
      auto dst_addr = ip_header->dst;
      mbuf->Advance(sizeof(ebbrt::Ipv4Header));
      auto tcp_header = reinterpret_cast<ebbrt::TcpHeader *>(mbuf->MutData());
      tcp_header->checksum = 0;
      tcp_header->checksum =
          IpPseudoCsum(*mbuf, ebbrt::kIpProtoTCP, src_addr, dst_addr);
      mbuf->Retreat(sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
    } else {
      kabort("Error: Unknown packet checksum required ");
    }
  } // end if kNeedsCsum


//#ifndef NDEBUG 
//#if DEBUG_PRINT_IO 
//  ebbrt::kprintf_force("UmProxy(c%lu): INCOMING POST-MASK\n",
//                       (size_t)ebbrt::Cpu::GetMine());
//#endif
//  umm::UmProxy::DebugPrint(buf->GetDataPointer());
//#endif

  auto umi_ref = umm::manager->GetInstance(target_umi);
  if (!umi_ref) {
    kprintf(RED "Core %u received packet for halted/nonexistant "
                   "UMI #%u. DROPPING\n" RESET,
            (size_t)ebbrt::Cpu::GetMine(), target_umi);
    return;
  }

  /* Convert back to read-only buffer */
  buf = std::unique_ptr<ebbrt::IOBuf>(
      static_cast<ebbrt::IOBuf *>(mbuf.release()));
  umi_ref->WritePacket(std::move(buf));
}

void umm::UmProxy::ProcessOutgoing(std::unique_ptr<ebbrt::MutIOBuf> buf){

  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  auto target_umi = umi_id_;
  // Preprocess
  if (!umm::UmProxy::nat_preprocess_out(buf)) {
    return;
  }

  // Masquerading
  if (!umm::UmProxy::nat_masquerade_out(buf)) {
    return;
  }

  // Postprocess
  if (!umm::UmProxy::nat_postprocess_out(buf)) {
    return;
  }

#if DEBUG_PRINT_IO
  ebbrt::kprintf_force("\nC%lu:NAT_OUT(<-umi) len=%d\n",
                       (size_t)ebbrt::Cpu::GetMine(),
                    buf->ComputeChainDataLength());
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif

  if (internal_destination(buf)) {
    if (buf->Length() >=
        (sizeof(ebbrt::EthernetHeader) + sizeof(ebbrt::Ipv4Header) +
         sizeof(ebbrt::TcpHeader))) {
      auto dp = buf->GetDataPointer();
      dp.Advance(sizeof(ebbrt::EthernetHeader));
      auto ip = dp.Get<ebbrt::Ipv4Header>();
      if (ip.proto != 0x6) {
        goto post_check;
      }
      auto tcp = dp.Get<ebbrt::TcpHeader>();
      auto host_dst_port = ebbrt::ntohs(tcp.dst_port);
      target_umi = internal_port_lookup(host_dst_port);
      if( target_umi == umi::null_id) // no match
        return;
      auto umi_ref = umm::manager->GetInstance(target_umi);
      if (!umi_ref) {
        // sending packet for halted/nonexistant UMI... dropping 
        return;
      }
    }
  // Send packet to the LO interface
  post_check:
    root_.lo.itf_.Receive(std::move(buf));
  } else {
    /*
      ebbrt::PackerInfo pinfo;
      uint8_t flags{0};
      uint8_t gso_type{0};
      uint16_t hdr_len{0};
      uint16_t gso_size{0};
      uint16_t csum_start{0};
      uint16_t csum_offset{0};
    */
    ebbrt::PacketInfo pinfo;
    pinfo.flags |= ebbrt::PacketInfo::kNeedsCsum;
    pinfo.csum_start =
        sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader);
    pinfo.csum_offset = 16; // checksum is 16 bytes into the TCP header

    auto dp = buf->GetMutDataPointer();
    auto &eth = dp.Get<ebbrt::EthernetHeader>();
    auto ethtype = ebbrt::ntohs(eth.type);
    auto ip = dp.Get<ebbrt::Ipv4Header>();
    auto &ift = ebbrt::network_manager->GetInterface();
    if (ip.proto == 0x6) {
      auto tcp = dp.Get<ebbrt::TcpHeader>();
      auto payload_len = buf->Length() - tcp.HdrLen() -
                         sizeof(ebbrt::Ipv4Header) -
                         sizeof(ebbrt::EthernetHeader);
      if (payload_len > 0) {
        // Signal yield for external TCP packet with non-zero payload
        auto umi_ref = umm::manager->GetInstance(target_umi);
        kbugon(!umi_ref);
        umi_ref->SetInactive();
      }
    }
    // What does EthArpSend do?...
    //    - retreat buffer to start of eth header
    //    - checks eth for broadcast, multicast, external send
    //    - for external send, set dest_ip to gatewate, lookup in arp cache
    //        - If hit, set gw maddr and do send
    //        - If miss, trigger arp request, send once resolved
    buf->Advance(sizeof(ebbrt::EthernetHeader)); // EthArpSend will retreat
    ift.EthArpSend(ethtype, ip, std::move(buf), pinfo);
  }
  return;
}


std::unique_ptr<ebbrt::MutIOBuf> umm::UmProxy::raw_to_iobuf(const void *data,
                                                            const size_t len) {
  kassert(len > sizeof(ebbrt::EthernetHeader));
  auto ibuf = ebbrt::MakeUniqueIOBuf(len);
  memcpy((void *)ibuf->MutData(), data, len);
  return static_cast<std::unique_ptr<ebbrt::MutIOBuf>>(std::move(ibuf));
}


void umm::UmProxy::SetActiveInstance(umm::umi::id id) {
  // clear the mapping cache?
  umi_id_ = id; // set active instance 
  //port_map_cache_.clear();
}

void umm::UmProxy::RemoveInstanceState(umm::umi::id id) {
#if DEBUG_PRINT_IO
  kprintf("\nC%luU%d:NAT_CLR ",
                       (size_t)ebbrt::Cpu::GetMine(),id);
#endif
  if (umi_id_ == id) {
    umi_id_ = 0; // We no longer have an active instance
    // Clear the port map cache?
    //port_map_cache_.clear();
  }
  auto umi_ref = umm::manager->GetInstance(id);
  if (!umi_ref) {
    kprintf(YELLOW "Tried to free ports of nonexistant UMI #%u\n" RESET, id);
    return;
  }
  // Free the internal ports
  kprintf(GREEN "Freeing ports for UMI #%u\n" RESET, id);
  for (int i : umi_ref->src_ports_) {
    host_src_port_map_cache_.erase(i);
  }
  //root_.FreePorts(id);  Does nothing at the moment
}


uint16_t umm::UmProxy::swizzle_port_in(uint16_t nport) {
  /* search in the core local cache */
  auto mapping = external_portmap_lookup(nport);
  kassert(mapping != null_port_mapping_);
  auto iport = std::get<2>(mapping);
  return iport;
}

uint16_t umm::UmProxy::swizzle_port_out(uint16_t iport) {
  uint16_t ret;
  /* search in the core-local cache for a match */
  auto it = port_map_cache_.right.find(internal_port(iport, umi_id_));
  if (it != port_map_cache_.right.end()) {
    // Cache Hit
    ret = it->second;
  } else {
    // Cache Miss, get a NAT port from the root
    auto nport = root_.SetupExternalPortMapping(umi_id_, iport);
    port_map_cache_.insert(port_map_t::value_type(nport, internal_port(iport, umi_id_)));
    ret = nport;
  }
  return ret;
}

void umm::UmProxy::DebugPrint(ebbrt::IOBuf::DataPointer dp) {
  auto eh = dp.Get<ebbrt::EthernetHeader>();
#if DEBUG_PRINT_ETH
   ebbrt::kprintf_force("  eth type=0x%x\n", ebbrt::ntohs(eh.type));
   ebbrt::kprintf_force("  eth src_mac=%x:%x:%x:%x:%x:%x\n", eh.src[0], eh.src[1],
                        eh.src[2], eh.src[3], eh.src[4], eh.src[5]);
   ebbrt::kprintf_force("  eth dst_mac=%x:%x:%x:%x:%x:%x\n", eh.dst[0], eh.dst[1],
                        eh.dst[2], eh.dst[3], eh.dst[4], eh.dst[5]);
#endif
  auto ethtype = ebbrt::ntohs(eh.type);
  if (ethtype == ebbrt::kEthTypeArp) {
#if DEBUG_PRINT_ARP
     auto ap = dp.GetNoAdvance<ebbrt::ArpPacket>();
     ebbrt::kprintf_force("  arp hardware type 0x%x\n", ebbrt::ntohs(ap.htype));
     ebbrt::kprintf_force("  arp proto type 0x%x\n", ebbrt::ntohs(ap.ptype));
     ebbrt::kprintf_force("  arp opcode 0x%x\n", ebbrt::ntohs(ap.oper));
     ebbrt::kprintf_force("  arp sender mac %x:%x:%x:%x:%x:%x\n", ap.sha[0],
                          ap.sha[1], ap.sha[2], ap.sha[3], ap.sha[4], ap.sha[5]);
     ebbrt::kprintf_force("  arp target mac %x:%x:%x:%x:%x:%x\n", ap.tha[0],
                          ap.tha[1], ap.tha[2], ap.tha[3], ap.tha[4], ap.tha[5]);
     auto spa = ap.spa.toArray();
     ebbrt::kprintf_force("  arp sender ip %hhd.%hhd.%hhd.%hhd \n", spa[0],
                          spa[1], spa[2], spa[3]);
     spa = ap.tpa.toArray();
     ebbrt::kprintf_force("  arp target ip %hhd.%hhd.%hhd.%hhd \n", spa[0],
                          spa[1], spa[2], spa[3]);
#endif
     dp.Advance(sizeof(ebbrt::ArpPacket));
  } else if (ethtype == ebbrt::kEthTypeIp) {
    auto ip = dp.Get<ebbrt::Ipv4Header>();
#if DEBUG_PRINT_IP
     ebbrt::kprintf_force("  ip proto type 0x%x\n", ip.proto);
     auto spa = ip.src.toArray();
     ebbrt::kprintf_force("  ip sender ip %hhd.%hhd.%hhd.%hhd \n", spa[0],
                          spa[1], spa[2], spa[3]);
     spa = ip.dst.toArray();
     ebbrt::kprintf_force("  ip target ip %hhd.%hhd.%hhd.%hhd \n", spa[0],
                          spa[1], spa[2], spa[3]);
     ebbrt::kprintf_force("  ip checksum 0x%x\n", ip.chksum);
#endif
    if (ip.proto == 0x6) {
#if DEBUG_PRINT_TCP
      auto tcp = dp.GetNoAdvance<ebbrt::TcpHeader>();
      ebbrt::kprintf_force("  tcp src port %d\n", ebbrt::ntohs(tcp.src_port));
      ebbrt::kprintf_force("  tcp dst port %d\n", ebbrt::ntohs(tcp.dst_port));
      ebbrt::kprintf_force("  tcp seqno 0x%x\n", ebbrt::ntohl(tcp.seqno));
      ebbrt::kprintf_force("  tcp ackno 0x%x\n", ebbrt::ntohl(tcp.ackno));
      ebbrt::kprintf_force("  tcp checksum 0x%x\n", ebbrt::ntohl(tcp.checksum));
      ebbrt::kprintf_force("  tcp FLAGS 0x%x\n", tcp.Flags());
      if (tcp.Flags() & ebbrt::kTcpFin)
        ebbrt::kprintf_force(RED "  tcp type FIN\n" RESET);
      if (tcp.Flags() & ebbrt::kTcpSyn)
        ebbrt::kprintf_force("  tcp type SYN\n");
      if (tcp.Flags() & ebbrt::kTcpRst)
        ebbrt::kprintf_force(RED "  tcp type RST\n" RESET);
      if (tcp.Flags() & ebbrt::kTcpPsh)
        ebbrt::kprintf_force("  tcp type PSH\n");
      if (tcp.Flags() & ebbrt::kTcpAck)
        ebbrt::kprintf_force("  tcp type ACK\n");
      if (tcp.Flags() & ebbrt::kTcpUrg)
        ebbrt::kprintf_force("  tcp type URG\n");
      if (tcp.Flags() & ebbrt::kTcpEce)
        ebbrt::kprintf_force("  tcp type ECE\n");
      if (tcp.Flags() & ebbrt::kTcpCwr)
        ebbrt::kprintf_force("  tcp type CWR\n");
#endif
     dp.Advance(sizeof(ebbrt::TcpHeader));
    } else if (ip.proto == 0x11) {
#if DEBUG_PRINT_UDP
      auto udp = dp.Get<ebbrt::UdpHeader>();
      ebbrt::kprintf_force("  udp src port %d\n", ebbrt::ntohs(udp.src_port));
      ebbrt::kprintf_force("  udp dst port %d\n", ebbrt::ntohs(udp.dst_port));
      ebbrt::kprintf_force("  udp len %d\n", ebbrt::ntohs(udp.length));
      ebbrt::kprintf_force("  udp checksum 0x%x\n", ebbrt::ntohs(udp.checksum));
     dp.Advance(sizeof(ebbrt::UdpHeader));
#endif 
    }
  }
}

