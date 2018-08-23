//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <Umm.h>

#include <UmDebugTrapper.h>

void foo0(){kprintf("%s\n", __func__);}
void foo1(){kprintf("%s\n", __func__);}
void foo2(){kprintf("%s\n", __func__);}
void foo3(){kprintf("%s\n", __func__);}

void callFns(){
  foo0();
  foo1();
  foo2();
  foo3();
}

umm::debugTrapper dbT;

int f0(){kprintf(YELLOW "hey from f0\n" RESET); return 0;}
int f1(){kprintf(YELLOW "hey from f1\n" RESET); return 1;}
int f2(){kprintf(YELLOW "hey from f2\n" RESET); return 2;}
int f3(){kprintf(YELLOW "hey from f3\n" RESET); return 3;}

void hardCodedTest(){
  // There should be only one instance of this guy per core.

  // Grab all the handles.
  kassert(dbT.isAvailable() == true); dbHandle dbH0 = dbT.checkoutDBHandle();
  kassert(dbT.isAvailable() == true); dbHandle dbH1 = dbT.checkoutDBHandle();
  kassert(dbT.isAvailable() == true); dbHandle dbH2 = dbT.checkoutDBHandle();
  kassert(dbT.isAvailable() == true); dbHandle dbH3 = dbT.checkoutDBHandle();
  kassert(dbT.isAvailable() == false);

  // Set some breakpoint addrs.
  dbT.setBPAddr(dbH0, (uintptr_t) foo0);
  dbT.setBPAddr(dbH1, (uintptr_t) foo1);
  dbT.setBPAddr(dbH2, (uintptr_t) foo2);
  dbT.setBPAddr(dbH3, (uintptr_t) foo3);
  // kprintf("This barfs\n");
  // dbHandle dbH4 = dbT.checkoutDBHandle();

  dbT.setBPCondition(dbH0, INS);
  dbT.setBPCondition(dbH1, INS);
  dbT.setBPCondition(dbH2, INS);
  dbT.setBPCondition(dbH3, INS);

  dbT.enableExcep(dbH0);
  dbT.enableExcep(dbH1);
  dbT.enableExcep(dbH2);
  dbT.enableExcep(dbH3);

  dbT.registerLambda(dbH0, f0);
  dbT.registerLambda(dbH1, f1);
  dbT.registerLambda(dbH2, f2);
  dbT.registerLambda(dbH3, f3);

  callFns();

  dbT.returnDBHandle(dbH0);
  dbT.returnDBHandle(dbH1);
  dbT.returnDBHandle(dbH2);
  dbT.returnDBHandle(dbH3);

  // kprintf("This barfs\n");
  // dbT.returnDBHANDLE(dbH0);
}

extern "C" void ebbrt::idt::DebugException(ExceptionFrame* ef) {
  kprintf(MAGENTA "DebugException\n" RESET);

  dbT.disambiguateDBExcep(ef);

  // Set resume flag to prevent infinite retriggering of exception
  dbT.setResumeFlag(ef);
}

void AppMain() {
  kprintf(RED "Hello \n" RESET);

  hardCodedTest();

  kprintf(RED "Done \n" RESET);
}
