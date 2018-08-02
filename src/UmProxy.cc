//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmProxy.h"

void umm::UmProxy::Init() {
  // Setup multicore Ebb translation
  Create(UmProxy::global_id);
}

uint32_t umm::UmProxy::NetWrite(const void* data, const size_t len){
  auto eth = (ebbrt::EthernetHeader *)(data);
  auto ethtype = ebbrt::ntohs(eth->type);

  if (ethtype == ebbrt::kEthTypeArp || ethtype == 0x86dd) { // ignore ARP & IPv6  
    // Confirm write, do nothing
    kprintf("UmProxy NetWrite type=0x%x len=%d\n", ethtype, len);
    return len;
  } else {
    kprintf("Error, unsupported EthType: %p 0x%x \n", eth, ethtype);
  }
  return 0; // No data written
}

uint32_t umm::UmProxy::NetRead(const void* data, const size_t len){
  if( ! BytesAvailable() ){
    return 0;
  }
  kprintf("UmProxy NetRead request %p len=%d\n", data, len);
  return 0;
}

uint64_t umm::UmProxy::BytesAvailable(){
  return 0;
}

ebbrt::EthernetAddress umm::UmProxy::MacAddress(){
  return {{0x06, 0xfe, 0x00, 0x00, 0x00, 0x00}};
}
