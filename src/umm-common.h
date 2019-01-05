//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_COMMON_H_
#define UMM_COMMON_H_

/** umm-common.h
 *  Common definitions for all Umm classes   
 */

#define UMM_REGION_PAGE_ORDER 0  //  2^i pages

#include <cstdint>
#include <list>   // region list
#include <memory> // std::unique_ptr
#include <cinttypes> // PRIx64 

#include <ebbrt/Clock.h>
#include <ebbrt/Cpu.h>
#include <ebbrt/Debug.h>
#include <ebbrt/Future.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/native/Idt.h>
#include <ebbrt/native/Pfn.h>
#include <ebbrt/native/VMem.h>

// Some colors for kprintf
#define RESET "\033[0m"
#define RED "\033[31m"     /* Red */
#define GREEN "\033[32m"   /* Green */
#define YELLOW "\033[33m"  /* Yellow */
#define BLUE "\033[34m"    /* Blue */
#define MAGENTA "\033[35m" /* Magenta */
#define CYAN "\033[36m"    /* Cyan */

#endif // UMM_COMMON_H_
