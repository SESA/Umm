//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmLoader.h"
#include "UmRegion.h"

// Symbol table
umm::ElfLoader::Elf64_Sym const *elf_symtab = nullptr; 
size_t			elf_symtab_size = 0; 
// String table
char const *elf_strtab = nullptr; 
size_t			elf_strtab_size = 0;

const char *umm::ElfLoader::get_symbol_name(uint64_t tbl_idx) {
  kassert(elf_strtab != nullptr);
  kassert(elf_strtab_size != 0);
  return elf_strtab + tbl_idx;
}

uintptr_t umm::ElfLoader::GetSymbolAddress(const char *sym) {
  kassert(elf_symtab != nullptr);
  kassert(elf_symtab_size != 0);
  for (unsigned int i = 0; i < elf_symtab_size; i++) {
    if (std::string(get_symbol_name(elf_symtab[i].st_name)) ==
        std::string(sym)) {
      return elf_symtab[i].st_value;
    }
  }
  ERROR(RED "Error: %s not found in elf symbol table: %s\n" RESET, sym);
  return 0;
}

umm::UmSV
umm::ElfLoader::createSVFromElf(unsigned char *elf_start) {

  // Get location of elf header from input binary blob
  auto eh = (const umm::ElfLoader::Ehdr *)elf_start;

  if (!elf_check_file(eh) || !elf_check_supported(eh)) {
    ERROR("Error: ELF File cannot be loaded.\n");
    kabort();
  }

  // Create state structure to return from the call
  auto ret_state = UmSV(eh->e_entry);

  // Load the section header from the elf
  Shdr *sht = (Shdr *)((char *)eh + eh->e_shoff);

  // Last page pointer
  uintptr_t next_page_ptr = 0;

  /** Elf Section Loop
  *   Iterate over each section
  */
  for (int i = 0; i < eh->e_shnum; i++) {
    Shdr *sh = sht + i;

    // Skip if it the section is empty.
    if (!sh->sh_size)
      continue;

    // Store symtab location for symbol lookups
    if (sh->sh_type == SHT_SYMTAB &&
        std::string(get_section_name(eh, sh)) == ".symtab") {
      elf_symtab = (Elf64_Sym *)((char *)eh + sh->sh_offset);
      elf_symtab_size = sh->sh_size / sizeof(Elf64_Sym);
      continue;
    }

    // Store strtab location for symbol lookups
    if (sh->sh_type == SHT_STRTAB &&
        std::string(get_section_name(eh, sh)) == ".strtab") {
      elf_strtab = ((char *)eh + sh->sh_offset);
      elf_strtab_size = sh->sh_size;
      continue;
    }

    // Skip section if doesn't appear in address space.
    if (!(sh->sh_flags & SHF_ALLOC))
      continue;

    // Error if section is not page-aligned
    kassert((sh->sh_addr % kPageSize) == 0);

    kprintf("Checking section : %s\n", get_section_name(eh, sh));

    // New Region structure
    umm::Region reg = Region();
    reg.start = sh->sh_addr;
    reg.length = sh->sh_size;
    reg.name = std::string(get_section_name(eh, sh));
    reg.writable = (sh->sh_flags & SHF_WRITE);

    // Set data reference to backing physical memory
    if (!(sh->sh_type == SHT_NOBITS)) {
      reg.data = (unsigned char *)eh + sh->sh_offset;
    }

    /** Specialty section loops
    *   E.g., gcc_except_* which typically follows .rodata
    */
    int i_ptr = i;
    while (i_ptr < eh->e_shnum && ++i_ptr) {
      Shdr *nxt_sh = sht + i_ptr;
      auto sh_name = std::string(get_section_name(eh, nxt_sh));
      if (sh_name.length() == 0)
        break;
      if (sh_name.substr(0, 8) != "link_set" && sh_name.substr(0, 5) != ".gcc_")
        break;
      // Assume a link_set_* section and include it the current section
      kprintf("Extra data section found: %s\n", sh_name.c_str());
      i++;
      // Expand Region length to include addition section
      if (reg.start + reg.length < nxt_sh->sh_addr) {
        // added space for alignment 
        auto buff = nxt_sh->sh_addr - (reg.start + reg.length);
        reg.length += buff ;
      }
      reg.length += nxt_sh->sh_size;
    }

    // Last the last page pointer after the bss region
    if (reg.name == ".bss") {
      next_page_ptr = ebbrt::Pfn::Up(reg.start + reg.length).ToAddr();
    }

    ret_state.AddRegion(reg);
  } // end Elf Section loop

  // TODO(jmcadden): Move the 'usr' and Solo5 logic out of the Elf loader

  // Add 'usr' region beginning the next available page
  kassert(next_page_ptr);
  size_t usr_len =
      (kSlotEndVAddr + 1) - next_page_ptr; // Dedicate all remaining memory

  auto usr_reg = Region();
  usr_reg.start = next_page_ptr;
  usr_reg.length = usr_len; 
  usr_reg.name = std::string("usr");
  usr_reg.writable = true;
  ret_state.AddRegion(usr_reg);

  // HACK: With the ef zeroed in the sv constructor, this is the minimal state
  // to add to get a JS app running on a rump kernel. There is fpu and simd
  // control state as well as a bit set in the CS segment register. These values
  // were simply observed to work, they may be a superset of what's actually
  // neded to run. https://www.felixcloutier.com/x86/FXSAVE.html

  // Avoid X87FpuFloatingPointError:
  // https://wiki.osdev.org/Exceptions#x87_Floating-Point_Exception
  ret_state.ef.fpu[0] = 0x37f;

  // Avoid SimdFloatingPointException:
  // https://wiki.osdev.org/Exceptions#SIMD_Floating-Point_Exception
  ret_state.ef.fpu[3] = 0xffff00001f80;

  // Avoid GeneralProtectionException on CS register.
  ret_state.ef.cs = 0x8;

  return ret_state;
}
