
//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_LOOPBACK_DRIVER_H_
#define UMM_LOOPBACK_DRIVER_H_

#include "umm-common.h"

namespace umm {
  class UmProxy;
}

/**
 *  LoopbackDriver - Loopback Device Driver
 */
class LoopbackDriver : public ebbrt::EthernetDevice {
public:
  LoopbackDriver();
  void Send(std::unique_ptr<ebbrt::IOBuf> buf,
            ebbrt::PacketInfo pinfo) override;
  const ebbrt::EthernetAddress &GetMacAddress() override { return mac_addr_; }

private:
  ebbrt::EbbRef<umm::UmProxy> proxy_;
  ebbrt::EthernetAddress mac_addr_;
  ebbrt::NetworkManager::Interface &itf_;

  friend class umm::UmProxy;
};

#endif // UMM_LOOPBACK_DRIVER_H_
