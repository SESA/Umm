#ifndef UMM_UM_DEBUGTRAPPER_H_
#define UMM_UM_DEBUGTRAPPER_H_

#include "stdint.h"
#include "list"
#include "vector"
#include "Umm.h"
#include "util/x86_64.h"

typedef uint8_t dbHandle;

enum bpType {
  INS = 0,
  RD,
  WR
};

namespace umm {


class debugTrapper{
public:
  // HACK
  static int nullFn() { return 0; }
  debugTrapper(){
    kprintf(GREEN "dT cons\n");
    for(int i = 0; i < 4; i++)
      functors_vec_.push_back(nullFn);
  }

  void setBPAddr(const dbHandle dbH, uintptr_t addr) const;
  void setBPCondition(const dbHandle dbH, uint8_t bpType) const;
  void enableExcep(const dbHandle dbH) const;
  void disableExcep(const dbHandle dbH) const;

  void registerLambda(const dbHandle dbH, const std::function<int()> fn);

  // void globalDisableExcep() const;
  // void globalEnableExcep() const;

  dbHandle checkoutDBHandle();
  void returnDBHandle(const dbHandle dbH);
  void retrunAllDBHandles();

  void disambiguateDBExcep(ebbrt::idt::ExceptionFrame* ef) const;

  bool isAvailable() const;

  // HACK
  void setResumeFlag(ExceptionFrame* ef);
  void initFuncs();
private:
  void toggleEnable(const dbHandle dbH, const bool enable) const;
  bool checkValid(const dbHandle dbH) const;
  void dumpList() const;
  std::list<dbHandle> handle_list_ = {0,1,2,3};
  std::vector<std::function<int()>> functors_vec_;
};
} // namespace umm

#endif // UMM_UM_DEBUGTRAPPER_H_
