//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_PG_TBL_MGR
#define UMM_UM_PG_TBL_MGR

#include "stdint.h"
#include <functional>

#define SMALL_PG_SHIFT 12
#define MED_PG_SHIFT 21
#define LG_PG_SHIFT 30

// Order for page allocator. 0, 9, 18.
extern const unsigned char orders[];

// Size of shift for various pages, 12, 23, 30.
extern const unsigned char pgShifts[];

// Number of bytes for various page sizes.
extern const uint64_t pgBytes[];

// Indexed names for debugging printers..
extern const char* level_names[];

// Magic PML4 entry chosen for slot.
#define SLOT_PML4_NUM 0x180

// For the page allocator.
enum Orders {
  SMALL_ORDER = 0,
  MEDIUM_ORDER = 9,
  LARGE_ORDER = 18
};

enum Level {
  PML4_LEVEL = 4,
  PDPT_LEVEL = 3,
  DIR_LEVEL = 2,
  TBL_LEVEL = 1,
  NO_LEVEL = 0
};

enum PgSz {
  _1G__ = 3,
  _2M__ = 2,
  _4K__ = 1
};

namespace umm {

 // TODO(tommyu):where should this go?
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
  uint64_t pageTabEntToPFN(unsigned char lvl);
  lin_addr pageTabEntToAddr(unsigned char lvl);
  lin_addr cr3ToAddr();
  void printCommon();
  // Set address field of PTE. Optionally dirty bit and accessed bit.
  void setPte(simple_pte *tab, bool dirty = false, bool acc = false, bool rw = true);
  void clearPTE();
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

    // TODO(tommyu): Dropping these here in removing phys_addr. Might be totally redundant with those below, need to check.
    struct {
      uint64_t OFFSET : 12, FRAME_NUMBER : 40;
    } construct_addr_4K;
    struct {
      uint64_t OFFSET : 21, FRAME_NUMBER : 27;
    } construct_addr_2M;
    struct {
      uint64_t OFFSET : 30, FRAME_NUMBER : 18;
    } construct_addr_1G;

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
  // lin_addr getPhysAddrRec(simple_pte* root = nullptr, unsigned char lvl = 4);
  // lin_addr getPhysAddrRecHelper(simple_pte* root, unsigned char lvl);
};
static_assert(sizeof(lin_addr) == sizeof(uint64_t), "Bad lin_addr SZ");

namespace UmPgTblMgmt {
  void doubleCacheInvalidate(simple_pte *root, uint8_t lvl);
  void flushTranslationCaches();

  // NOTE: NYI. Higher level operations on page tables.
  // static void areEqual();
  // static void isSubset();
  // static void difference();

  // Reclaimer

  // Counters
  // TODO(tommyu): Do we want defaults? Maybe not.
  void countDirtyPages(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  void countAccessedPages(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  void countValidPages(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
  void countValidPTEs(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);

  void traverseValidPages(simple_pte *root, uint8_t lvl);
  uintptr_t injectOffset(lin_addr la, unsigned char lvl);

  // Copiers
  simple_pte * walkPgTblCOW(simple_pte *root, simple_pte *copy, uint8_t lvl);
  simple_pte * walkPgTblCopyDirtyCOW(simple_pte *root, simple_pte *copy, uint8_t lvl);

  simple_pte * walkPgTblCopyDirty(simple_pte *root, simple_pte *copy = nullptr);
  simple_pte * walkPgTblCopyDirty(simple_pte *root, simple_pte *copy, uint8_t lvl);


  // Extractors
  simple_pte *addrToPTE(lin_addr la, simple_pte *root = nullptr,
                        unsigned char lvl = 4);
  // Chasers
  lin_addr getPhysAddrRec(lin_addr la, simple_pte *root = nullptr,
                          unsigned char lvl = 4);

  // Mappers
  simple_pte *mapIntoPgTbl(simple_pte *root, lin_addr phys,
                           lin_addr virt, unsigned char rootLvl,
                           unsigned char mapLvl, unsigned char curLvl, bool writeFault);

  // Root Getters
  simple_pte *getSlotPDPTRoot();
  simple_pte *getPML4Root();

  // Printers / DBers.
  // TODO(tommyu): Fix these names.
  void dumpTableAddrs(simple_pte *root, unsigned char lvl);
  void dumpFullTableAddrs(simple_pte *root, unsigned char lvl);
  void alignToLvl(unsigned char lvl);

  // NOTE: World of lambdas begins here.

  // Used in traversePageTable.
  // Predicate, determines whether to follow this entry.
  typedef std::function<bool(simple_pte *curPte, uint8_t lvl)> predicateFn;
  // Work to run before recursive call. TODO: is this necessary?
  typedef std::function<void(simple_pte *curPte, uint8_t lvl)> beforeRecFn;
  // Work to run after recursive call.
  typedef std::function<void(simple_pte *childPte, simple_pte *curPte, uint8_t lvl)> afterRecFn;
  // Work to run when a leaf PTE is uncovered.
  typedef beforeRecFn leafFn;
  // Work to run before returning to parent page table.
  typedef beforeRecFn beforeRetFn;

  // Used in walkPageTable.
  typedef std::function<uintptr_t(simple_pte *curPte, lin_addr virt, uint8_t lvl)> walkLeafFn;

  static inline void invlpg(void* m) {
    asm volatile ( "invlpg (%0)" : : "b"(m) : "memory" );
  }

  void freePageTableLamb(simple_pte *root, unsigned char lvl);

  void cacheInvalidateValidPagesLamb(simple_pte *root, uint8_t lvl);

  lin_addr getPhysAddrLamb(lin_addr la, simple_pte* root, unsigned char lvl);

  void printTraversalLamb(simple_pte *root, uint8_t lvl);

  void countValidPagesLamb(std::vector<uint64_t> &counts,
                            simple_pte *root, uint8_t lvl);
  void countValidPTEsLamb(std::vector<uint64_t> &counts,
                          simple_pte *root, uint8_t lvl);

  void countAccessedPagesLamb(std::vector<uint64_t> &counts,
                              simple_pte *root, uint8_t lvl);

  void countDirtyPagesLamb(std::vector<uint64_t> &counts,
                           simple_pte *root, uint8_t lvl);

  void traverseAccessedPages(simple_pte *root, uint8_t lvl, leafFn L);
  void traverseAccessedPages(simple_pte *root, uint8_t lvl, leafFn L, beforeRetFn BRET);

  void traverseValidPages(simple_pte *root, uint8_t lvl, leafFn L);

  void traverseValidPages(simple_pte *root, uint8_t lvl, beforeRecFn BR,
                          afterRecFn AR, leafFn L, beforeRetFn BRET);

  simple_pte *traversePageTable(simple_pte *root, uint8_t lvl,
                                predicateFn P, beforeRecFn BR,
                                afterRecFn AR, leafFn L,
                                beforeRetFn BRET);

  uintptr_t walkPageTable(simple_pte *root, uint8_t lvl, lin_addr virt, walkLeafFn L);

  simple_pte *mapIntoPgTblLamb(simple_pte *root, lin_addr phys,
                               lin_addr virt, unsigned char rootLvl,
                               unsigned char mapLvl, unsigned char curLvl);


  // namespace {

  // Mapper.
   simple_pte *mapIntoPgTblHelper(simple_pte *root, lin_addr phys,
                                        lin_addr virt, unsigned char rootLvl,
                                  unsigned char mapLvl, unsigned char curLvl, bool writeFault);
  simple_pte *findAndSetPTECOW(simple_pte *root, simple_pte *origPte,
                                 lin_addr virt, unsigned char rootLvl,
                                 unsigned char mapLvl, unsigned char curLvl);


  // Counter Helpers
   void countDirtyPagesHelper(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
   void countAccessedPagesHelper(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
   void countValidPagesHelper(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);
   void countValidPTEsHelper(std::vector<uint64_t> &counts, simple_pte *root = nullptr, uint8_t lvl = PML4_LEVEL);

  //  void traverseValidPagesHelper(simple_pte *root, uint8_t);
  // Printers Debuggers
   void dumpFullTableAddrsHelper(simple_pte *root, unsigned char lvl);

   lin_addr copyDirtyPage(lin_addr src, unsigned char lvl);
   lin_addr reconstructLinAddrPgFromOffsets(uint64_t *idx);
  simple_pte *AddrToPTEHelper(lin_addr la, uint64_t *offsets, simple_pte *root,
                               unsigned char lvl);
  lin_addr cr3ToAddr();
   simple_pte *nextTableOrFrame(simple_pte *pg_tbl_start, uint64_t pg_tbl_offset,
                               unsigned char lvl);
  simple_pte *walkPgTblCopyDirtyCOWHelper(simple_pte *root,
                                 simple_pte *copy,
                                 unsigned char lvl,
                                 uint64_t *idx);
   simple_pte *walkPgTblCOWHelper(simple_pte *root,
                                              simple_pte *copy,
                                              unsigned char lvl,
                                              uint64_t *idx);
  simple_pte *walkPgTblCopyDirtyHelper(simple_pte *root,
                                 simple_pte *copy,
                                 unsigned char lvl,
                                 uint64_t *idx);

   lin_addr getPhysAddrRecHelper(lin_addr la, simple_pte *root, unsigned char lvl);
  bool exists (simple_pte *pte);
   bool isLeaf     (simple_pte *pte, unsigned char lvl);
   bool isAccessed (simple_pte *pte);
   bool isDirty    (simple_pte *pte);
   bool isReadOnly (simple_pte *pte);
   bool isWritable (simple_pte *pte);
// } // anon namespace
} // namespace UmPgTblMgmt
}

#endif //UMM_UM_PG_TBL_MGR
