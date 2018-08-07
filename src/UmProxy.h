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
#include <ebbrt/native/Net.h>
#include <ebbrt/native/NetChecksum.h>
#include <ebbrt/native/NetTcpHandler.h>
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
	/** TcpSession handler
	 * 	Manages a TCP connection between the Um instance and the UmProxy
	 */
  class TcpSession : public ebbrt::TcpHandler {
  public:
    TcpSession(ebbrt::NetworkManager::TcpPcb pcb)
        : ebbrt::TcpHandler(std::move(pcb)), core_(ebbrt::Cpu::GetMine()) {
      ebbrt::kprintf_force("UmProxy starting TCP connection (core %d)", core_);
    }
    void Close() { ebbrt::kprintf_force("UmProxy TCP connection closed (core %d)\n", core_); }
    void Connected() override;
    void Abort() { ebbrt::kprintf_force("UmProxy TCP connection aborted (core %d)\n", core_); }
    void Receive(std::unique_ptr<ebbrt::MutIOBuf> b);
  private:
		// TODO(jmcadden): received data buffer 
		const size_t core_;
  }; // end TcpSession

  /** Class-wide UmProxy state & initialization */
  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("UmProxy");
  static void Init();
  static void DebugPrint(ebbrt::IOBuf::DataPointer dp);

  explicit UmProxy(const LoopbackDriver &root) : root_(root) {}

  /**	UmMac - Returns mac address for an Um instance */
  ebbrt::EthernetAddress UmMac();

  /**	UmWrite - Outgoing data from the UM instance
   *  returns the amount of data written
   */
  uint32_t UmWrite(const void *data, const size_t len);

  /**  UmRead - Incoming data read to UM instance */
  uint32_t UmRead(void *data, const size_t len);

  /**	UmHasData - Returns 'true' if there is data to be read */
  bool UmHasData() { return !um_recv_queue_.empty(); }

  /** Receive data from the ebbrt network stack */
  void Receive(std::unique_ptr<ebbrt::IOBuf> buf, ebbrt::PacketInfo pinfo);

  /** Connect - TCP connection to the Um instance */
	TcpSession* Connect(uint16_t port=8080); 

private:
  std::queue<std::unique_ptr<ebbrt::IOBuf>> um_recv_queue_;
  const LoopbackDriver &root_;
};

constexpr auto proxy = ebbrt::EbbRef<UmProxy>(UmProxy::global_id);
}
#endif // UMM_UM_PROXY_H_
