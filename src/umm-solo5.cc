
#include "umm-solo5.h"

#include "UmProxy.h"

/*
 * Block until timeout_nsecs have passed or I/O is
 * possible, whichever is sooner. Returns 1 if I/O is possible, otherwise 0.
 */
int solo5_hypercall_poll(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_poll *)arg;
  arg_->ret = 0;
  umm::manager->Block(arg_->timeout_nsecs);
  // return from block

  if(umm::proxy->UmHasData()){
    arg_->ret = 1;
  }
  return 0;
}

int solo5_hypercall_netinfo(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_netinfo *)arg;
  auto ma = umm::UmProxy::client_internal_macaddr();
  arg_->mac_address[0] = ma[0];
  arg_->mac_address[1] = ma[1];
  arg_->mac_address[2] = ma[2];
  arg_->mac_address[3] = ma[3];
  arg_->mac_address[4] = ma[4];
  arg_->mac_address[5] = ma[5];
  return 0;
}

/* UKVM_HYPERCALL_NETWRITE 
struct ukvm_netwrite {
    //IN 
    UKVM_GUEST_PTR(const void *) data;
    size_t len;

    //OUT
    int ret; // amount written 
}; */
int solo5_hypercall_netwrite(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_netwrite *)arg;
  arg_->ret = arg_->len; // confirm the full amount will be sent
  void *buf = malloc(arg_->len);
  memcpy((void *)buf, arg_->data, arg_->len);
  unsigned long len = arg_->len;
  ebbrt::event_manager->SpawnLocal(
      [buf, len]() {
        umm::proxy->ProcessOutgoing(umm::UmProxy::raw_to_iobuf(buf, len));
      },
      true);
  return 0;
}

/* UKVM_HYPERCALL_NETREAD 
struct ukvm_netread {
    // IN 
    UKVM_GUEST_PTR(void *) data;

    // IN/OUT
    size_t len; // amount read

    // OUT
    int ret; // 0=OK
}; */
int solo5_hypercall_netread(volatile void *arg) {
  auto arg_ = (volatile struct ukvm_netread *)arg;
  arg_->len = umm::proxy->UmRead(arg_->data, arg_->len);
  // ret is 0 on successful read, 1 otherwise
  arg_->ret = (arg_->len > 0) ? 0 : 1;
  return 0;
}
