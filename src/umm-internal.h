//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_INTERNAL_H_
#define UMM_INTERNAL_H_

#include "umm-common.h"

/** umm-internal.h
 *  Internal definitions for all Umm classes   
 *  DO NOT include this from header files
 */

// EbbRT type aliases
#define kbugon ebbrt::kbugon
#define kprintf ebbrt::kprintf
using ebbrt::kprintf_force;
using ebbrt::kabort;
using ebbrt::Pfn;
using ebbrt::pmem::kPageSize;
using ebbrt::idt::ExceptionFrame;

#endif 
