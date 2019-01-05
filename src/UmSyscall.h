#ifndef UMM_UM_SYSCALL_H_
#define UMM_UM_SYSCALL_H_

#include "UmManager.h"
#include "umm-solo5.h"

extern "C" {
extern void syscall_path();
  void sys_call_handler(int n, void *arg);
}



namespace umm {

namespace syscall {

void addUserSegments();
void trigger_syscall();
void trigger_sysret();
void enableSyscallSysret();
}; // namespace syscalls
}; // namespace umm

#endif // UMM_UM_MANAGER_H_
