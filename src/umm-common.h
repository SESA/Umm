//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_COMMON_H_
#define UMM_COMMON_H_

/** umm-common.h
 *  Common definitions for all Umm classes   
 */

#define UMM_USR_REGION_SIZE 1<<28 
#define UMM_REGION_PAGE_ORDER 0  //  2^i pages

#include <cstdint>
#include <list>   // region list
#include <memory> // std::unique_ptr
#include <cinttypes> // PRIx64 

#include <ebbrt/Debug.h>
#include <ebbrt/native/Idt.h>
#include <ebbrt/native/Pfn.h>
#include <ebbrt/native/VMem.h>

// EbbRT type aliases
#define kbugon ebbrt::kbugon
#define kprintf ebbrt::kprintf
using ebbrt::kprintf_force;
using ebbrt::kabort;
using ebbrt::Pfn;
using ebbrt::pmem::kPageSize;
using ebbrt::idt::ExceptionFrame;


static inline void PrintExceptionFrame(ebbrt::idt::ExceptionFrame* ef) {
  kprintf_force("SS: %#018" PRIx64 " RSP: %#018" PRIx64 "\n", ef->ss,
                       ef->rsp);
  kprintf_force("FLAGS: %#018" PRIx64 "\n",
                       ef->rflags);  // TODO(Dschatz): print out actual meaning
  kprintf_force("CS: %#018" PRIx64 " RIP: %#018" PRIx64 "\n", ef->cs,
                       ef->rip);
  kprintf_force("Error Code: %" PRIx64 "\n", ef->error_code);
  kprintf_force("RAX: %#018" PRIx64 " RBX: %#018" PRIx64 "\n", ef->rax,
                       ef->rbx);
  kprintf_force("RCX: %#018" PRIx64 " RDX: %#018" PRIx64 "\n", ef->rcx,
                       ef->rdx);
  kprintf_force("RSI: %#018" PRIx64 " RDI: %#018" PRIx64 "\n", ef->rsi,
                       ef->rdi);
  kprintf_force("RBP: %#018" PRIx64 " R8:  %#018" PRIx64 "\n", ef->rbp,
                       ef->r8);
  kprintf_force("R9:  %#018" PRIx64 " R10: %#018" PRIx64 "\n", ef->r9,
                       ef->r10);
  kprintf_force("R11: %#018" PRIx64 " R12: %#018" PRIx64 "\n", ef->r11,
                       ef->r12);
  kprintf_force("R13: %#018" PRIx64 " R14: %#018" PRIx64 "\n", ef->r13,
                       ef->r14);
  kprintf_force("R15: %#018" PRIx64 "\n", ef->r15);
}

// Some colors for kprintf
#define RESET "\033[0m"
#define RED "\033[31m"     /* Red */
#define GREEN "\033[32m"   /* Green */
#define YELLOW "\033[33m"  /* Yellow */
#define BLUE "\033[34m"    /* Blue */
#define MAGENTA "\033[35m" /* Magenta */
#define CYAN "\033[36m"    /* Cyan */

#endif // UMM_COMMON_H_
