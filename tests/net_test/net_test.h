//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_TEST_NET_TEST_H
#define UMM_TEST_NET_TEST_H


#include <ebbrt/IOBuf.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/NetTcpHandler.h>

#include <Umm.h>

class TcpSession : public ebbrt::TcpHandler {
public:
  TcpSession(ebbrt::NetworkManager::TcpPcb pcb)
      : ebbrt::TcpHandler(std::move(pcb)){ebbrt::kprintf_force("App TCP connecting...\n");}
  void Close() { ebbrt::kprintf_force("App TCP closed.\n");}
  void Connected() override;
  void Abort() { ebbrt::kprintf_force("App TCP abort.\n");}
  void Receive(std::unique_ptr<ebbrt::MutIOBuf> b);

private:
  std::unique_ptr<ebbrt::MutIOBuf> buf_;
  ebbrt::NetworkManager::TcpPcb pcb_;
};

#endif
