//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_PROXY_H_
#define UMM_UM_PROXY_H_

#include <queue>

#include <ebbrt/EbbId.h>
#include <ebbrt/GlobalStaticIds.h>
#include <ebbrt/MulticoreEbb.h>
#include <ebbrt/UniqueIOBuf.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/NetChecksum.h>
#include <ebbrt/native/NetEth.h>
#include <ebbrt/native/NetUdp.h>

#include "umm-common.h"

namespace umm {

class UmProxy;

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
  ebbrt::EbbRef<UmProxy> proxy_;
  ebbrt::EthernetAddress mac_addr_;
  ebbrt::NetworkManager::Interface &itf_;

  friend class UmProxy;
};

/**
 *  UmProxy - Ebb that manages per-core network IO of SV instances
 */
class UmProxy : public ebbrt::MulticoreEbb<UmProxy, LoopbackDriver> {
public:
  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("UmProxy");
  /** Class-wide static initialization logic */
  static void Init();
  static void DebugPrint(ebbrt::IOBuf::DataPointer dp);
  explicit UmProxy(const LoopbackDriver &root) : root_(root) {}
  ebbrt::EthernetAddress UmMac();

  /**	UmWrite - Outgoing data from the UM instance
   *  returns the amount written
   */
  uint32_t UmWrite(const void *data, const size_t len);

  /**  UmRead - Incoming data read to UM instance
   */
  uint32_t UmRead(void *data, const size_t len);

  /**	UmHasData
   *  Return true if there is data to be read
   */
  bool UmHasData() { return !um_recv_queue_.empty(); }

  /** Receive data from the ebbrt network stack
  */
  void Receive(std::unique_ptr<ebbrt::IOBuf> buf, ebbrt::PacketInfo pinfo);

private:
  std::queue<std::unique_ptr<ebbrt::IOBuf>> um_recv_queue_;
  const LoopbackDriver &root_;
};

constexpr auto proxy = ebbrt::EbbRef<UmProxy>(UmProxy::global_id);
}
#endif // UMM_UM_PROXY_H_
