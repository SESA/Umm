#ifndef UMM_UM_PG_TBL_MGR
#define UMM_UM_PG_TBL_MGR

#include "stdint.h"

#define SMALL_PG_SHIFT 12
#define MED_PG_SHIFT 21
#define LG_PG_SHIFT 30

#define SMALL_PG_BYTES 1UL << SMALL_PG_SHIFT

extern const unsigned char pgShifts[];

#define PML4__ 4
#define PDPT__ 3
#define DIR__  2
#define TAB__  1

#define SLOT_PML4_NUM 0x180

enum Level {
  PML4_LEVEL = 4,
  PDPT_LEVEL = 3,
  PD_LEVEL = 2,
  PT_LEVEL = 1,
  NO_LEVEL = 0
};

enum PgSz {
  _1G__ = 3,
  _2M__ = 2,
  _4K__ = 1
};


extern const char* level_names[];



// class pgTabTool {
// public:
//   simple_pte* linAddrToPTEGetOrCreate(lin_addr la, simple_pte* root = nullptr, unsigned char lvl = 4);
//   simple_pte* linAddrToPTEGetOrCreateHelper(lin_addr la, uint64_t *offsets, simple_pte* root, unsigned char lvl);

//   simple_pte* linAddrToPTE(lin_addr la, simple_pte* root = nullptr, unsigned char lvl = 4);
//   simple_pte* linAddrToPTEHelper(lin_addr la, uint64_t *offsets, simple_pte* root, unsigned char lvl);

//   void countAllocatedPagesHelper(uint64_t *counts, simple_pte *root, unsigned char lvl);
//   void countAllocatedPages();
//   void countDirtyPages();

//   void countAllocatedPagesHelperImp(uint64_t *counts, simple_pte *root, unsigned char lvl);
//   void countAllocatedPagesImp();
// };

namespace umm {

  class phys_addr {
  public:
    union {
      uint64_t raw;
      struct {
        uint64_t OFFSET : 12, FRAME_NUMBER : 40;
      } construct_addr_4K;
      struct {
        uint64_t OFFSET : 21, FRAME_NUMBER : 27;
      } construct_addr_2M;
      struct {
        uint64_t OFFSET : 30, FRAME_NUMBER : 18;
      } construct_addr_1G;
    };
  };
  static_assert(sizeof(phys_addr) == sizeof(uint64_t), "Bad phys_addr SZ");

class lin_addr;
// Simplified page table entry.
class simple_pte {
 public:
  union {
    uint64_t raw;
    struct {
    //   // Ok to grab upper 40 for PN b/c (top reserved 0).
      uint64_t
      WHOCARES : 12,
        PAGE_NUMBER : 40;
    } decomp4K;
    struct {
      uint64_t
      WHOCARES : 21,
        PAGE_NUMBER : 31;
    } decomp2M;
    struct {
      uint64_t
      WHOCARES : 30,
        PAGE_NUMBER : 22;
    } decomp1G;
    struct{
      uint64_t
        SEL : 1,
        RW : 1,
        US : 1,
        PWT: 1,
        PCD: 1,
        A : 1,
        DIRTY: 1,
        MAPS : 1,
        WHOCARES2 : 4,
        PG_TBL_ADDR : 40,
        RES : 12;
    } decompCommon;
  };
  lin_addr pageTabEntToAddr(unsigned char lvl);
  lin_addr cr3ToAddr();
  void printCommon();
  void tableOrFramePtrToPte(simple_pte *tab);
private:
  void printBits(uint64_t val, int len);
  void underlineNibbles();
  void printNibblesHex();
};
static_assert(sizeof(simple_pte) == sizeof(uint64_t), "Bad simple_pte SZ");

// Linear address with different interpretations.
class lin_addr {
 public:
  union {
    uint64_t raw;
    struct {
      uint64_t OFFSET : 12, TAB : 9, DIR : 9, PDPT : 9, PML4 : 9;
    } tblOffsets;
    struct {
      // Ok to grab upper 40 for PN b/c (top reserved 0).
      uint64_t
      PGOFFSET : 12,
        PAGE_NUMBER : 40;
    } decomp4K;
    struct {
      uint64_t
      PGOFFSET : 21,
        PAGE_NUMBER : 31;
    } decomp2M;
    struct {
      uint64_t
      PGOFFSET : 30,
        PAGE_NUMBER : 22;
    } decomp1G;
  };
public:
  uint16_t operator[](uint8_t idx);
private:
  phys_addr getPhysAddrRec(simple_pte* root = nullptr, unsigned char lvl = 4);
  phys_addr getPhysAddrRecHelper(simple_pte* root, unsigned char lvl);
};
static_assert(sizeof(lin_addr) == sizeof(uint64_t), "Bad lin_addr SZ");

class UmPgTblMgr {
public:
  static void countDirtyPages(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  static void countAccessedPages(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  static void countValidPages(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  static void traverseValidPages(simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  static simple_pte * walkPgTblCopyDirty(simple_pte *root, simple_pte *copy = nullptr);
  simple_pte *addrToPTE(lin_addr la, simple_pte *root = nullptr,
                        unsigned char lvl = 4);
  static phys_addr getPhysAddrRec(lin_addr la, simple_pte *root = nullptr,
                           unsigned char lvl = 4);
  static phys_addr getPhysAddrRecHelper(lin_addr la, simple_pte *root, unsigned char lvl);

  static simple_pte *mapIntoPgTbl(simple_pte *root, lin_addr phys,
                                  lin_addr virt, unsigned char rootLvl,
                                  unsigned char mapLvl, unsigned char curLvl);

  static void traverseMappedPages();
  static simple_pte *getSlotPDPTRoot();

private:
  UmPgTblMgr(); // Don't instantiate.

  static void countDirtyPagesHelper(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  static void countAccessedPagesHelper(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  static void countValidPagesHelper(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);

  static void traverseValidPagesHelper(simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  static lin_addr copyDirtyPage(lin_addr src, unsigned char lvl);
  static lin_addr reconstructLinAddrPgFromOffsets(uint64_t *idx);
  simple_pte *aAddrToPTEHelper(lin_addr la, uint64_t *offsets, simple_pte *root,
                               unsigned char lvl);
  lin_addr cr3ToAddr();
  static simple_pte *nextTableOrFrame(simple_pte *pg_tbl_start, uint64_t pg_tbl_offset,
                               unsigned char lvl);
  static simple_pte *walkPgTblCopyDirtyHelper(uint64_t *counts,
                                              simple_pte *root,
                                              simple_pte *copy,
                                              unsigned char lvl, uint64_t *idx);
  static simple_pte *getPML4Root();
  static bool isLeaf     (simple_pte *pte, unsigned char lvl);
  static bool exists     (simple_pte *pte);
  static bool isAccessed (simple_pte *pte);
  static bool isDirty    (simple_pte *pte);
  static bool isReadOnly (simple_pte *pte);
  static bool isWritable (simple_pte *pte);
};

} // namespace umm

#endif //UMM_UM_PG_TBL_MGR
