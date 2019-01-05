//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <string.h> // memcpy

#include "UmProxy.h"
#include "umm-internal.h"

// DEBUG NETWORK TRACE
#define DEBUG_PRINT_ETH 0
#define DEBUG_PRINT_ARP 0
#define DEBUG_PRINT_IP  0
#define DEBUG_PRINT_TCP 0
#define DEBUG_PRINT_UDP 0
#define DEBUG_PRINT_IO  0

void umm::UmProxy::Init() {
  // Setup Ebb translations
  auto lo_dev = new LoopbackDriver();
  Create(lo_dev, UmProxy::global_id);
}

umm::LoopbackDriver::LoopbackDriver()
    : itf_(ebbrt::network_manager->NewLoopback(*this)) {
  /** Hard-coded the LO interface ipv4 and mac address */
  mac_addr_ = {{0x06, 0xfe, 0x22, 0x22, 0x22, 0x22}};
  auto addr = new ebbrt::NetworkManager::Interface::ItfAddress();
  addr->address = {{169, 254, 0, 1}};
  addr->netmask = {{255, 255, 0, 0}};
  addr->gateway = {{169, 254, 0, 1}};
  itf_.SetAddress(
      std::unique_ptr<ebbrt::NetworkManager::Interface::ItfAddress>(addr));
}

void umm::LoopbackDriver::Send(std::unique_ptr<ebbrt::IOBuf> buf,
                               ebbrt::PacketInfo pinfo) {

#ifndef NDEBUG /// DEBUG OUTPUT
#if DEBUG_PRINT_IO
  ebbrt::kprintf_force("(C#%lu) LO OUTGOING PRE-MASK (lo->umi) len=%d chain_len=%d\n",
                      (size_t)ebbrt::Cpu::GetMine(),
                      buf->ComputeChainDataLength(),
                      buf->CountChainElements());
#endif
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif

  auto mbuf = std::unique_ptr<ebbrt::MutIOBuf>(static_cast<ebbrt::MutIOBuf *>(buf.release()));
  umm::UmProxy::nat_masquerade_in(mbuf);
  buf = std::unique_ptr<ebbrt::IOBuf>(static_cast<ebbrt::IOBuf *>(mbuf.release()));

  /** Set the TCP/UDP checksum for outgoing packets */
  if (pinfo.flags & ebbrt::PacketInfo::kNeedsCsum) {
    if (pinfo.csum_offset == 6) { // UDP
      /* UDP header checksum is optional so we clear the value */
      kabort("UDP checksum not yet supported\n");
      buf->Advance(sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
      auto mbuf = static_cast<ebbrt::MutIOBuf *>(buf.release());
      auto udp_header = reinterpret_cast<ebbrt::UdpHeader *>(mbuf->MutData());
      udp_header->checksum = 0;
      mbuf->Retreat(sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
      buf = std::unique_ptr<ebbrt::IOBuf>(static_cast<ebbrt::IOBuf *>(mbuf));
    } else if (pinfo.csum_offset == 16) { // TCP
      kprintf("Computing TCP checksum \n");
      /* For TCP we compute the checksum with a pseudo IP header */
      buf->Advance(sizeof(ebbrt::EthernetHeader));
      auto ip_header = reinterpret_cast<const ebbrt::Ipv4Header *>(buf->Data());
      auto src_addr = ip_header->src;
      auto dst_addr = ip_header->dst;
      buf->Advance(sizeof(ebbrt::Ipv4Header));
      auto mbuf = static_cast<ebbrt::MutIOBuf *>(buf.release());
      auto tcp_header = reinterpret_cast<ebbrt::TcpHeader *>(mbuf->MutData());
      tcp_header->checksum = 0;
      tcp_header->checksum =
          IpPseudoCsum(*mbuf, ebbrt::kIpProtoTCP, src_addr, dst_addr);
      mbuf->Retreat(sizeof(ebbrt::Ipv4Header) + sizeof(ebbrt::EthernetHeader));
      buf = std::unique_ptr<ebbrt::IOBuf>(static_cast<ebbrt::IOBuf *>(mbuf));
    } else {
      kabort("Error: Unknown packet checksum required ");
    }
  } // end if kNeedsCsum

#ifndef NDEBUG /// DEBUG OUTPUT
#if DEBUG_PRINT_IO
  ebbrt::kprintf_force("(C#%lu) LO OUTGOING (lo->umi) len=%d chain_len=%d\n",
                      (size_t)ebbrt::Cpu::GetMine(),
                      buf->ComputeChainDataLength(),
                      buf->CountChainElements());
#endif
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif

  umm::proxy->Receive(std::move(buf), std::move(pinfo));
}


bool umm::UmProxy::internal_destination(std::unique_ptr<ebbrt::MutIOBuf>& buf){

  auto dp = buf->GetMutDataPointer();
  auto& eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);

  if (ethtype == ebbrt::kEthTypeArp) {
    return true;
  }

  kassert(ethtype == ebbrt::kEthTypeIp);
  auto iph = dp.Get<ebbrt::Ipv4Header>();
  auto dst = iph.dst.toArray();
  auto iip = host_internal_ipv4().toArray();
  for (uint8_t i = 0; i < 4; ++i) {
    if (dst[i] != iip[i]) {
      ebbrt::kprintf_force(
          "Destination IP is external:  %hhd.%hhd.%hhd.%hhd \n", 
                       dst[0], dst[1], dst[2], dst[3]);
      return false;
    }
  }
  ebbrt::kprintf_force("Destination IP is internal:  %hhd.%hhd.%hhd.%hhd \n",
                       dst[0], dst[1], dst[2], dst[3]);
  return true;
}

void umm::UmProxy::nat_preprocess_out(std::unique_ptr<ebbrt::MutIOBuf>& buf){}

void umm::UmProxy::nat_preprocess_in(std::unique_ptr<ebbrt::MutIOBuf>& buf){}

void umm::UmProxy::nat_masquerade_out(std::unique_ptr<ebbrt::MutIOBuf>& buf){

  auto dp = buf->GetMutDataPointer();
  auto& eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);

  // Mask ethernet 
  eth.src = client_local_macaddr();

  if (ethtype == ebbrt::kEthTypeArp) {
     auto& arp = dp.Get<ebbrt::ArpPacket>();
     arp.sha = client_local_macaddr();
     arp.spa = client_local_ipv4();
  } else if (ethtype == ebbrt::kEthTypeIp) {
    auto &ip = dp.Get<ebbrt::Ipv4Header>();
    ip.src = client_local_ipv4();
    ip.chksum = 0;
    ip.chksum = ip.ComputeChecksum();
  }
}

void umm::UmProxy::nat_masquerade_in(std::unique_ptr<ebbrt::MutIOBuf>& buf){

  /* Overwrite the destination/target IP and MAC with thier internal values */

  auto dp = buf->GetMutDataPointer();
  auto& eth = dp.Get<ebbrt::EthernetHeader>();
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
    if(ebbrt::ntohs(arp.oper) == 0x2){ // ARP REPLY
    }
  } else if (ethtype == ebbrt::kEthTypeIp) {
    eth.dst = client_internal_macaddr();
    auto &ip = dp.Get<ebbrt::Ipv4Header>();
    ip.dst = client_internal_ipv4();
    ip.chksum = 0;
    ip.chksum = ip.ComputeChecksum();
  }
}

void umm::UmProxy::ProcessOutgoing(std::unique_ptr<ebbrt::MutIOBuf> buf){

  // PREPROCESSING
  kassert(buf->Length() >= sizeof(ebbrt::EthernetHeader));
  auto dp = buf->GetMutDataPointer();
  auto& eth = dp.Get<ebbrt::EthernetHeader>();
  auto ethtype = ebbrt::ntohs(eth.type);
  if (ethtype == ebbrt::kEthTypeArp) {
    auto arp = dp.GetNoAdvance<ebbrt::ArpPacket>();
    if (arp.spa == arp.tpa) { // Gratuitous ARP
      kprintf("UmProxy ignore gratuitous ARP\n");
      // DROP PACKET AND RETURN
      return;
    }
  } else if (ethtype == 0x86dd) { // ignore IPv6
      kprintf("UmProxy ignore IPV6 noise\n");
    // DROP PACKET AND RETURN
    return;
  }

#ifndef NDEBUG /// DEBUG OUTPUT
#if DEBUG_PRINT_IO
  ebbrt::kprintf_force("(C#%lu) LO INCOMING PRE-MASK (lo<-umi) len=%d chain_len=%d\n",
                       (size_t)ebbrt::Cpu::GetMine(),
                    buf->ComputeChainDataLength(),
                    buf->CountChainElements());
#endif
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif

  // MASQUERADING
  //NAT::Outbound::Masquerade
  nat_masquerade_out(buf);

#ifndef NDEBUG /// DEBUG OUTPUT
#if DEBUG_PRINT_IO
  ebbrt::kprintf_force("(C#%lu) LO INCOMING (lo<-umi) len=%d chain_len=%d\n",
                       (size_t)ebbrt::Cpu::GetMine(),
                    buf->ComputeChainDataLength(),
                    buf->CountChainElements());
#endif
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif

  if(internal_destination(buf)){
    // Send packet to the LO interface
    root_.itf_.Receive(std::move(buf));
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

void umm::UmProxy::Receive(std::unique_ptr<ebbrt::IOBuf> buf,
                           ebbrt::PacketInfo pinfo) {
  // Queue network data to be read into the UM instance (via UmRead)
  if (!buf->ComputeChainDataLength())
    return;
  um_recv_queue_.emplace(std::move(buf));
}

ebbrt::EthernetAddress umm::UmProxy::UmMac() {
  size_t core = ebbrt::Cpu::GetMine();
  return {{0x06, 0xfe, 0x00, 0x00, 0x00, (uint8_t)core}};
}

uint32_t umm::UmProxy::UmWrite(const void *data, const size_t len) {
  kassert(len > sizeof(ebbrt::EthernetHeader));
  auto eth = (ebbrt::EthernetHeader *)(data);
  auto ethtype = ebbrt::ntohs(eth->type);
  if (ethtype == ebbrt::kEthTypeArp) {
    auto arp = (ebbrt::ArpPacket *)((const uint8_t *)data +
                                    sizeof(ebbrt::EthernetHeader));
    if (arp->spa == arp->tpa) { // Gratuitous ARP
      kprintf("UmProxy ignore gratuitous ARP\n");
      return len;
    }
  } else if (ethtype == 0x86dd) { // ignore IPv6
    // kprintf("UmProxy ignore type=0x%x len=%d\n", ethtype, len);
    return len;
  }
  auto ibuf = ebbrt::MakeUniqueIOBuf(len);
  memcpy((void *)ibuf->MutData(), data, len);
  auto buf = static_cast<std::unique_ptr<ebbrt::MutIOBuf>>(std::move(ibuf));

#ifndef NDEBUG /// DEBUG OUTPUT
#if DEBUG_PRINT_IO
  ebbrt::kprintf_force("(C#%lu) LO INCOMING (lo<-umi) len=%d chain_len=%d\n",
                       (size_t)ebbrt::Cpu::GetMine(),
                    buf->ComputeChainDataLength(),
                    buf->CountChainElements());
#endif
  umm::UmProxy::DebugPrint(buf->GetDataPointer());
#endif 

  root_.itf_.Receive(std::move(buf));
  return len;
}

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

