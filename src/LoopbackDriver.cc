//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "LoopbackDriver.h"
#include "UmProxy.h"

LoopbackDriver::LoopbackDriver()
    : itf_(ebbrt::network_manager->NewLoopback(*this)) {
  /** Hard-code ipv4 and mac address for LO interface */
  mac_addr_ = {{0x06, 0xfe, 0x22, 0x22, 0x22, 0x22}};
  auto addr = new ebbrt::NetworkManager::Interface::ItfAddress();
  addr->address = {{169, 254, 0, 1}};
  addr->netmask = {{255, 255, 0, 0}};
  addr->gateway = {{169, 254, 0, 1}};
  itf_.SetAddress(
      std::unique_ptr<ebbrt::NetworkManager::Interface::ItfAddress>(addr));
}

void LoopbackDriver::Send(std::unique_ptr<ebbrt::IOBuf> buf,
                               ebbrt::PacketInfo pinfo) {
  umm::proxy->ProcessIncoming(std::move(buf), std::move(pinfo));
}
