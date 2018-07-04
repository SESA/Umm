//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "UmLoader.h"

std::unique_ptr<umm::UmInstance>
umm::ElfLoader::CreateInstanceFromElf(unsigned char *elf_start) {

  // Get location of elf header from input binary blob
  auto eh = (const umm::ElfLoader::Ehdr *)elf_start;

  if (!elf_check_file(eh) || !elf_check_supported(eh)) {
    ERROR("Error: ELF File cannot be loaded.\n");
    kabort();
  }

  // Create state structure to return from the call
  auto ret_state = UmState(eh->e_entry);

  // Load the section header from the elf
  Shdr *sht = (Shdr *)((char *)eh + eh->e_shoff);

  // Iterate over each section
  for (int i = 0; i < eh->e_shnum; i++) {
    Shdr *sh = sht + i;

    // Skip if it the section is empty.
    if (!sh->sh_size)
      continue;

    // Skip section if doesn't appear in address space.
    if (!(sh->sh_flags & SHF_ALLOC))
      continue;

    // Error if section is not page-aligned
    kassert((sh->sh_addr % kPageSize) == 0);

    kprintf("Checking section : %s\n", get_section_name(eh, sh));

    // New Region structure
    auto reg = UmState::Region();
    reg.start = sh->sh_addr;
    reg.length = sh->sh_size;
    reg.name = std::string(get_section_name(eh, sh));
    reg.writable = (sh->sh_flags & SHF_WRITE);

    // Set data reference to backing physical memory
    if (!(sh->sh_type == SHT_NOBITS)) {
      reg.data = (unsigned char *)eh + sh->sh_offset;
    }

    // Check for any speciality sections to the current section
    //    E.g., gcc_except_* which typically follows .rodata
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
      // Expand the Region length to include this addition section
      // XXX(jmcadden): New offset does not account for section alignment
      reg.length += nxt_sh->sh_size;
    }

    ret_state.AddRegion(reg);
  } // end Region loop

  // TODO(jmcadden): Add heap/stack section to Region

  return std::make_unique<UmInstance>(ret_state);
}
