//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "../../src/UmPgTblMgr.h"
#include "../../src/UmPgTblMgr.h"
#include <UmPgTblMgr.h>
#include <Umm.h>

#include <stdio.h>

#include <ebbrt/native/PageAllocator.h>
#include <ebbrt/native/Pfn.h>

// 32 bit version
// #define USE_SYSENTER_SYSEXIT

#define wrmsr(msr, val1, val2)                                                 \
  __asm__ __volatile__("wrmsr"                                                 \
                       : /* no outputs */                                      \
                       : "c"(msr), "a"(val1), "d"(val2))

using namespace umm;

void cpuGetMSR(uint32_t msr, uint32_t *hi, uint32_t *lo) {
  asm volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

void cpuSetMSR(uint32_t msr, uint32_t hi, uint32_t lo) {
  asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

void userFn() {
  int a, b, c;
  a = 5;
  b = 7;
  c = a ^ b;


#ifdef USE_SYSENTER_SYSEXIT
  __asm__ __volatile__("sysenter");
#else
  // call foo
  int arr[3];
  arr[0]=5;
  __asm__ __volatile__("mov %0, %%edi" ::"r"(0x0));
  __asm__ __volatile__("movq %0, %%rsi" ::"r"(&arr[0]));
  __asm__ __volatile__("syscall");

  arr[1]=6;
  __asm__ __volatile__("mov %0, %%edi" ::"r"(0x1));
  __asm__ __volatile__("movq %0, %%rsi" ::"r"(&arr[1]));
  __asm__ __volatile__("syscall");

  arr[2]=7;
  __asm__ __volatile__("mov %0, %%edi" ::"r"(0x2));
  __asm__ __volatile__("movq %0, %%rsi" ::"r"(&arr[2]));
  __asm__ __volatile__("syscall");
#endif


  printf("I don't really run %d \n", c);
}

void call_foo(void *input) {
  printf(YELLOW "Hi from foo, input is %d\n" RESET, *(int *)input);
}

void call_bar(void *input) {
  printf(YELLOW "Hi from bar, input is %d\n" RESET, *(int *)input);
}

void call_exit(void *input) {
  printf(RED "Hi from exit, input is %d\n" RESET, *(int *)input);
  while(1);
}

void (*system_calls[3])(void *) = {call_foo, call_bar, call_exit};

void my_call_handler(int arg, void *input) {
  uint64_t rip;
  asm volatile("mov %%rcx, %0;" : "=r"(rip) : :);

  printf("In call_handler with arg #%d, input %p \n", arg, input);

  system_calls[arg](input);

  __asm__ __volatile__("mov %0, %%rcx" ::"r"(rip));
  __asm__ __volatile__("sysretq");
}

void configureUserSegments32() {
  // Setup CS and SS segment registers.
  // Using bogus value 8 to avoid GPFault.
  uint32_t msr, lo, hi;
  msr = 0x174;
  lo = 4;
  hi = 0;

  printf("setting CS: lo:%x hi:%x \n", lo, hi);
  cpuSetMSR(msr, hi, lo);
}

struct flowState {
  void *stack;
  void *code;
};

void configureSupSegments32(flowState supState) {
  {
    // This code allows us to get back to supervisor.

    // Setup CS and SS segment registers.
    // Using bogus value 8 to avoid GPFault.
    uint32_t SYSENTER_CS_MSR, hi, lo;
    SYSENTER_CS_MSR = 0x174;
    hi = 0;
    lo = 8;

    printf("setting CS: lo:%x hi:%x \n", lo, hi);
    cpuSetMSR(SYSENTER_CS_MSR, hi, lo);
  }

  {
    // Configure enter RSP.
    uint32_t SYSENTER_RSP_MSR, hi, lo;
    SYSENTER_RSP_MSR = 0x175;

    uintptr_t stackPtr = (uintptr_t)supState.stack;
    uint32_t fourBytesSet = ~0;
    hi = (stackPtr >> 32) & fourBytesSet;
    lo = stackPtr & fourBytesSet;
    cpuSetMSR(SYSENTER_RSP_MSR, hi, lo);
  }

  {
    // Configure RIP
    uint32_t SYSENTER_RIP_MSR, hi, lo;
    SYSENTER_RIP_MSR = 0x176;

    uintptr_t insPtr = (uintptr_t)supState.code;
    uint32_t fourBytesSet = ~0;
    hi = (insPtr >> 32) & fourBytesSet;
    lo = insPtr & fourBytesSet;
    cpuSetMSR(SYSENTER_RIP_MSR, hi, lo);
  }
}

void configureSupSegments64(uintptr_t fnAddr) {
  // This code allows us to syscall to supervisor.
  {
    // Faking a CS val.
    uint32_t IA32_STAR_MSR, lo, hi;
    IA32_STAR_MSR = 0xC0000081;

    // Want to preserve syscall portion
    printf("About to get msr %#x\n", IA32_STAR_MSR);
    cpuGetMSR(IA32_STAR_MSR, &hi, &lo);
    printf("Got msr %x, val hi:%#x, lo:%#x\n", IA32_STAR_MSR, hi, lo);

    // Low portion reserved.
    lo = 0;

    // Preserve SYSRET cs reg.
    hi = hi & (0xFFFF << 16);

    // Write fake value, 4, into SYSCALL reg.
    hi = hi | 4;

    printf("setting CS: lo:%x hi:%x \n", lo, hi);
    cpuSetMSR(IA32_STAR_MSR, hi, lo);
  }


  {
    // This allows us to syscall into supervisor.
    uint32_t IA32_LSTAR_MSR, lo, hi;
    IA32_LSTAR_MSR = 0xC0000082;

    // Low 32 bits.
    lo = fnAddr & 0xFFFFFFFF;
    hi = fnAddr >> 32;

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

    printf("setting CS: lo:%x hi:%x \n", lo, hi);
    cpuSetMSR(IA32_STAR_MSR, hi, lo);
}

// void configureSupSegments64(flowState supState) {
  // SYSCALL invokes an OS system-call handler at privilege level 0. It does so
  // by loading RIP from the IA32_LSTAR MSR (after saving the address of the
  // instruction following SYSCALL into RCX)

  // SYSCALL also saves RFLAGS into R11 and then masks RFLAGS using the
  // IA32_FMASK MSR (MSR address C0000084H); specifically, the processor clears
  // in RFLAGS every bit corresponding to a bit that is set in the IA32_FMASK
  // MSR.

  // SYSCALL loads the CS and SS selectors with values derived from bits 47:32
  // of the IA32_STAR MSR.

  // The SYSCALL instruction does not save the stack pointer (RSP).
// }

uintptr_t getUserPage() {
  // Get page.
  auto page = ebbrt::page_allocator->Alloc();
  auto page_addr = page.ToAddr();

  lin_addr la;
  la.raw = page_addr;
  // Mark table entry and all higher user executable.
  // NOTE: this suffers a tlb flush, probably overkill.
  UmPgTblMgmt::setUserAllPTEsWalkLamb(la, UmPgTblMgmt::getPML4Root(),
                                      PML4_LEVEL);
  // UmPgTblMgmt::dumpAllPTEsWalkLamb(la, UmPgTblMgmt::getPML4Root(),
  // PML4_LEVEL);

  return page_addr;
}

void getUserState(flowState *us) {
  printf(YELLOW "Hi from %s\n" RESET, __func__);

  {
    // Alloc a page for stack.
    auto page_addr = getUserPage();

    // Guessing go to next page and 8 byte align backward?
    us->stack = (void *)(page_addr + (1 << 12) - 8);
    printf("User state stack is %p\n", us->stack);
  }

  {
    // Alloc a page for code.
    auto page_addr = getUserPage();
    us->code = (void *)page_addr;

    // Copy over from existing code for userFn
    // Size determined by objdump.
    memcpy((void *)page_addr, (void *)userFn, 1<<10);
    printf("User state code is %p\n", us->code);
  }
}

void doubleTransition() {
  // Transition sup -> usr -> sup. Then return.
  // Have to preconfigure return to supervisor.
  // Configure user control flow & go.

  printf(YELLOW "Hi from %s\n" RESET, __func__);


  {
    // For getting back to supervisor state.
    flowState supState;
    uint64_t rsp;
    asm volatile("mov %%rsp, %0;" : "=r"(rsp) : :);
    supState.stack = (void *)rsp;
    supState.code = (void *)my_call_handler;

#ifdef USE_SYSENTER_SYSEXIT
    configureSupSegments32(supState);
    // We just fake the segment state out.
    configureUserSegments32();
#else
    // We just fake the segment state out.
    configureSupSegments64((uintptr_t)supState.code);
    configureUserSegments64();
    // segmentHack64();
#endif
  }

  flowState userState;
  getUserState(&userState);

  {

// Switch to usermode.
#ifdef USE_SYSENTER_SYSEXIT
    // Transfer to user mode.
    // Setup RCX addr loaded to RSP.
    __asm__ __volatile__("mov %0, %%rcx" ::"r"(userState.stack));

    // RDX addr loaded to RIP.
    __asm__ __volatile__("mov %0, %%rdx" ::"r"(userState.code));
    __asm__ __volatile__("sysexit");
#else
    // SYSRET loads the CS and SS selectors with values derived from bits 63:48
    // of the IA32_STAR MSR
    // TODO: software swings RSP.
    __asm__ __volatile__("mov %0, %%rsp" ::"r"(userState.stack));
    uint64_t rflags = 0x46; // Read using GDB
    // Flags loaded using r11.
    __asm__ __volatile__("mov %0, %%r11" ::"r"(rflags));
    // RIP loadef from RCX
    __asm__ __volatile__("mov %0, %%rcx" ::"r"(userState.code));
    __asm__ __volatile__("sysretq");
#endif
  }

// mylabel:
  // Return back to supervisor mode here.
  // Aren't the registers going to be all jacked up?

  printf(MAGENTA "Back executing system code, & it feels so good.\n" RESET);
}

void checkSetup(){
  printf("Need IA32_EFER.SCE to be 1\n");
  uint32_t IA32_EFER_MSR, lo, hi;
  IA32_EFER_MSR = 0xC0000080;

  // Want to preserve syscall portion
  printf("About to get EFER msr %#x\n", IA32_EFER_MSR);
  cpuGetMSR(IA32_EFER_MSR, &hi, &lo);

  printf("IA32_EFER.SCE is %x\n", lo & 1);

  // Syscall Sysret extension enabled?
  if((lo & 1) == 0) {
    printf("About to set EFER.SCE msr %#x\n", IA32_EFER_MSR);
    cpuSetMSR(IA32_EFER_MSR, hi, lo | 1);

    cpuGetMSR(IA32_EFER_MSR, &hi, &lo);
    printf("After set, IA32_EFER.SCE is %x\n", lo & 1);

  }

}

void AppMain() {
  printf(YELLOW "Hi from %s\n" RESET, __func__);

  checkSetup();

  printf("Going to try to transition now\n");

  // int db=1; while(db);
  doubleTransition();

  printf("Main going down\n");
}
