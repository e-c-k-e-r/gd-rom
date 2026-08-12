#include "elf.h"
#include <cstring>

constexpr uint32_t PT_LOAD = 1;

bool elf_to_binary( const std::vector<uint8_t>& elf_data, std::vector<uint8_t>& bin_data ) {
	if ( elf_data.size() < sizeof(Elf32_Ehdr) ) return false;
	const auto* ehdr = (const Elf32_Ehdr*)(elf_data.data());

    // could use memcmp instead?
	if ( ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F' ) {
		return false;
	}

	uint32_t ph_offset = ehdr->e_phoff;
	uint16_t ph_num = ehdr->e_phnum;
	if ( ph_offset + ph_num * sizeof(Elf32_Phdr) > elf_data.size() ) return false;

	const auto* phdrs = (const Elf32_Phdr*)(elf_data.data() + ph_offset);

	uint32_t min_paddr = 0xFFFFFFFF;
	uint32_t max_paddr = 0;
	bool has_loadable = false;

	for ( uint16_t i = 0; i < ph_num; ++i ) {
		if ( phdrs[i].p_type == PT_LOAD && phdrs[i].p_filesz > 0 ) {
			if ( phdrs[i].p_paddr < min_paddr ) min_paddr = phdrs[i].p_paddr;
			if ( phdrs[i].p_paddr + phdrs[i].p_filesz > max_paddr ) {
				max_paddr = phdrs[i].p_paddr + phdrs[i].p_filesz;
			}
			has_loadable = true;
		}
	}

	if ( !has_loadable ) return false;

	uint32_t total_size = max_paddr - min_paddr;
	bin_data.assign(total_size, 0);

	for ( uint16_t i = 0; i < ph_num; ++i ) {
		if ( phdrs[i].p_type == PT_LOAD && phdrs[i].p_filesz > 0 ) {
			uint32_t dest_offset = phdrs[i].p_paddr - min_paddr;
			memcpy(bin_data.data() + dest_offset, elf_data.data() + phdrs[i].p_offset, phdrs[i].p_filesz);
		}
	}
	return true;
}