#include "UmPth.h"

// TODO: const
void umm::UmPth::copyInPages(const umm::simple_pte *srcRoot){
  root_ = UmPgTblMgmt::walkPgTblCopyDirty(const_cast<umm::simple_pte *>(srcRoot), root_, lvl_);
  kassert(root_ != nullptr);
}

void umm::UmPth::printMappedPagesCount() const{
  std::vector<uint64_t> counts(5); // Vec of size 5, zero elements.
  UmPgTblMgmt::countValidPagesLamb(counts, root_, lvl_);
  printf("Valid pages:\n");

    for (int i=4; i>0; i--){
      printf("counts[%s] = %lu\n", level_names[i], counts[i]);
    }
}
