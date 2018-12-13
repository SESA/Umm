#include "umm-solo5.h"
#include "UmSyscall.h"

void cpuGetMSR(uint32_t msr, uint32_t *hi, uint32_t *lo) {
  asm volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

void cpuSetMSR(uint32_t msr, uint32_t hi, uint32_t lo) {
  asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

void enableCPUSyscallSysret(){
  // I dunno where to do this, but we gotta make sure each cpu is configured.
  // printf("Need IA32_EFER.SCE to be 1\n");
  uint32_t IA32_EFER_MSR, lo, hi;
  IA32_EFER_MSR = 0xC0000080;

  // Want to preserve syscall portion
  // printf("About to get EFER msr %#x\n", IA32_EFER_MSR);
  cpuGetMSR(IA32_EFER_MSR, &hi, &lo);

  // printf("IA32_EFER.SCE is %x\n", lo & 1);

  // Syscall Sysret extension enabled?
  if((lo & 1) == 0) {
    // printf("About to set EFER.SCE msr %#x\n", IA32_EFER_MSR);
    cpuSetMSR(IA32_EFER_MSR, hi, lo | 1);

    cpuGetMSR(IA32_EFER_MSR, &hi, &lo);
    // printf("After set, IA32_EFER.SCE is %x\n", lo & 1);
  }
}

void configureSupSegments64(uintptr_t syscallHandler) {
  // This code allows us to syscall to supervisor.

  {
    // Faking a CS val.
    uint32_t IA32_STAR_MSR, lo, hi;
    IA32_STAR_MSR = 0xC0000081;

    // Want to preserve syscall portion
    cpuGetMSR(IA32_STAR_MSR, &hi, &lo);

    // Low portion reserved.
    lo = 0;

    // Preserve SYSRET cs reg.
    hi = hi & (0xFFFF << 16);

    // Write fake value, 4, into SYSCALL reg.
    hi = hi | 4;

    cpuSetMSR(IA32_STAR_MSR, hi, lo);
  }

  {
    // This provides RIP on syscall into supervisor.
    uint32_t IA32_LSTAR_MSR, lo, hi;
    IA32_LSTAR_MSR = 0xC0000082;

    // Low 32 bits.
    lo = syscallHandler & 0xFFFFFFFF;
    hi = syscallHandler >> 32;

    cpuSetMSR(IA32_LSTAR_MSR, hi, lo);
  }
}
  void configureUserSegments64() {
      // This allows us to sysret into user.
      // Faking a CS val.
      uint32_t IA32_STAR_MSR, lo, hi;
      IA32_STAR_MSR = 0xC0000081;

      // Want to preserve syscall portion
      cpuGetMSR(IA32_STAR_MSR, &hi, &lo);

      // Low portion reserved.
      lo = 0;

      // Preserve Syscall cs reg.
      hi = hi & 0xFFFF;

      // Write fake value, 4, into sysret reg.
      hi = hi | (4 << 16);

      cpuSetMSR(IA32_STAR_MSR, hi, lo);

  }

  void enableSyscallSysret(){
    // Configure cpu to use syscall / sysret ins.
    enableCPUSyscallSysret();

    // configureSupSegments64((uintptr_t)supState.code);

    // from umm-solo5.h
    configureSupSegments64(call_handler);

    configureUserSegments64();
  }

  void trigger_sysret();

  void trigger_syscall(){
    // printf("About to syscall\n");

    {
      // Load ef with fn reg state.
      ExceptionFrame ef;

      asm ("fxsave %0"     :: "m"(ef.fpu));              // Save fpu etc.
      asm ("mov %%r15, %0" : "=r"(ef.r15));
      asm ("mov %%r14, %0" : "=r"(ef.r14));
      asm ("mov %%r13, %0" : "=r"(ef.r13));
      asm ("mov %%r12, %0" : "=r"(ef.r12));
      asm ("mov %%r11, %0" : "=r"(ef.r11));
      asm ("mov %%r10, %0" : "=r"(ef.r10));
      asm ("mov %%r9 , %0" : "=r"(ef.r9));
      asm ("mov %%r8 , %0" : "=r"(ef.r8));
      asm ("mov %%rbp, %0" : "=r"(ef.rbp));
      asm ("mov %%rdi, %0" : "=r"(ef.rdi));
      asm ("mov %%rsi, %0" : "=r"(ef.rsi));
      asm ("mov %%rdx, %0" : "=r"(ef.rdx));
      asm ("mov %%rcx, %0" : "=r"(ef.rcx));
      asm ("mov %%rbx, %0" : "=r"(ef.rbx));
      asm ("mov %%rax, %0" : "=r"(ef.rax));
      ef.error_code = 0;                                 // TODO: IDK
      asm ("mov %%rsp, %0" : "=r"(ef.rsp));              // Current fn sp.
      asm ("mov %%ss , %0" : "=r"(ef.ss));
      ef.rip = (uint64_t) &&done;                        // After syscall???
      asm ("mov %%cs, %0" : "=r"(ef.cs));
      asm ("pushfq"); asm ("popq %0" : "=r"(ef.rflags)); // Push & Pop

      // Pass ef as 1st arg.
      asm ("mov %0, %%rdi" :: "r"(&ef));
    }

    asm ("syscall");  // this needs to execute process_checkpoint
  done:
    ;
  }


void trigger_sysret(){
    printf("About to sysret\n");
    // Restore fn reg state.
      // Load ef with fn reg state.
      ExceptionFrame ef;

      asm ("fxsave %0"     :: "m"(ef.fpu)); // Save fpu etc.
      asm ("mov %%r15, %0" : "=r"(ef.r15));
      asm ("mov %%r14, %0" : "=r"(ef.r14));
      asm ("mov %%r13, %0" : "=r"(ef.r13));
      asm ("mov %%r12, %0" : "=r"(ef.r12));
      asm ("mov %%r11, %0" : "=r"(ef.r11));
      asm ("mov %%r10, %0" : "=r"(ef.r10));
      asm ("mov %%r9 , %0" : "=r"(ef.r9));
      asm ("mov %%r8 , %0" : "=r"(ef.r8));
      asm ("mov %%rbp, %0" : "=r"(ef.rbp));
      asm ("mov %%rdi, %0" : "=r"(ef.rdi));
      asm ("mov %%rsi, %0" : "=r"(ef.rsi));
      asm ("mov %%rdx, %0" : "=r"(ef.rdx));
      asm ("mov %%rcx, %0" : "=r"(ef.rcx));
      asm ("mov %%rbx, %0" : "=r"(ef.rbx));
      asm ("mov %%rax, %0" : "=r"(ef.rax));
      ef.error_code = 0;                    // TODO: IDK
      asm ("mov %%rsp, %0" : "=r"(ef.rsp)); // Current fn sp.
      asm ("mov %%ss , %0" : "=r"(ef.ss));
      ef.rip = (uint64_t) &&done;           // After syscall???
      asm ("mov %%cs, %0" : "=r"(ef.cs));
      asm ("pushfq"); asm ("popq %0" : "=r"(ef.rflags)); // Push & Pop

    // TODO: These registers are still probably messed up.
    int db=1; while(db);
    umm::manager->process_gateway(&ef);

    // Back and have umi ef.

    // SYSRET loads the CS and SS selectors with values derived from bits 63:48
    // of the IA32_STAR MSR
    // TODO: software swings RSP.
    asm volatile("mov %0, %%rsp" ::"r"(ef.rsp));

    // uint64_t rflags = 0x46; // Read using GDB  // HACK: This is bs.

    // Flags loaded using r11.
    asm ("mov %0, %%r11" ::"r"(ef.rflags));
    // RIP loadef from RCX
    asm ("mov %0, %%rcx" ::"r"(ef.rip));

    asm ("sysretq"); // this needs to execute process_checkpoint.
  done:
    ;
  }

void call_handler(int arg, void *input) {
  uint64_t rip;
  asm volatile("mov %%rcx, %0;" : "=r"(rip) : :);

  printf("In call_handler with arg #%d, input %p \n", arg, input);

  sys_calls[arg](input);

  __asm__ __volatile__("mov %0, %%rcx" ::"r"(rip));
  __asm__ __volatile__("sysretq");
}
