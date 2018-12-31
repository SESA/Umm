#include "UmManager.h"
#include "UmSyscall.h"
#include <ebbrt/native/Cpu.h>
#include "umm-internal.h"
void (*sys_calls[11])(volatile void *) = {
                                          NULL,
                                          solo5_hypercall_walltime,
                                          solo5_hypercall_puts,
                                          solo5_hypercall_poll,
                                          solo5_hypercall_blkinfo,
                                          solo5_hypercall_blkwrite,
                                          solo5_hypercall_blkread,
                                          solo5_hypercall_netinfo,
                                          solo5_hypercall_netwrite,
                                          solo5_hypercall_netread,
                                          solo5_hypercall_halt};

const char *hypercall_names[11]{"NULL",
                              "solo5_hypercall_walltime",
                              "solo5_hypercall_puts",
                              "solo5_hypercall_poll",
                              "solo5_hypercall_blkinfo",
                              "solo5_hypercall_blkwrite",
                              "solo5_hypercall_blkread",
                              "solo5_hypercall_netinfo",
                              "solo5_hypercall_netwrite",
                              "solo5_hypercall_netread",
                              "solo5_hypercall_halt"};
uintptr_t hackRSP;
extern "C" {
  void sys_call_handler(int n, void *arg) {
    // Do a stack switch, then vector off the hypercall.

    // We come in on the user fn stack.
    asm volatile("movq %%rsp, %0;" : "=r"(hackRSP) : :);

    {
      // Get kern stack
      uintptr_t kern_rsp = umm::manager->GetCallerStack();

      uintptr_t aligned_kern_rsp = kern_rsp & ~0xF; // force 16 byte alignment

      // Swizzle onto kern stack
      __asm__ __volatile__("movq %0, %%rsp" ::"r"(aligned_kern_rsp));

    }

    // kprintf("\t%s, arg: %p\n", hypercall_names[n], arg);
    // kprintf(CYAN "User stack at %p\n" RESET, hackRSP);
    sys_calls[n](arg);

    // Swizzle back to user.
    __asm__ __volatile__("mov %0, %%rsp" ::"r"(hackRSP));
  }
}

namespace umm {
namespace syscall {

// Stollen from Barrelfish.
union segment_descriptor {
  uint64_t raw;
  struct {
    uint64_t lo_limit        : 16;
    uint64_t lo_base         : 24;
    uint64_t type            : 4;
    uint64_t system_desc     : 1;
    uint64_t privilege_level : 2;
    uint64_t present         : 1;
    uint64_t hi_limit        : 4;
    uint64_t available       : 1;
    uint64_t long_mode       : 1;
    uint64_t operation_size  : 1;
    uint64_t granularity     : 1;
    uint64_t hi_base         : 8;
  } d;
};

void printBits(uint64_t val, int len) {
  if (len < 0)
    return;
  for (len = len - 1; len > 0; len--) {
    if (val & (1ULL << len))
      printf(RED "1 " RESET);
    else
      printf("0 ");
  }
  if (val & (1ULL << len))
    printf(RED "1" RESET);
  else
    printf("0");
}

void printGDTEnt(int offset) {
  kassert(offset >= 0);
  if (offset == 0)
    printf(RED "Why are you printing the null entry?\n" RESET);

  ebbrt::Cpu& c = ebbrt::Cpu::GetMine();
  uint64_t *gdt_ptr = (uint64_t *)c.gdt();

  printf(MAGENTA "Printing GDT[%d]:\n" RESET, offset);

  segment_descriptor *dp;
  dp = (segment_descriptor *) (gdt_ptr + offset);

  printf("|");
  printf("hi_base        "); printf("|");
  printf("g"); printf("|");
  printf("s"); printf("|");
  printf("l"); printf("|");
  printf("a"); printf("|");
  printf("hi_lim "); printf("|");
  printf("p"); printf("|");
  printf("prv"); printf("|");
  printf("d"); printf("|");
  printf("type   "); printf("|");
  printf("lo_base                                        "); printf("|");
  printf("lo_limit                       "); printf("|");

  printf(" \n");

  printf("|");
  printBits(dp->d.hi_base, 8); printf("|");
  printBits(dp->d.granularity, 1); printf("|");
  printBits(dp->d.operation_size, 1); printf("|");
  printBits(dp->d.long_mode, 1); printf("|");
  printBits(dp->d.available, 1); printf("|");
  printBits(dp->d.hi_limit, 4); printf("|");
  printBits(dp->d.present, 1); printf("|");
  printBits(dp->d.privilege_level, 2); printf("|");
  printBits(dp->d.system_desc, 1); printf("|");
  printBits(dp->d.type, 4); printf("|");
  printBits(dp->d.lo_base, 24); printf("|");
  printBits(dp->d.lo_limit, 16); printf("|");

  printf("\n");

}

void addUserSegments(){
  // Instrument GDT with user segments.
  // Stollen from Barrelfish.

  ebbrt::Cpu& c = ebbrt::Cpu::GetMine();
  uint64_t *gdt_ptr = (uint64_t *)c.gdt();

  // segment_descriptor *cs;
  // {
  //   // Configure User stack Segment.
  //   cs = (segment_descriptor *) gdt_ptr+1;

  //   cs->raw = 0;

  //   cs->d.lo_limit = 0;
  //   cs->d.lo_base = 0;
  //   cs->d.type = 0xa;
  //   cs->d.system_desc = 1;
  //   cs->d.privilege_level = 0;
  //   cs->d.present = 1;
  //   cs->d.hi_limit = 0;
  //   cs->d.available = 0;
  //   cs->d.long_mode = 1;
  //   cs->d.operation_size = 0;
  //   cs->d.granularity = 0;
  //   cs->d.hi_base = 0;
  // }

  // segment_descriptor *ds;
  // {
  //   // Configure User stack Segment.
  //   ds = (segment_descriptor *) gdt_ptr+2;

  //   ds->raw = 0;

  //   ds->d.lo_limit = 0;
  //   ds->d.lo_base = 0;
  //   ds->d.type = 2;
  //   ds->d.system_desc = 1;
  //   ds->d.privilege_level = 0;
  //   ds->d.present = 1;
  //   ds->d.hi_limit = 0;
  //   ds->d.available = 0;
  //   ds->d.long_mode = 1;
  //   ds->d.operation_size = 0;
  //   ds->d.granularity = 0;
  //   ds->d.hi_base = 0;
  // }

  segment_descriptor *uss;
  {
    // Configure User stack Segment.
    uss = (segment_descriptor *) gdt_ptr+3;

    uss->raw = 0;

    uss->d.lo_limit = 0;
    uss->d.lo_base = 0;
    uss->d.type = 2;
    uss->d.system_desc = 1;
    uss->d.privilege_level = 3;
    uss->d.present = 1;
    uss->d.hi_limit = 0;
    uss->d.available = 0;
    uss->d.long_mode = 1;
    uss->d.operation_size = 0;
    uss->d.granularity = 0;
    uss->d.hi_base = 0;
  }

  segment_descriptor *ucs;
  {
    // Configure User code segment.
    ucs = (segment_descriptor *) (gdt_ptr + 4);

    ucs->raw = 0;

    ucs->d.lo_limit = 0;
    ucs->d.lo_base = 0;
    ucs->d.type = 0xa;
    ucs->d.system_desc = 1;
    ucs->d.privilege_level = 3;
    ucs->d.present = 1;
    ucs->d.hi_limit = 0;
    ucs->d.available = 0;
    ucs->d.long_mode = 1;
    ucs->d.operation_size = 0;
    ucs->d.granularity = 0;
    ucs->d.hi_base = 0;
  }

  printGDTEnt(1);
  printGDTEnt(2);
  printGDTEnt(3);
  printGDTEnt(4);
  printGDTEnt(5);
  printGDTEnt(6);

}
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
  // TODO config MASK.

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
    hi = hi | ( (1 << 3) | 0);

    printf(RED "Setting sysret cs. hi is %#x\n" RESET, hi);
    cpuSetMSR(IA32_STAR_MSR, hi, lo);

    cpuGetMSR(IA32_STAR_MSR, &hi, &lo);
    printf(RED "New MSR val hi is %#x\n" RESET, hi);
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

      // Shift by 3 passes the two CPL bits and the L/G selector.
      // Assume first 5 GDT entries looks like.
      // NOTE: sysretq semantics require USR DATA to come immediately before
      // USR CODE. Refer to the Operation section of the manual entry for sysret.
      // ======================================================
      // | NULL | Kern Code | Kern Data | USR DATA | USR CODE |
      // ======================================================

      // NOTE: Because (per the sysret manual entry)
      // CS.Selector <- CS.Selector ← IA32_STAR[63:48]+16
      // SS.Selector <- CS.Selector ← IA32_STAR[63:48]+8
      // The value we're writing into these 16 bits is
      // ==============================================================
      // | USR DATA offset - 1 | Local vs Global | Current priv level |
      // ==============================================================
      // OR
      // =================================
      // |0 0 0 0 0 0 0 0 0 0 0 1 0|0|1 1|
      // =================================

      // NOTE: the way intel does this appears to be a bit insane.
      // On sysretq, CS gets loaded with the desc one after what you write here.
      // On sysretq, SS gets loaded with the desc 2 after what you write here.
      // If this makes any sense to you, send me an email: tunger10@gmail.com
      hi = hi | ((2 << 3) << 16);
      // Write 3 into bits 48 and 49.
      hi = hi | (3 << 16);

      printf(CYAN "Setting sysret cs. hi is %#x\n" RESET, hi);
      cpuSetMSR(IA32_STAR_MSR, hi, lo);

      cpuGetMSR(IA32_STAR_MSR, &hi, &lo);
      printf(CYAN "New MSR val hi is %#x\n" RESET, hi);

  }

  void enableSyscallSysret(){
    // Configure cpu to use syscall / sysret ins.
    enableCPUSyscallSysret();


    // from umm-solo5.h
    configureSupSegments64((uintptr_t)syscall_path);

    configureUserSegments64();
  }

}; // namespace syscall
}; // namespace umm
