//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <UmPgTblMgr.h>
#include <Umm.h>
#include <ebbrt/native/PageAllocator.h>
#include <ebbrt/Debug.h>
#include <ebbrt/native/Pfn.h>

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

uintptr_t getPage() {
  // Get page.
  auto page = ebbrt::page_allocator->Alloc();
  auto page_addr = page.ToAddr();

  // lin_addr la;
  // la.raw = page_addr;
  // Mark table entry and all higher user executable.
  // NOTE: this suffers a tlb flush, probably overkill.
  // UmPgTblMgmt::setUserAllPTEsWalkLamb(la, UmPgTblMgmt::getPML4Root(),
  //                                     PML4_LEVEL);
  // UmPgTblMgmt::dumpAllPTEsWalkLamb(la, UmPgTblMgmt::getPML4Root(),
  // PML4_LEVEL);

  return page_addr;
}

bool PCIDSupported() {
  bool PCID_SUPPORTED;
  {
    // Query CPUID.01H:ECX.PCID: CR4.PCIDE may be set to 1, enabling
    // process-context identifiers.
    uint32_t ecx;
    asm("movl $0x1, %%eax\n\t"
        "cpuid\n\t"
        "movl %%ecx, %0;"
        : "=r"(ecx)
        :
        :);
    PCID_SUPPORTED = (ecx >> 17) & 0x1;
  }

  if (PCID_SUPPORTED) {
    ebbrt::kprintf_force(GREEN "PCID IS SUPPORTED\n");
    return true;
  } else {
    ebbrt::kprintf_force(RED "PCID IS NOT SUPPORTED\n" RESET);
  }
  return false;
}

void enablePCID(){
  // Set cr4.pcide 0 -> 1.

  // Check cr4.pcide, shouldn't be set.
  {
    uint64_t cr4;
    asm("mov %%cr4, %[cr4]" : [cr4] "=r"(cr4));
    bool PCID_ENABLED = (cr4 >> 17) & 0x1;

    if (PCID_ENABLED) {
      ebbrt::kprintf_force(GREEN "PCID was already enabled\n" RESET);
      return;
    } else {
      ebbrt::kprintf_force(
          YELLOW "PCID not enabled by default, attempting activation\n" RESET);
    }
  }

  // CR3[11:0] must be 0x0.
  {
    uint64_t cr3;
    asm("mov %%cr3, %[cr3]" : [cr3] "=r"(cr3));
    uint64_t lower12Bits = cr3 & ((1 << 12) - 1);

    if (lower12Bits) {
      ebbrt::kprintf_force(RED "cr3 is not correctly configured, figure that out.\n" RESET);

      if (lower12Bits % 8) {
        ebbrt::kprintf_force(YELLOW "Some of the bottom 3 bits are set.\n" RESET);
        ebbrt::kprintf_force(YELLOW "They're ignored, so zero them.\n" RESET);
        asm("movq %0, %%cr3" ::"r"(cr3 & ~(0x7)));

        asm("mov %%cr3, %[cr3]" : [cr3] "=r"(cr3));
        lower12Bits = cr3 & ((1 << 12) - 1);
        if (lower12Bits) {
          ebbrt::kprintf_force(RED "cr3 is still not correctly configured.\n" RESET);
          ebbrt::kabort();
        } else {
          ebbrt::kprintf_force(GREEN "cr3 looks good, continuing\n" RESET);
        }
      }
    } else {
      ebbrt::kprintf_force(GREEN "cr3 looks good, continuing\n" RESET);
    }

    asm("mov %%cr3, %[cr3]" : [cr3] "=r"(cr3));
    ebbrt::kprintf_force(CYAN "cr3 is %#lx\n" RESET, cr3);
  }

  // IA32_EFER.LMA must be 0x1.
  {
    uint32_t IA32_EFER_MSR, lo, hi;
    IA32_EFER_MSR = 0xC0000080;

    // Want to preserve syscall portion
    cpuGetMSR(IA32_EFER_MSR, &hi, &lo);

    uint64_t IA32_EFER = ((uint64_t)hi << 32) | lo;
    ebbrt::kprintf_force(CYAN "IA32_EFER is %#lx\n" RESET, IA32_EFER);

    bool LMA = (lo >> 10) & 0x1;

    if(LMA){
      ebbrt::kprintf_force(GREEN "LMA is g2g\n" RESET);
    }
    else{
      ebbrt::kprintf_force(RED "IA32_EFER.LMA must be set.\n" RESET);
      ebbrt::kabort();
    }
  }

  // Set pcide bit enabled in cr4.
  {
    ebbrt::kprintf_force(YELLOW "About to attempt setting pcide\n" RESET);

    uint64_t cr4;
    asm("mov %%cr4, %[cr4]" : [cr4] "=r"(cr4));
    uint64_t cr4_pcide_enabled = cr4 | 0x1 << 17;
    ebbrt::kprintf_force(YELLOW "cr4 was %#lx\n" RESET, cr4);
    ebbrt::kprintf_force(YELLOW "changing it to %#lx\n" RESET, cr4_pcide_enabled);

    // Write the cr4 register.
    __asm__ __volatile__("movq %0, %%cr4" ::"r"(cr4_pcide_enabled));
    // __asm__ __volatile__("movq %0, %%cr4" ::"r"(cr4));


    // Confirm bit is set.
    asm("mov %%cr4, %[cr4]" : [cr4] "=r"(cr4));
    if((cr4 >> 17) & 0x1 ){
      ebbrt::kprintf_force(GREEN "PCIDE set!\n" RESET);
    }
    else{
      ebbrt::kprintf_force(RED "Failed to set PCIDE\n" RESET);
      ebbrt::kabort();
    }
  }


}


void testAddrSpcSwitchWithoutPCID(simple_pte* otherPT){
  // Alternate between page tables accessing data.

  // Map the page holding this fn into other addr space.
  // Get a page, memcopy this fn onto it.

  ebbrt::kprintf_force(YELLOW "Hi from %s\n" RESET, __func__);
  ebbrt::kprintf_force(YELLOW "This fn starts at %p\n" RESET, testAddrSpcSwitchWithoutPCID);

  register uint64_t cr3;
  asm("mov %%cr3, %[cr3]" : [cr3] "=r"(cr3));
  ebbrt::kprintf_force(GREEN "old cr3 was %#lx\n" RESET, cr3);

  uint64_t ctr = 1ULL << 24;
  otherPT = (simple_pte*)((uint64_t)otherPT |((1<< 12) - 2));
  cr3 = cr3 | 7;
  int db = 1; while(db);
  
  while(ctr--){
    // In ebbrt
    asm("movq %0, %%cr3" ::"r"(otherPT));
    // In other Addr space.
    asm("movq %0, %%cr3" ::"r"(cr3));
  }
}

simple_pte* newPT(uintptr_t virt_addr, uintptr_t phys_addr){
  // Take a physical pg, return a new page table with that page mapped at addr.
  lin_addr virt, phys;
  virt.raw = virt_addr;
  phys.raw = phys_addr;
  ebbrt::kprintf_force(MAGENTA "Phys page at %#lx\n" RESET, phys.raw);
  simple_pte* root = UmPgTblMgmt::mapIntoPgTbl(nullptr, phys, virt, PML4_LEVEL,
                                               TBL_LEVEL, PML4_LEVEL, false);

  uint64_t bottomOfPg = ((uint64_t) testAddrSpcSwitchWithoutPCID) & ~((1<<12) - 1);
  memcpy((void *)phys_addr, (void *) bottomOfPg, 1<<12);
  ebbrt::kprintf_force(CYAN "bottom pg is %#lx\n", bottomOfPg);

  ebbrt::kprintf_force(CYAN "New PT PML4 at %p\n", root);

  return root;
}

void AppMain() {
  ebbrt::kprintf_force(YELLOW "Hi from %s\n" RESET, __func__);

  lin_addr slotLA;
  slotLA.raw = (uint64_t) testAddrSpcSwitchWithoutPCID;

  simple_pte* otherPT = newPT(slotLA.raw, getPage());
  {
    UmPgTblMgmt::dumpAllPTEsWalkLamb(slotLA, otherPT, PML4_LEVEL);
  }

  ebbrt::kprintf_force(YELLOW "Starting Without PCID test\n" RESET);
  testAddrSpcSwitchWithoutPCID(otherPT);
  ebbrt::kprintf_force(GREEN "Done Without PCID test\n" RESET);


  if(PCIDSupported())
    enablePCID();
  else
    ebbrt::kabort();


  ebbrt::kprintf_force(RED "Starting With PCID test\n" RESET);
  testAddrSpcSwitchWithoutPCID(otherPT);
  ebbrt::kprintf_force(GREEN "Done With PCID test\n" RESET);



  ebbrt::kprintf_force(CYAN "Main going down\n");
}
