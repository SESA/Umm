//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <string.h> // memcpy

#include "UmProxy.h"
#include "UmInstance.h"
#include "umm-internal.h"

// DEBUG NETWORK TRACE
#define DEBUG_PRINT_ETH 1
#define DEBUG_PRINT_ARP 1
#define DEBUG_PRINT_IP  1
#define DEBUG_PRINT_TCP 1
#define DEBUG_PRINT_UDP 1
#define DEBUG_PRINT_IO  1

void umm::UmProxy::Init() {
  // Setup Ebb translations
  auto lo_dev = new LoopbackDriver();
  auto proxy_root = new ProxyRoot(*lo_dev);
  Create(proxy_root, UmProxy::global_id);
}

umm::umi::exec_location umm::ProxyRoot::GetLocationFromPort(uint16_t nport){
  kprintf("Get location for NAT port %u\n", nport);
  nat_port_map_left_iterator_t it = nport_to_umi_map_.left.find(nport);
  if (it != nport_to_umi_map_.left.end()) {
    // Hit
    kprintf("Location found: NAT port=%u, umi=%u, core=%u\n", it->first, it->second.first, it->second.second);
    return it->second;
  } else {
    // Miss
    kabort("Error: No match found for NAT port %u\n", nport);
  }
}

uint16_t umm::ProxyRoot::RegisterPortMask(umi::id id) {
  auto port = allocate_port();
  size_t core = (size_t)ebbrt::Cpu::GetMine();
  umi_location loc = std::make_pair(id,core); 
  kprintf("ProxyRoot: Register NAT port %u to umi #%u on core %u\n", port, loc.first, loc.second); 
  nport_to_umi_map_.insert(nat_port_map_t::value_type(port, loc));
	return port;
}

uint16_t umm::ProxyRoot::allocate_port() {
  if (unlikely(port_set_.empty())) {
    kabort("ProxyRoot: ERROR no NAT ports remaining \n");
  }
  std::lock_guard<ebbrt::SpinLock> guard(lock_);
  uint16_t ret = boost::icl::first(port_set_);
  port_set_.subtract(ret);
  return ret;
}

void umm::ProxyRoot::free_port(uint16_t val) {
  std::lock_guard<ebbrt::SpinLock> guard(lock_);
  port_set_ += val;
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
  for (uint8_t i = 0; i < 4; ++i) {
    if (dst[i] != iip[i]) {
      kprintf(
          MAGENTA "Destination IP is external:  %hhd.%hhd.%hhd.%hhd \n" RESET,
          dst[0], dst[1], dst[2], dst[3]);
      return false;
    }
  }
  //ebbrt::kprintf_force("Destination IP is internal:  %hhd.%hhd.%hhd.%hhd \n",
  //                     dst[0], dst[1], dst[2], dst[3]);
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


umm::ProxyRoot::umi_location umm::UmProxy::nat_get_info(std::unique_ptr<ebbrt::MutIOBuf> &buf){
 
  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  auto dp = buf->GetMutDataPointer();
  auto &eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);
  kassert(ethtype == ebbrt::kEthTypeIp);
  auto &ip = dp.Get<ebbrt::Ipv4Header>();
  kassert(ip.proto == 0x6);
  auto &tcp = dp.Get<ebbrt::TcpHeader>();
  auto nport = ebbrt::ntohs(tcp.dst_port);
  return root_.GetLocationFromPort(nport);
}

bool umm::UmProxy::nat_masquerade_in(std::unique_ptr<ebbrt::MutIOBuf> &buf) {
  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  auto dp = buf->GetMutDataPointer();
  auto &eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);
  // Only apply port masquerade to external TCP/IP traffic
  if (ethtype == ebbrt::kEthTypeIp) {
    auto &ip = dp.Get<ebbrt::Ipv4Header>();
    if(ip.proto == 0x6){
      kprintf(MAGENTA "Processing incoming external packet\n" RESET);
      // Grab the tcp/ip headers and let's take a look
      auto &tcp = dp.Get<ebbrt::TcpHeader>();
      auto nport = ebbrt::ntohs(tcp.dst_port);
      // Conform we're on the right core for this packet
      // loc is std::pair<umi::id, umi::core>
      auto loc = root_.GetLocationFromPort(nport);
      size_t core = loc.second;
      uint32_t umi_id = loc.first;
      if (core != (size_t)ebbrt::Cpu::GetMine()) { // TODO: Send this packet to
                                                   // the correct core and
                                                   // return
        kprintf("Warning: Core %u received pkt for umi %u on core %u. "
                "DROPPING...\n",
                (size_t)ebbrt::Cpu::GetMine(), umi_id, core);
        
        //ebbrt::event_manager->SpawnRemote([this]() { ebb_->Poke(); }, i);
        return false;
      }
      if (umi_id != umi_id_) {
        // TODO: Handle umi sceduling
        kprintf("Warning: Core %u received packet for umi (%u) which is no "
                "longer sceduled on this core. Current umi: %u. DROPPING...\n",
                (size_t)ebbrt::Cpu::GetMine(), umi_id, umi_id_);
        nat_connection_reset(buf);
        return false;
      }
      // Set the correct destination port
      tcp.dst_port = ebbrt::htons(swizzle_port_in(nport));
    }
  }
  return true;
}

void umm::UmProxy::nat_connection_reset(std::unique_ptr<ebbrt::MutIOBuf> &buf) {
  kassert(buf->Length() >=
          (sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader)));
  // TCP RESET?


  auto dp = buf->GetMutDataPointer();
  dp.Advance(sizeof(ebbrt::EthernetHeader));
  auto &ip = dp.Get<ebbrt::Ipv4Header>();
  auto &tcp = dp.Get<ebbrt::TcpHeader>();
  uint32_t seqno = ebbrt::ntohl(tcp.seqno);
  uint32_t ackno = ebbrt::ntohl(tcp.ackno);
  uint16_t local_port = ebbrt::ntohs(tcp.dst_port);
  uint16_t remote_port = ebbrt::ntohs(tcp.src_port);
  ebbrt::Ipv4Address local_ip = host_external_ipv4();
  ebbrt::Ipv4Address remote_ip = ip.src;

  // ** DEBUG PRINT **
      kprintf(RED "TCP CONNECTION RESET:\n" );
  ebbrt::kprintf_force("tcp: seq=0x%x ack=0x%x iport:%d rport:%d\n", seqno, ackno,
                       local_port, remote_port);
  auto spa = local_ip.toArray();
  ebbrt::kprintf_force("  local_ip: %hhd.%hhd.%hhd.%hhd \n", spa[0], spa[1],
                       spa[2], spa[3]);
  spa = remote_ip.toArray();
  ebbrt::kprintf_force("  remote_ip: %hhd.%hhd.%hhd.%hhd \n" RESET, spa[0],
                       spa[1], spa[2], spa[3]);

  /*
    void TcpReset(bool ack, uint32_t seqno, uint32_t ackno,
                  const ipv4address& local_ip, const Ipv4Address& remote_ip,
                  uint16_t local_port, uint16_t remote_port);
  */
  ebbrt::network_manager->TcpReset(false, ackno, 0, local_ip, remote_ip,
                                   local_port, remote_port);
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

#ifndef NDEBUG /// DEBUG OUTPUT
#if DEBUG_PRINT_IO
  ebbrt::kprintf_force(
      "UmProxy(c%lu): OUTGOING PRE-MASK (<-umi) len=%d chain_len=%d\n",
      (size_t)ebbrt::Cpu::GetMine(), buf->ComputeChainDataLength(),
      buf->CountChainElements());
#endif
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif
  return true;
}

bool umm::UmProxy::nat_postprocess_out(std::unique_ptr<ebbrt::MutIOBuf> &buf) {
  return true;
}

/** Process an incoming packet with an UMI target destination */
void umm::UmProxy::ProcessIncoming(std::unique_ptr<ebbrt::IOBuf> buf,
                                   ebbrt::PacketInfo pinfo) {
#ifndef NDEBUG 
#if DEBUG_PRINT_IO /// DEBUG OUTPUT
  ebbrt::kprintf_force("UmProxy(c%lu): INCOMING PRE-MASK\n",
                       (size_t)ebbrt::Cpu::GetMine());
#endif
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif

  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  auto mbuf = std::unique_ptr<ebbrt::MutIOBuf>(
      static_cast<ebbrt::MutIOBuf *>(buf.release()));

  // Preprocess
  if (!umm::UmProxy::nat_preprocess_in(mbuf)) {
    return;
  }
  // External masquerade
  if (!internal_source(mbuf)) { // check if source is external 
    if (!umm::UmProxy::nat_masquerade_in(mbuf)) {
      auto loc = umm::UmProxy::nat_get_info(mbuf);
      size_t core = loc.second;
      if (core != (size_t)ebbrt::Cpu::GetMine()) {
        uint32_t umi_id = loc.first;
        kprintf("OK: Forwarding packet for umi %u to core %u.\n", umi_id, core);
        buf = std::unique_ptr<ebbrt::IOBuf>(
            static_cast<ebbrt::IOBuf *>(mbuf.release()));
        ebbrt::event_manager->SpawnRemote(
            [ this, b = std::move(buf), pinfo ]() mutable {
              umm::proxy->ProcessIncoming(std::move(b), std::move(pinfo));
            },
            core);
        return;
      }

      return;
    }
  }
  //TODO: Move below into nat_postprocess_in
  /** Set the TCP/UDP checksum for incoming packets */
  if (pinfo.flags & ebbrt::PacketInfo::kNeedsCsum) {
    if (pinfo.csum_offset == 6) { // UDP
      /* UDP header checksum is optional so we clear the value */
      kabort("UDP checksum not yet supported\n");
      mbuf->Advance(sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
      auto udp_header = reinterpret_cast<ebbrt::UdpHeader *>(mbuf->MutData());
      udp_header->checksum = 0;
      mbuf->Retreat(sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
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
      //buf = std::unique_ptr<ebbrt::IOBuf>(static_cast<ebbrt::IOBuf *>(mbuf));
    } else {
      kabort("Error: Unknown packet checksum required ");
    }
  } // end if kNeedsCsum
  buf = std::unique_ptr<ebbrt::IOBuf>(
      static_cast<ebbrt::IOBuf *>(mbuf.release()));
#ifndef NDEBUG 
#if DEBUG_PRINT_IO 
  ebbrt::kprintf_force("UmProxy(c%lu): INCOMING POST-MASK\n",
                       (size_t)ebbrt::Cpu::GetMine());
#endif
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif

  // Queue packed be read by the UM instance 
  um_recv_queue_.emplace(std::move(buf));
}

void umm::UmProxy::ProcessOutgoing(std::unique_ptr<ebbrt::MutIOBuf> buf){

  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
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

#ifndef NDEBUG /// DEBUG OUTPUT
#if DEBUG_PRINT_IO
  ebbrt::kprintf_force("UmProxy(c%lu): OUTGOING POST-MASK (<-umi) len=%d chain_len=%d\n",
                       (size_t)ebbrt::Cpu::GetMine(),
                    buf->ComputeChainDataLength(),
                    buf->CountChainElements());
#endif
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif

  if (internal_destination(buf)) {
    // Send packet to the LO interface
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
    pinfo.csum_start = sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader);
    pinfo.csum_offset = 16; // checksum is 16 bytes into the TCP header

    auto dp = buf->GetMutDataPointer();
    auto &eth = dp.Get<ebbrt::EthernetHeader>();
    auto ethtype = ebbrt::ntohs(eth.type);
    auto ip = dp.Get<ebbrt::Ipv4Header>();
    auto &ift = ebbrt::network_manager->GetInterface();
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

//uint32_t umm::UmProxy::UmWrite(const void *data, const size_t len) {
//  kassert(len > sizeof(ebbrt::EthernetHeader));
//  auto eth = (ebbrt::EthernetHeader *)(data);
//  auto ethtype = ebbrt::ntohs(eth->type);
//  if (ethtype == ebbrt::kEthTypeArp) {
//    auto arp = (ebbrt::ArpPacket *)((const uint8_t *)data +
//                                    sizeof(ebbrt::EthernetHeader));
//    if (arp->spa == arp->tpa) { // Gratuitous ARP
//      kprintf("UmProxy ignore gratuitous ARP\n");
//      return len;
//    }
//  } else if (ethtype == 0x86dd) { // ignore IPv6
//    // kprintf("UmProxy ignore type=0x%x len=%d\n", ethtype, len);
//    return len;
//  }
//  auto ibuf = ebbrt::MakeUniqueIOBuf(len);
//  memcpy((void *)ibuf->MutData(), data, len);
//  auto buf = static_cast<std::unique_ptr<ebbrt::MutIOBuf>>(std::move(ibuf));
//
//#ifndef NDEBUG /// DEBUG OUTPUT
//#if DEBUG_PRINT_IO
//  ebbrt::kprintf_force("(C#%lu) LO INCOMING (lo<-umi) len=%d chain_len=%d\n",
//                       (size_t)ebbrt::Cpu::GetMine(),
//                    buf->ComputeChainDataLength(),
//                    buf->CountChainElements());
//#endif
//  umm::UmProxy::DebugPrint(buf->GetDataPointer());
//#endif 
//
//  root_.lo.itf_.Receive(std::move(buf));
//  return len;
//}

uint32_t umm::UmProxy::UmRead(void *data, const size_t len) {
  if (um_recv_queue_.empty()) {
    return 0;
  }
  kprintf("Core %u: received message for Um\n", (size_t)ebbrt::Cpu::GetMine());
  // remove the first element from the queue and pass it upto the instance
  auto buf = std::move(um_recv_queue_.front());
  um_recv_queue_.pop();
  auto in_len = buf->ComputeChainDataLength();
  // Assert we are not trying to send more than can be read
  kassert(len >= in_len);
  auto dp = buf->GetDataPointer();
  dp.GetNoAdvance(in_len, static_cast<uint8_t *>(data));
  // kprintf("Core %u: Um read in data\n", (size_t)ebbrt::Cpu::GetMine());
  return in_len;
}

uint16_t umm::UmProxy::swizzle_port_in(uint16_t nport) {
  /* search in the core local cache */
  auto it = umi_port_map_cache_.right.find(nport);
  if (it != umi_port_map_cache_.right.end()){
    // Cache Hit
    kprintf("Swizzle nport %u! iport=%u\n", nport, it->second);
    return it->second; 
  }
  // Cache Miss
  kabort("UmProxy: NAT ERROR no port mapping found on this core for this umi\n");
}

uint16_t umm::UmProxy::swizzle_port_out(uint16_t iport) {
  uint16_t ret;
  /* search in the core-local cache for a match */
  auto it = umi_port_map_cache_.left.find(iport);
  if (it != umi_port_map_cache_.left.end()) {
    // Hit
    ret = it->second;
  } else {
    // Miss, get a NAT port from the root
    auto nport = root_.RegisterPortMask(umi_id_);
    umi_port_map_cache_.insert(port_map_t::value_type(iport, nport));
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
        ebbrt::kprintf_force("  tcp type FIN\n");
      if (tcp.Flags() & ebbrt::kTcpSyn)
        ebbrt::kprintf_force("  tcp type SYN\n");
      if (tcp.Flags() & ebbrt::kTcpRst)
        ebbrt::kprintf_force("  tcp type RST\n");
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

