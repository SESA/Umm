#ifndef UMM_UM_SYSCALL_H_
#define UMM_UM_SYSCALL_H_

namespace umm {

namespace syscall {

void trigger_syscall();
void trigger_sysret();
void enableSyscallSysret();
}; // namespace syscalls
}; // namespace umm

#endif // UMM_UM_MANAGER_H_
