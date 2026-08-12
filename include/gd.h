#pragma once

#include "config.h"
#include "reader.h"
#include "writer.h"
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

constexpr size_t SECTOR_SIZE = 2048;
constexpr uint32_t LBA_OFFSET = 11702;
constexpr uint32_t FAD_LBA_BIAS   = 150;
constexpr uint32_t MILCD_TRACK2_FAD = 11852;
constexpr uint32_t GDROM_TRACK3_FAD = 45150;

#pragma pack(push, 1)
struct Both16 {
	uint16_t le;
	uint16_t be;
	void set(uint16_t val) {
		le = val;
		be = __builtin_bswap16(val);
	}
};

struct Both32 {
	uint32_t le;
	uint32_t be;
	void set(uint32_t val) {
		le = val;
		be = __builtin_bswap32(val);
	}
};

struct IsoPvd {
	uint8_t type = 1;
	char id[5] = {'C', 'D', '0', '0', '1'};
	uint8_t version = 1;
	uint8_t unused1 = 0;
	char system_id[32];
	char volume_id[32];
	uint8_t unused2[8] = {0};
	Both32 volume_space_size;
	uint8_t unused3[32] = {0};
	Both16 volume_set_size;
	Both16 volume_sequence_number;
	Both16 logical_block_size;
	Both32 path_table_size;
	uint32_t l_path_table_lba;
	uint32_t optional_l_path_table_lba = 0;
	uint32_t m_path_table_lba;
	uint32_t optional_m_path_table_lba = 0;
	uint8_t root_directory_record[34];
	char volume_set_id[128] = {0};
	char publisher_id[128] = {0};
	char data_preparer_id[128] = {0};
	char application_id[128] = {0};
	char copyright_file_id[37] = {0};
	char abstract_file_id[37] = {0};
	char bibliographic_file_id[37] = {0};
	char creation_date[17] = {0};
	char modification_date[17] = {0};
	char expiration_date[17] = {0};
	char effective_date[17] = {0};
	uint8_t file_structure_version = 1;
	uint8_t unused4 = 0;
	uint8_t application_use[512] = {0};
	uint8_t unused5[653] = {0};
};

struct IsoDirectoryRecord {
	uint8_t length;
	uint8_t ext_attr_length = 0;
	Both32 extent_lba;
	Both32 data_length;
	uint8_t datetime[7];
	uint8_t flags;
	uint8_t file_unit_size = 0;
	uint8_t interleave_gap = 0;
	Both16 volume_seq_num;
	uint8_t name_length;
};
#pragma pack(pop)

struct IsoFilter {
	std::vector<std::string> skip_extensions;
	std::vector<std::string> skip_substrings;

	bool should_skip( const fs::path& path ) const;
};

struct FileNode {
	std::string name;
	fs::path physical_path;
	bool is_directory = false;
	uint32_t size = 0;
	uint32_t lba = 0;
	std::vector<FileNode> children;
};


struct Layout {
	uint32_t total_sectors = 0;
	std::vector<FileNode*> dirs;
	std::vector<FileNode*> files;
	std::map<FileNode*, FileNode*> parent_map; // unordered_map?

	void solve( FileNode& root, uint32_t starting_lba, uint32_t lba_offset );
};

class UF_API Context {
public:
	FileNode root;
	std::vector<uint8_t> ip_bin;
	IsoFilter filter;

	Context();
	
	bool load_bootstrap( const uint8_t* data, size_t size = 32768 );
	bool load_bootstrap( const fs::path& path );
	void merge_directory( const fs::path& source_dir );

private:
	void add_node( const fs::path& rel_path, const fs::path& phys_path, bool is_dir );
};

bool UF_API compile_iso( Context& context, std::vector<uint8_t>& iso_buffer, uint32_t lba_offset = LBA_OFFSET );
bool UF_API write_iso( Context& context, const std::string& out_path );
bool UF_API write_cdi( Context& context, const std::string& cdi_path );
bool UF_API write_gdi( Context& context, const std::string& gdi_path );
bool UF_API write_cue( Context& context, const std::string& cue_path );