#include "UmDebugTrapper.h"
#include "Umm.h"
#include "util/x86_64.h"

using namespace umm;

void debugTrapper::setBPAddr(const dbHandle dbH, const uintptr_t addr) const{
  kassert(checkValid(dbH));
  kprintf("in set, dbH is %d, addr is %p\n", dbH, addr);

  switch(dbH) {
  case 0:
    x86_64::DR0 dr0; dr0.val = addr; dr0.set();
    break;
  case 1:
    x86_64::DR1 dr1; dr1.val = addr; dr1.set();
    break;
  case 2:
    x86_64::DR2 dr2; dr2.val = addr; dr2.set();
    break;
  case 3:
    x86_64::DR3 dr3; dr3.val = addr; dr3.set();
    break;
  default:
    kprintf("Bad handle\n");
    kabort();
  }
}

void debugTrapper::setBPCondition(const dbHandle dbH, uint8_t bpType) const{
  kassert(checkValid(dbH));
  kassert(bpType >= INS && bpType <= WR);

  kprintf("in set condition, dbH is %d, type is %d\n", dbH, bpType);

  x86_64::DR7 dr7;
  dr7.get();

  switch(dbH) {
  case 0:
    // NOTE: Man says set Len when RW is 0. Can't be factored out.
    if(bpType == 0) dr7.L0 = 0;
    dr7.RW0 = bpType;
    break;
  case 1:
    if(bpType == 0) dr7.L1 = 0;
    dr7.RW1 = bpType;
    break;
  case 2:
    if(bpType == 0) dr7.L2 = 0;
    dr7.RW2 = bpType;
    break;
  case 3:
    if(bpType == 0) dr7.L2 = 0;
    dr7.RW2 = bpType;
    break;
  default:
    kprintf("Bad bpType\n");
    kabort();
  }

  // Set that value!
  dr7.set();
}

void debugTrapper::toggleEnable(const dbHandle dbH, const bool enable) const{
  kassert(checkValid(dbH));
  uint8_t bit = enable ? 1 : 0;

  kprintf("in toggle enable, dbH is %d, addr is %s\n", dbH, enable ? "true" : "false");
  x86_64::DR7 dr7;
  dr7.get();

  switch(dbH) {
  case 0:
    dr7.L0 = bit;
    break;
  case 1:
    dr7.L1 = bit;
    break;
  case 2:
    dr7.L2 = bit;
    break;
  case 3:
    dr7.L3 = bit;
    break;
  default:
    kprintf("Bad handle\n");
    kabort();
  }

  // Set that value!
  dr7.set();
}

void debugTrapper::enableExcep(const dbHandle dbH) const{
  kassert(checkValid(dbH));
  toggleEnable(dbH, 1);
}
void debugTrapper::disableExcep(const dbHandle dbH) const{
  kassert(checkValid(dbH));
  toggleEnable(dbH, 1);
}

void debugTrapper::registerLambda(const dbHandle dbH, const std::function<int()> fn){
  kassert(checkValid(dbH));
  functors_vec_[dbH] = fn;
}

dbHandle debugTrapper::checkoutDBHandle() {
  // TODO: This seems stupid, is there a one liner?
  dbHandle ret = handle_list_.front();
  handle_list_.pop_front();
  return ret;
}

void debugTrapper::returnDBHandle(const dbHandle dbH) {
  kassert(checkValid(dbH));
  handle_list_.push_front(dbH);
  functors_vec_.insert(functors_vec_.begin() + dbH, nullFn);
}

void debugTrapper::disambiguateDBExcep(ebbrt::idt::ExceptionFrame* ef) const{
  x86_64::DR6 db6;
  db6.get();

  int i = -1; // An impossible value.

  // Think only one should be set.
  if(db6.B0 == 1){ i = 0;}
  if(db6.B1 == 1){ i = 1;}
  if(db6.B2 == 1){ i = 2;}
  if(db6.B3 == 1){ i = 3;}

  kprintf("DR%d was responsible for this exception\n", i);
  kprintf("Launch lambda %d\n", i);
  functors_vec_[i]();

  kassert(i >= 0);
}

bool debugTrapper::isAvailable() const{
  if(handle_list_.size() > 0)
    return true;
  return false;
}

void debugTrapper::setResumeFlag(ExceptionFrame* ef){
  ef->rflags |= 1 << 16;
}

bool debugTrapper::checkValid(const dbHandle dbH) const{
  // Check user input is in the range of valid handles & not somehow in our available list.

  // Check if in range.
  bool rangeGood = dbH >= 0 && dbH <= 3;

  // Make sure it's not alreadt
  bool found = std::find(handle_list_.begin(), handle_list_.end(), dbH) != handle_list_.end();

  if(!rangeGood)
    kprintf("range no good, %d\n", dbH);

  if(found){
    kprintf("wtf, %d, that's in the list \n", dbH);
    dumpList();
  }

  return rangeGood && !found;
}

void debugTrapper::dumpList() const{
  kprintf("List has %d elems\n", handle_list_.size());
  kprintf("Elems: ");
  for(const auto& i : handle_list_)
    kprintf("%d ", i);
  kprintf("\n");

}
