//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_X86_64_H_
#define UMM_X86_64_H_

/** umm-x86_64.h
 *  Architecture specific structures and definitions
 */
namespace umm {
namespace x86_64 {

// DR0
typedef struct {
  uint64_t val;
  void get() { __asm__ __volatile__("mov %%db0, %0" : "=r"(val)); }
  void set() { __asm__ __volatile__("mov %0, %%db0" ::"r"(val)); }
} DR0;

// DR1
typedef struct {
  uint64_t val;
  void get() { __asm__ __volatile__("mov %%db1, %0" : "=r"(val)); }
  void set() { __asm__ __volatile__("mov %0, %%db1" ::"r"(val)); }
} DR1;

// DR2
typedef struct {
  uint64_t val;
  void get() { __asm__ __volatile__("mov %%db2, %0" : "=r"(val)); }
  void set() { __asm__ __volatile__("mov %0, %%db2" ::"r"(val)); }
} DR2;

// DR3
typedef struct {
  uint64_t val;
  void get() { __asm__ __volatile__("mov %%db3, %0" : "=r"(val)); }
  void set() { __asm__ __volatile__("mov %0, %%db3" ::"r"(val)); }
} DR3;

// DR6
typedef struct {
  union {
    uint64_t val;
    struct {
      uint64_t B0 : 1, B1 : 1, B2 : 1, B3 : 1, Res : 9, BD : 1, BS : 1, BT : 1;
    } __attribute__((packed));
  };
  void get() { __asm__ __volatile__("mov %%db6, %0" : "=r"(val)); }
  void set() { __asm__ __volatile__("mov %0, %%db6" ::"r"(val)); }
} DR6;

// DR7
typedef struct {
  union {
    uint64_t val;
    struct {
      uint64_t L0 : 1, G0 : 1, L1 : 1, G1 : 1, L2 : 1, G2 : 1, L3 : 1, G3 : 1,
          LE : 1, GE : 1, : 3, GD : 1, : 2, RW0 : 2, LEN0 : 2, RW1 : 2,
          LEN1 : 2, RW2 : 2, LEN2 : 2, RW3 : 2, LEN3 : 2, : 32;
    } __attribute__((packed));
  };
  void get() { __asm__ __volatile__("mov %%db7, %0" : "=r"(val)); }
  void set() { __asm__ __volatile__("mov %0, %%db7" ::"r"(val)); }
  // V3B 17-9 :
  // 17.2.4 Debug Control Register (DR7)
  enum RW_VALUES { INEXECUTION = 0, DATAWRITE = 1, IORW = 2, DATARW = 3 };
} DR7;
} // namespace x86_64
} // namespace Om

#endif // UMM_X86_64_H_ 
