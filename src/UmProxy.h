//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_PROXY_H_
#define UMM_UM_PROXY_H_

#include <ebbrt/EbbId.h>
#include <ebbrt/GlobalStaticIds.h>
#include <ebbrt/MulticoreEbb.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/NetEth.h>

#include "umm-common.h"

namespace umm {

/**
 *  UmProxy - Ebb that manages per-core network IO of SV instances
 */
class UmProxy : public ebbrt::MulticoreEbb<UmProxy> {
public:
  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("UmProxy");
  /** Class-wide static initialization logic */
  static void Init(); 
  
  ebbrt::EthernetAddress MacAddress();
 
  /** NetWrite
   *  ret: amount written
   */
  uint32_t NetWrite(const void* data, const size_t len);

  /** NetRead
   *  ret: amount read
   */
  uint32_t NetRead(const void* const data, const size_t len);
   
  /** BytesAvailable
   *  ret: number of bytes available to read
   */ 
  uint64_t BytesAvailable();
  

};

constexpr auto proxy = 
    ebbrt::EbbRef<UmProxy>(UmProxy::global_id);
}
#endif // UMM_UM_PROXY_H_
