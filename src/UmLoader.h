//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UMM_UM_LOADER_H_
#define UMM_UM_LOADER_H_

/** 
 *  UmLoader.h  - Load SV from Elf and beyond...
 */

#include "umm-common.h"
#include "umm-solo5.h"

#include "UmInstance.h"
#include "UmState.h"

// Start of target ELF.
extern unsigned char _sv_start __attribute__((weak));

namespace umm {
namespace ElfLoader {

const char *get_symbol_name(uint64_t tbl_idx);

// Interprets Elf binary and creates a new Um Instance
std::unique_ptr<UmInstance> CreateInstanceFromElf(unsigned char *elf_start);

// Get the address of a symbol in the target
uintptr_t GetSymbolAddress(const char *sym);

#define STACK_PAGES (1 << 8)
#define HEAP_PAGES (1 << 8)

#define ELFMAG0 0x7F // e_ident[EI_MAG0]
#define ELFMAG1 'E'  // e_ident[EI_MAG1]
#define ELFMAG2 'L'  // e_ident[EI_MAG2]
#define ELFMAG3 'F'  // e_ident[EI_MAG3]

#define ERROR ebbrt::kprintf_force
#define ELFDATA2LSB 1
#define ELFCLASS64 2
#define EV_CURRENT 1

/* ELF base types. */
typedef uint32_t Elf32_Addr;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef int32_t Elf32_Sword;
typedef uint32_t Elf32_Word;
typedef uint16_t Elf32_Section;
typedef uint64_t Elf64_Addr;
typedef uint16_t Elf64_Half;
typedef int16_t Elf64_SHalf;
typedef uint64_t Elf64_Off;
typedef int32_t Elf64_Sword;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;
typedef uint16_t Elf64_Section;

// Symbol table entry
typedef struct
{
  Elf64_Word    st_name;                /* Symbol name (string tbl index) */
  unsigned char st_info;                /* Symbol type and binding */
  unsigned char st_other;               /* No defined meaning, 0 */
  Elf64_Section st_shndx;               /* Section index */
  Elf64_Addr    st_value;               /* Symbol value */
  Elf64_Xword   st_size;                /* Symbol size */
} Elf64_Sym;


#define STN_UNDEF	0		/* Undefined symbol index */

#define STB_LOCAL	0		/* Symbol not visible outside obj */
#define STB_GLOBAL	1		/* Symbol visible outside obj */
#define STB_WEAK	2		/* Like globals, lower precedence */
#define STB_LOOS	10		/* OS-specific semantics */
#define STB_GNU_UNIQUE	10		/* Symbol is unique in namespace */
#define STB_HIOS	12		/* OS-specific semantics */
#define STB_LOPROC	13		/* Processor-specific semantics */
#define STB_HIPROC	15		/* Processor-specific semantics */

#define STT_NOTYPE	0		/* Symbol type is unspecified */
#define STT_OBJECT	1		/* Symbol is a data object */
#define STT_FUNC	2		/* Symbol is a code object */
#define STT_SECTION	3		/* Symbol associated with a section */
#define STT_FILE	4		/* Symbol gives a file name */
#define STT_COMMON	5		/* An uninitialised common block */
#define STT_TLS		6		/* Thread local data object */
#define STT_RELC	8		/* Complex relocation expression */
#define STT_SRELC	9		/* Signed Complex relocation expression */
#define STT_LOOS	10		/* OS-specific semantics */
#define STT_GNU_IFUNC	10		/* Symbol is an indirect code object */
#define STT_HIOS	12		/* OS-specific semantics */
#define STT_LOPROC	13		/* Processor-specific semantics */
#define STT_HIPROC	15		/* Processor-specific semantics */

// ELF Header
#define EI_NIDENT 16
struct Ehdr {
  unsigned char e_ident[EI_NIDENT]; /* ELF "magic number" */
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry; /* Entry point virtual address */
  Elf64_Off e_phoff;  /* Program header table file offset */
  Elf64_Off e_shoff;  /* Section header table file offset */
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
};

// Section Header
struct Shdr {
  Elf64_Word sh_name;   /* Section name, index in string tbl */
  Elf64_Word sh_type;   /* Type of section */
  Elf64_Xword sh_flags; /* Miscellaneous section attributes */
  Elf64_Addr sh_addr;   /* Section virtual addr at execution */
  Elf64_Off sh_offset;  /* Section file offset */
  Elf64_Xword sh_size;  /* Size of section in bytes */

  // If sec is relative, string table for given symbol
  // can be found in sh_link.
  Elf64_Word sh_link; /* Index of another section */

  // If header is type sht_rel or sht_rela,
  // sh_info is the section relocation operates on.
  Elf64_Word sh_info;       /* Additional section information */
  Elf64_Xword sh_addralign; /* Section alignment */
  Elf64_Xword sh_entsize;   /* Entry size if section holds table */
};

enum ShT_Attributes {
  SHF_WRITE = 0x1, // Writable
  SHF_ALLOC = 0x02 // Exists in memory
};

enum Elf_Ident {
  EI_MAG0 = 0,       // 0x7F
  EI_MAG1 = 1,       // 'E'
  EI_MAG2 = 2,       // 'L'
  EI_MAG3 = 3,       // 'F'
  EI_CLASS = 4,      // Architecture (32/64)
  EI_DATA = 5,       // Byte Order
  EI_VERSION = 6,    // ELF Version
  EI_OSABI = 7,      // OS Specific
  EI_ABIVERSION = 8, // OS Specific
  EI_PAD = 9         // Padding
};

enum Elf_Type {
  ET_NONE = 0, // Unkown Type
  ET_REL = 1,  // Relocatable File
  ET_EXEC = 2  // Executable File
};

enum ShT_Types {
  SHT_NULL = 0,     // Null section
  SHT_PROGBITS = 1, // Program information
  SHT_SYMTAB = 2,   // Symbol table
  SHT_STRTAB = 3,   // String table
  SHT_RELA = 4,     // Relocation (w/ addend)
  SHT_NOBITS = 8,   // Not present in file
  SHT_REL = 9,      // Relocation (no addend)
};

inline char *get_section_name(const Ehdr *eh, const Shdr *sh) {
  kassert(eh != 0);
  // Section header table
  Shdr *sht = (Shdr *)((char *)eh + eh->e_shoff);
  // Section holding names of sections.
  Shdr *strSec = sht + eh->e_shstrndx;
  // The string table.
  char *strTab = (char *)eh + strSec->sh_offset;
  if (strTab == NULL) {
    return NULL;
  }
  char *name = strTab + sh->sh_name;
  return name;
}

inline bool elf_check_file(const Ehdr *eh) {
  if (!eh)
    return false;
  if (eh->e_ident[EI_MAG0] != ELFMAG0) {
    ERROR("ELF Header EI_MAG0 incorrect.\n");
    return false;
  }
  if (eh->e_ident[EI_MAG1] != ELFMAG1) {
    ERROR("ELF Header EI_MAG1 incorrect.\n");
    return false;
  }
  if (eh->e_ident[EI_MAG2] != ELFMAG2) {
    ERROR("ELF Header EI_MAG2 incorrect.\n");
    return false;
  }
  if (eh->e_ident[EI_MAG3] != ELFMAG3) {
    ERROR("ELF Header EI_MAG3 incorrect.\n");
    return false;
  }
  return true;
}

inline bool elf_check_supported(const Ehdr *eh) {
  if (eh->e_ident[EI_CLASS] != ELFCLASS64) {
    ERROR("Unsupported ELF File Class.\n");
    return false;
  }
  if (eh->e_ident[EI_DATA] != ELFDATA2LSB) {
    ERROR("Unsupported ELF File byte order.\n");
    return false;
  }
  if (eh->e_ident[EI_VERSION] != EV_CURRENT) {
    ERROR("Unsupported ELF File version.\n");
    return false;
  }
  if (eh->e_type != ET_REL && eh->e_type != ET_EXEC) {
    ERROR("Unsupported ELF File type.\n");
    return false;
  }

  if (eh->e_type == ET_REL) {
    ERROR("Relocatable Elf Unsupported.\n");
    return false;
  }
  return true;
}

} // umm::ElfLoader
} // umm

#endif // UMM_UM_LOADER_H_
