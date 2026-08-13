#include <gd.h>

#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include <cstring>

namespace {
	bool ends_with(const std::string& str, const std::string& suffix) {
		if ( str.length() < suffix.length() ) return false;
		return str.rfind(suffix) == ( str.length() - suffix.length() );
	}

	std::string clean_iso_name(const std::string& name, bool is_dir) {
		if ( name == "." || name == ".." ) return name;
		std::string out;

		for ( char c : name ) {
			if ( std::isalnum(c) || c == '_' || c == '-' ) out += std::toupper(c);
			else if ( c == '.' && !is_dir ) out += '.';
		}

		if ( out.length() > 30 ) {
			if ( is_dir ) out = out.substr(0, 30);
			else {
				size_t dot = out.find_last_of('.');
				if ( dot != std::string::npos && dot > 25 ) out = out.substr(0, 25) + out.substr(dot);
				else out = out.substr(0, 30);
			}
		}

		if ( !is_dir && !ends_with(out, ";1") ) out += ";1";

		return out;
	}
}


Reader::Reader( const std::vector<uint8_t>& buffer, uint32_t offset, uint32_t length, bool zeroCopy, bool aligned ) : m_buffer(buffer), m_offset(offset), m_endOffset(offset + length), m_zeroCopy( zeroCopy ), m_aligned( aligned ) {
}

template<>
const char* Reader::read<char>( size_t readSize ) {
	if ( m_offset + readSize > m_endOffset ) return nullptr;

	const char* ptr = (const char*)(m_buffer.data() + m_offset);
	m_offset += readSize;
	return ptr;
}

Writer::Writer( std::vector<uint8_t>& buffer, uint32_t offset, bool aligned ) : m_buffer(buffer), m_offset(offset), m_aligned(aligned) {
}

Writer::Writer( std::vector<uint8_t>& buffer, bool aligned ) : m_buffer(buffer), m_offset(0), m_aligned(aligned) {
}

bool IsoFilter::should_skip(const fs::path& path) const {
	if ( path.has_extension() ) {
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		for ( const auto& skip_ext : skip_extensions ) if ( ext == skip_ext ) return true;
	}

	std::string path_str = path.string();
	std::transform(path_str.begin(), path_str.end(), path_str.begin(), ::tolower);
	for ( const auto& sub : skip_substrings ) if ( path_str.find(sub) != std::string::npos ) return true;

	return false;
}

Context::Context()  {
	root.name = "";
	root.is_directory = true;
}

bool Context::load_bootstrap( const uint8_t* data, size_t size ) {
	ip_bin.resize( size );
	memcpy( ip_bin.data(), data, size );
	return true;
}
bool Context::load_bootstrap( const fs::path& path ) {
	std::ifstream f( path, std::ios::binary );
	if ( !f ) return false;

	f.seekg(0, std::ios::end);
	size_t size = f.tellg();
	f.seekg(0, std::ios::beg);

	ip_bin.resize(size);
	f.read((char*)(ip_bin.data()), size);

	if ( ip_bin.size() < 32768 ) ip_bin.resize(32768, 0);
	return true;
}

void Context::merge_directory(const fs::path& source_dir) {
	if ( !fs::exists( source_dir ) ) {
		return;
	}
	fs::path canonical_source = fs::canonical(source_dir);
	for ( const auto& entry : fs::recursive_directory_iterator( canonical_source ) ) {
		if ( filter.should_skip( entry.path() ) ) continue;
		fs::path rel = fs::relative(entry.path(), canonical_source);
		if ( rel.empty() || rel == "." ) continue;

		add_node( rel, entry.path(), entry.is_directory() );
	}
}

void Context::add_node(const fs::path& rel_path, const fs::path& phys_path, bool is_dir) {
	FileNode* current = &root;
	for ( const auto& part : rel_path ) {
		bool is_leaf = (part == rel_path.filename());

		std::string part_str = clean_iso_name( part.string(), is_leaf ? is_dir : true );

		auto it = std::find_if(current->children.begin(), current->children.end(), [&](const FileNode& child) {
			return child.name == part_str;
		});

		if ( it != current->children.end() ) {
			if ( is_leaf ) {
				it->is_directory = is_dir;
				it->physical_path = phys_path;
				it->size = is_dir ? 0 : fs::file_size(phys_path);
			}
			current = &(*it);
			continue;
		}

		FileNode new_node;
		new_node.name = part_str;
		new_node.is_directory = is_leaf ? is_dir : true;
		new_node.physical_path = phys_path;
		if ( !new_node.is_directory ) new_node.size = fs::file_size(phys_path);
		current->children.emplace_back(new_node);
		current = &current->children.back();
	}
}

void Layout::solve( FileNode& root, uint32_t starting_lba, uint32_t lba_offset ) {
	uint32_t current_lba = starting_lba;
	dirs.clear();
	files.clear();
	parent_map.clear();

	auto collect = [&](auto& self, FileNode* d) -> void {
		dirs.emplace_back(d);

		std::sort(d->children.begin(), d->children.end(), [](const FileNode& a, const FileNode& b) {
			return a.name < b.name;
		});

		std::vector<FileNode*> subdirs;
		for ( auto& child : d->children ) {
			if ( child.is_directory ) {
				subdirs.emplace_back(&child);
				parent_map[&child] = d;
			} else {
				files.emplace_back(&child);
			}
		}

		for ( auto* sub : subdirs ) {
			self(self, sub);
		}
	};

	collect(collect, &root);

	for ( auto* d : dirs ) {
		d->lba = current_lba;
		uint32_t size_bytes = 68;
		for ( const auto& child : d->children ) {
			uint32_t iso_name_len = child.name.length();
			uint32_t rec_len = sizeof(IsoDirectoryRecord) + iso_name_len;

			std::string real_name = child.physical_path.filename().string();
			uint32_t nm_len = 5 + real_name.length();
			uint32_t pre_nm_pad = (rec_len % 2 != 0) ? 1 : 0;

			rec_len += pre_nm_pad + nm_len;
			if ( rec_len % 2 != 0 ) rec_len++;

			size_bytes += rec_len;
		}
		d->size = size_bytes;
		uint32_t sectorsNeeded = (size_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
		current_lba += sectorsNeeded;
	}

	FileNode* boot_bin_node = nullptr;

	for ( auto* f : files ) {
		std::string filename = f->physical_path.filename().string();
		std::transform(filename.begin(), filename.end(), filename.begin(), ::toupper);

		if ( filename == "1ST_READ.BIN" ) {
			boot_bin_node = f;
		} else {
			f->lba = current_lba;
			uint32_t sectorsNeeded = (f->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
			current_lba += sectorsNeeded;
		}
	}

	if ( boot_bin_node ) {
		uint32_t bin_sectors = (boot_bin_node->size + SECTOR_SIZE - 1) / SECTOR_SIZE;

		if ( lba_offset == 45000 ) {
			boot_bin_node->lba = 503850 - bin_sectors;
			total_sectors = 549150 - 45000;
		} else {
			boot_bin_node->lba = current_lba;
			total_sectors = current_lba + bin_sectors;
		}
	} else {
		total_sectors = (lba_offset == 45000) ? (549150 - 45000) : current_lba;
	}
}

namespace {
	const unsigned int track_start_mark[10] = {
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
	};

	const unsigned int gap_dummy_sector[6][2] = {
		{0x002, 0x20}, {0x006, 0x20}, {0x91c, 0x3f}, {0x91d, 0x13}, {0x91e, 0xb0}, {0x91f, 0xbe}
	};

	const unsigned int gap_dummy_sector_2[101][2] = {
		{0x00008, 0x54}, {0x00009, 0x44}, {0x0000a, 0x49}, {0x0000b, 0x01}, {0x0000c, 0x50}, {0x0000d, 0x01}, {0x0000e, 0x02},
		{0x0000f, 0x02}, {0x00010, 0x02}, {0x00011, 0x80}, {0x00012, 0xff}, {0x00013, 0xff}, {0x00014, 0xff}, {0x00808, 0x78},
		{0x00809, 0x62}, {0x0080a, 0x21}, {0x0080b, 0x6d}, {0x00818, 0x93}, {0x00819, 0x78}, {0x0081a, 0x85}, {0x0081b, 0xf5},
		{0x0081c, 0x60}, {0x0081d, 0xf5}, {0x0081e, 0xf7}, {0x0081f, 0xf7}, {0x00820, 0xf7}, {0x00821, 0x0b}, {0x00822, 0xaa},
		{0x00823, 0xaa}, {0x00824, 0xaa}, {0x0085e, 0x88}, {0x0085f, 0xa6}, {0x00860, 0x63}, {0x00861, 0xb7}, {0x0086e, 0xc7},
		{0x0086f, 0x3c}, {0x00870, 0xcc}, {0x00871, 0xf4}, {0x00872, 0x30}, {0x00873, 0xf4}, {0x00874, 0xf5}, {0x00875, 0xf5},
		{0x00876, 0xf5}, {0x00877, 0x8b}, {0x00878, 0x55}, {0x00879, 0x55}, {0x0087a, 0x55}, {0x008b4, 0xf0}, {0x008b5, 0xc4},
		{0x008b6, 0x42}, {0x008b7, 0xda}, {0x008c6, 0x63}, {0x008c7, 0xb7}, {0x008c8, 0xd0}, {0x008c9, 0xf7}, {0x008ca, 0x59},
		{0x008cb, 0x26}, {0x008cc, 0xea}, {0x008cd, 0x66}, {0x008d0, 0xd1}, {0x008d2, 0xf3}, {0x008d3, 0x15}, {0x008d4, 0x4d},
		{0x008d5, 0xf5}, {0x008d6, 0xf8}, {0x008d7, 0x31}, {0x008d8, 0x7e}, {0x008d9, 0x2f}, {0x008da, 0x6b}, {0x008db, 0xcc},
		{0x008dc, 0x41}, {0x008dd, 0x80}, {0x008de, 0xe0}, {0x008df, 0xf2}, {0x008e0, 0x23}, {0x008e1, 0x40}, {0x008fa, 0x42},
		{0x008fb, 0xda}, {0x008fc, 0xcb}, {0x008fd, 0x22}, {0x008fe, 0x93}, {0x008ff, 0x5a}, {0x00900, 0x1a}, {0x00901, 0xa2},
		{0x00904, 0x7b}, {0x00906, 0x0c}, {0x00907, 0xbf}, {0x00910, 0x4e}, {0x00911, 0x0d}, {0x00912, 0x6e}, {0x00913, 0xcf},
		{0x00914, 0x77}, {0x00915, 0x04}
	};

	const unsigned int sector1[28][2] = {
		{0x00000, 0x02}, {0x00002, 0x96}, {0x00006, 0x2e}, {0x00007, 0x01}, {0x00024, 0xc4}, {0x00025, 0x01}, {0x00038, 0x02},
		{0x00041, 0xc4}, {0x00042, 0x01}, {0x0005a, 0xff}, {0x0005b, 0xff}, {0x0005c, 0xff}, {0x0005d, 0xff}, {0x0005e, 0xff},
		{0x0005f, 0xff}, {0x00060, 0xff}, {0x00061, 0xff}, {0x00062, 0x01}, {0x00066, 0x80}, {0x0006a, 0x02}, {0x0006e, 0x10},
		{0x00072, 0x44}, {0x00073, 0xac}, {0x000a0, 0xff}, {0x000a1, 0xff}, {0x000a2, 0xff}, {0x000a3, 0xff}, {0x000bd, 0x01}
	};

	const unsigned int sector2[31][2] = {
		{0x00000, 0x02}, {0x00002, 0x96}, {0x00006, 0x9c}, {0x00007, 0x04}, {0x00010, 0x02}, {0x00018, 0x01}, {0x00020, 0xb6},
		{0x00021, 0x2d}, {0x00038, 0x01}, {0x0003c, 0x04}, {0x0005a, 0xff}, {0x0005b, 0xff}, {0x0005c, 0xff}, {0x0005d, 0xff},
		{0x0005e, 0xff}, {0x0005f, 0xff}, {0x00060, 0xff}, {0x00061, 0xff}, {0x00062, 0x01}, {0x00066, 0x80}, {0x0006a, 0x02},
		{0x0006e, 0x10}, {0x00072, 0x44}, {0x00073, 0xac}, {0x000a0, 0xff}, {0x000a1, 0xff}, {0x000a2, 0xff}, {0x000a3, 0xff},
		{0x000b0, 0x02}, {0x000b8, 0xb6}, {0x000b9, 0x2d}
	};

	const unsigned int cdi_head_next[6][2] = {
		{0x0000b, 0x02}, {0x00016, 0x80}, {0x00017, 0x40}, {0x00018, 0x7e}, {0x00019, 0x05}, {0x0001d, 0x98}
	};

	const unsigned int cdi_header_end[4][2] = {
		{0x00001, 0x01}, {0x00005, 0x01}, {0x00026, 0x06}, {0x00029, 0x80}
	};

	void fill_buffer( uint8_t *buf, int total_size, int values_array_size, const unsigned int values_array[][2] ) {
		memset(buf, 0x0, total_size);
		for ( int i = 0 ; i < values_array_size ; i++ ) buf[values_array[i][0]] = values_array[i][1];
	}

	void write_array_block( std::ofstream& cdi, int array_size, int array_entries, const unsigned int values_array[][2] ) {
		std::vector<uint8_t> buf(array_size);
		fill_buffer(buf.data(), array_size, array_entries, values_array);
		cdi.write((const char*)(buf.data()), array_size);
	}

	void write_null_block( std::ofstream& cdi, int size ) {
		std::vector<uint8_t> buf(size, 0);
		cdi.write((const char*)(buf.data()), size);
	}

	void write_cdi_header_start( std::ofstream& cdi, const std::string& cdiname ) {
		uint8_t head_track_start_mark_blocks[20];
		for ( int i = 0 ; i < 20 ; i++ ) head_track_start_mark_blocks[i] = track_start_mark[i % 10];
		uint16_t unknow = 0x00AB;
		uint16_t unknow2 = 0x0210;
		uint8_t cdi_filename_length = cdiname.length();

		cdi.write((const char*)(head_track_start_mark_blocks), 20);
		cdi.write((const char*)(&unknow), 2);
		cdi.write((const char*)(&unknow2), 2);
		cdi.write((const char*)(&cdi_filename_length), 1);
		cdi.write(cdiname.data(), cdi_filename_length);

		write_array_block(cdi, 31, 6, cdi_head_next);
	}

	void write_cdi_head_data_sector( std::ofstream& cdi, long data_sector_count ) {
		uint8_t buf[195];
		fill_buffer(buf, 195, 31, sector2);

		buf[0x06] = data_sector_count;
		buf[0x07] = data_sector_count >> 8;
		buf[0x08] = data_sector_count >> 16;
		buf[0x09] = data_sector_count >> 31;

		buf[0x24] = data_sector_count + 150;
		buf[0x25] = (data_sector_count + 150) >> 8;
		buf[0x26] = (data_sector_count + 150) >> 16;
		buf[0x27] = (data_sector_count + 150) >> 31;

		buf[0x41] = data_sector_count + 150;
		buf[0x42] = (data_sector_count + 150) >> 8;
		buf[0x43] = (data_sector_count + 150) >> 16;
		buf[0x44] = (data_sector_count + 150) >> 31;

		cdi.write((const char*)(buf), 195);
	}

	void write_cdi_head_end( std::ofstream& cdi, const std::string& volumename, long total_cdi_space_used, long cdi_end_image_tracks ) {
		uint8_t volumename_length = volumename.length();

		cdi.write((const char*)(&total_cdi_space_used), 4);
		cdi.write((const char*)(&volumename_length), 1);
		cdi.write(volumename.data(), volumename_length);

		write_array_block(cdi, 42, 4, cdi_header_end);

		long current_pos = cdi.tellp();
		long cdi_header_pos = (current_pos + 4) - cdi_end_image_tracks;
		cdi.write((const char*)(&cdi_header_pos), 4);
	}
}

bool compile_iso( Context& ctx, std::vector<uint8_t>& iso_buffer, uint32_t lba_offset ) {
	// inject high-density TOC metadata into IP.BIN
	if ( !ctx.ip_bin.empty() && lba_offset == 45000 ) {
		if ( ctx.ip_bin.size() >= 32768 ) {
			memcpy(ctx.ip_bin.data() + 0x100, "TOC1", 4);

			uint32_t t3_fad = 45150;
			ctx.ip_bin[0x104] = (t3_fad & 0xFF);
			ctx.ip_bin[0x105] = ((t3_fad >> 8) & 0xFF);
			ctx.ip_bin[0x106] = ((t3_fad >> 16) & 0xFF);
			ctx.ip_bin[0x107] = 0x41;

			for ( int t = 1; t < 97; t++ ) {
				int offset = 0x104 + (t * 4);
				memset(ctx.ip_bin.data() + offset, 0xFF, 4);
			}
		}
	}

	Writer writer(iso_buffer);

	// write IP.BIN (sectors 0 to 15)
	if ( ctx.ip_bin.empty() ) writer.skip(32768);
	else {
		writer.write(ctx.ip_bin);
		if ( writer.offset() < 32768 ) writer.skip(32768 - writer.offset());
	}

	Layout layout;
	layout.solve(ctx.root, 20, lba_offset);

	// write Primary Volume Descriptor (sector 16)
	IsoPvd pvd;
	memset(pvd.system_id, ' ', 32);
	memcpy(pvd.system_id, "SEGA ENTERPRISES", 16);
	memset(pvd.volume_id, ' ', 32);
	memcpy(pvd.volume_id, "DC_GAME", 7);

	pvd.volume_space_size.set(layout.total_sectors + lba_offset);
	pvd.volume_set_size.set(1);
	pvd.volume_sequence_number.set(1);
	pvd.logical_block_size.set(SECTOR_SIZE);

	pvd.path_table_size.set(10);
	pvd.l_path_table_lba = 18 + lba_offset;
	pvd.m_path_table_lba = __builtin_bswap32(19 + lba_offset);

	IsoDirectoryRecord* root_rec = (IsoDirectoryRecord*)(pvd.root_directory_record);
	root_rec->length = 34;
	root_rec->extent_lba.set(20 + lba_offset);
	root_rec->data_length.set(ctx.root.size);
	root_rec->flags = 0x02;
	root_rec->volume_seq_num.set(1);
	root_rec->name_length = 1;
	pvd.root_directory_record[33] = 0;

	writer.write(pvd);

	// write Volume Descriptor Set Terminator (sector 17)
	uint8_t term[SECTOR_SIZE] = {255};
	memcpy(&term[1], "CD001", 5);
	term[6] = 1;
	writer.write(term, SECTOR_SIZE);

	// write L-Path Table (sector 18)
	uint8_t l_path[SECTOR_SIZE] = {1, 0};
	uint32_t root_lba_le = 20 + lba_offset;
	memcpy(&l_path[2], &root_lba_le, 4);
	l_path[6] = 1;
	writer.write(l_path, SECTOR_SIZE);

	// write M-Path Table (sector 19)
	uint8_t m_path[SECTOR_SIZE] = {1, 0};
	uint32_t root_lba_be = __builtin_bswap32(20 + lba_offset);
	memcpy(&m_path[2], &root_lba_be, 4);
	m_path[7] = 1;
	writer.write(m_path, SECTOR_SIZE);

	// write Directory Records (sector 20+)
	time_t t = time(nullptr);
	tm* now = localtime(&t);

	for ( auto* d : layout.dirs ) {
		uint32_t sector_start = writer.offset();

		auto write_entry = [&](uint32_t extent_lba, uint32_t data_len, uint8_t flags, const std::string& iso_name, const std::string& real_name) {
			uint8_t iso_name_len = (iso_name == "\0" || iso_name == "\x01") ? 1 : iso_name.length();

			bool write_nm = (!real_name.empty() && real_name != "." && real_name != "..");
			uint8_t nm_len = 0;
			if ( write_nm ) {
				nm_len = 5 + real_name.length();
			}

			uint8_t base_len = sizeof(IsoDirectoryRecord) + iso_name_len;
			uint8_t pre_nm_pad = 0;
			if ( write_nm && (base_len % 2 != 0) ) {
				pre_nm_pad = 1;
			}

			uint8_t rec_len = base_len + pre_nm_pad + nm_len;
			uint8_t post_rec_pad = 0;
			if ( rec_len % 2 != 0 ) {
				post_rec_pad = 1;
				rec_len++;
			}

			uint32_t current_sec_offset = (writer.offset() - sector_start) % SECTOR_SIZE;
			if ( current_sec_offset + rec_len > SECTOR_SIZE ) {
				writer.skip(SECTOR_SIZE - current_sec_offset);
			}

			IsoDirectoryRecord* rec = writer.reserve<IsoDirectoryRecord>();
			rec->length = rec_len;
			rec->extent_lba.set(extent_lba + lba_offset);
			rec->data_length.set(data_len);
			rec->datetime[0] = now->tm_year;
			rec->datetime[1] = now->tm_mon + 1;
			rec->datetime[2] = now->tm_mday;
			rec->datetime[3] = now->tm_hour;
			rec->datetime[4] = now->tm_min;
			rec->datetime[5] = now->tm_sec;
			rec->flags = flags;
			rec->volume_seq_num.set(1);
			rec->name_length = iso_name_len;

			if ( iso_name == "\0" || iso_name == "\x01" ) {
				uint8_t id_val = iso_name[0];
				writer.write(&id_val, 1);
			} else {
				writer.write(iso_name.data(), iso_name.length());
			}

			if ( write_nm ) {
				if ( pre_nm_pad ) {
					uint8_t pad = 0;
					writer.write(&pad, 1);
				}

				uint8_t nm_header[5] = { 'N', 'M', nm_len, 1, 0 };
				writer.write(nm_header, 5);
				writer.write(real_name.data(), real_name.length());
			}

			if ( post_rec_pad ) {
				uint8_t pad = 0;
				writer.write(&pad, 1);
			}
		};

		write_entry(d->lba, d->size, 0x02, "\0", "");

		uint32_t parent_lba = d->lba;
		uint32_t parent_size = d->size;
		auto it = layout.parent_map.find(d);
		if ( it != layout.parent_map.end() ) {
			parent_lba = it->second->lba;
			parent_size = it->second->size;
		}
		write_entry(parent_lba, parent_size, 0x02, "\x01", "");

		for ( const auto& child : d->children ) {
			std::string real_name = child.physical_path.filename().string();
			write_entry(child.lba, child.size, child.is_directory ? 0x02 : 0x00, child.name, real_name);
		}

		uint32_t written_bytes = writer.offset() - sector_start;
		uint32_t sector_pad = SECTOR_SIZE - (written_bytes % SECTOR_SIZE);
		if ( sector_pad < SECTOR_SIZE ) writer.skip(sector_pad);
	}

	// write file data
	for ( auto* f : layout.files ) {
		writer.seek(f->lba * SECTOR_SIZE);

		std::ifstream file_in(f->physical_path, std::ios::binary);
		if ( !file_in ) return false;

		file_in.seekg(0, std::ios::end);
		size_t size = file_in.tellg();
		file_in.seekg(0, std::ios::beg);

		std::vector<uint8_t> temp_file_buf(size);
		file_in.read((char*)(temp_file_buf.data()), size);

		writer.write(temp_file_buf.data(), size);

		size_t pad_size = SECTOR_SIZE - (size % SECTOR_SIZE);
		if ( pad_size < SECTOR_SIZE ) writer.skip(pad_size);
	}

	// pad out to solved total sectors
	writer.seek(layout.total_sectors * SECTOR_SIZE);
	writer.skip(0); // update offset

	return true;
}

bool write_iso( Context& ctx, const std::string& out_path ) {
	std::vector<uint8_t> iso_buffer;
	if ( !compile_iso(ctx, iso_buffer) ) {
		return false;
	}
	std::ofstream iso_out(out_path, std::ios::binary);
	if ( !iso_out ) {
		return false;
	}
	iso_out.write((const char*)(iso_buffer.data()), iso_buffer.size());
	return true;
}

bool write_cdi( Context& ctx, const std::string& cdi_path ) {
	std::vector<uint8_t> iso_buffer;
	if ( !compile_iso(ctx, iso_buffer) ) return false;

	// verify boot sector
	if ( iso_buffer.size() < 16 ) return false;

	// extract standard ISO Volume Name (at offset 0x8028)
	std::string volume_name = "DC_GAME";
	if ( iso_buffer.size() >= 0x8028 + 32 ) {
		char vol_buf[32] = {0};
		memcpy(vol_buf, iso_buffer.data() + 0x8028, 32);
		std::string raw_vol(vol_buf, 32);
		size_t end = raw_vol.find_last_not_of(" \r\n\t\0");
		if ( end != std::string::npos ) {
			volume_name = raw_vol.substr(0, end + 1);
		}
	}

	std::ofstream cdi_out(cdi_path, std::ios::binary);
	if ( !cdi_out ) return false;

	// write track 1 (Audio Track Header + Zero Stream)
	write_null_block(cdi_out, 352800); // cdi_start_file_header
	write_null_block(cdi_out, 710304); // cdi_audio_track_total_size

	// write pregap tracks (150 gap sectors total of 2336 bytes)
	write_array_block(cdi_out, 2336, 6, gap_dummy_sector);	 // 75 sectors of gap 1
	for (int i = 1; i < 75; i++) write_array_block(cdi_out, 2336, 6, gap_dummy_sector);

	write_array_block(cdi_out, 2336, 101, gap_dummy_sector_2); // 75 sectors of gap 2
	for (int i = 1; i < 75; i++) write_array_block(cdi_out, 2336, 101, gap_dummy_sector_2);

	// wrap and write track 2 sectors directly
	std::vector<uint8_t> cdi_sector(2336, 0);
	uint32_t data_blocks_count = 0;

	// fill standard subheader directly into CDI sector output
	// mode 2 Form 1 subheader: File=00, Chan=00, Submode=08 (Data), Coding=00 (repeated)
	uint8_t sub_hdr[8] = {0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00};
	memcpy(cdi_sector.data(), sub_hdr, 8);

	uint32_t num_sectors = iso_buffer.size() / 2048;
	for ( uint32_t i = 0; i < num_sectors; ++i ) {
		// copy 2048 user bytes directly after the 8-byte subheader
		memcpy(cdi_sector.data() + 8, iso_buffer.data() + (i * 2048), 2048);

		// write raw 2336-byte sector to file (the remaining 280 bytes are zeroed out as EDC/ECC)
		cdi_out.write((const char*)(cdi_sector.data()), 2336);
		data_blocks_count++;
	}

	// cdi4dc forces the data track to be at least 302 sectors
	while ( data_blocks_count < 302 ) {
		write_null_block(cdi_out, 2336);
		data_blocks_count++;
	}

	// write two gap 1 end sectors
	std::vector<uint8_t> end_gap(2336);
	fill_buffer(end_gap.data(), 2336, 6, gap_dummy_sector);
	cdi_out.write((const char*)(end_gap.data()), 2336);
	cdi_out.write((const char*)(end_gap.data()), 2336);

	// write the cdi4dc header blocks (at the end of the file)
	long cdi_end_image_tracks = cdi_out.tellp();

	// CDI V3/V4 header
	uint16_t track_count = 0x02;
	uint16_t first_track_num = 0x01;
	uint32_t padding = 0x00000000;
	cdi_out.write((const char*)(&track_count), 2);
	cdi_out.write((const char*)(&first_track_num), 2);
	cdi_out.write((const char*)(&padding), 4);

	std::string abs_cdi_name = fs::absolute(cdi_path).string();

	// sector 1: audio track metadata
	write_cdi_header_start(cdi_out, abs_cdi_name);
	write_array_block(cdi_out, 195, 28, sector1);

	// sector 2: data track metadata
	write_cdi_header_start(cdi_out, abs_cdi_name);
	write_cdi_head_data_sector(cdi_out, data_blocks_count);

	// sector 3: volume and total size metadata
	long total_cdi_space_used = (301 + 11702 + (data_blocks_count - 1)) - 150;
	write_cdi_header_start(cdi_out, abs_cdi_name);
	write_cdi_head_end(cdi_out, volume_name, total_cdi_space_used, cdi_end_image_tracks);

	cdi_out.close();
	return true;
}

bool write_cue( Context& ctx, const std::string& gdi_path ) {
	return false;
}

bool write_gdi( Context& ctx, const std::string& gdi_path ) {
	fs::path gdi_file(gdi_path);
	fs::path base_dir = gdi_file.parent_path();
	if ( base_dir.empty() ) base_dir = ".";

	std::string base_name = gdi_file.filename().string();
	size_t last_dot = base_name.find_last_of('.');
	if ( last_dot != std::string::npos ) {
		base_name = base_name.substr(0, last_dot);
	}

	std::string t3_name = base_name + "_track03.bin";

	// ISO => Track 3 (offset 45000)
	std::vector<uint8_t> t3_iso_buffer;
	if ( !compile_iso(ctx, t3_iso_buffer, 45000) ) return false;

	std::ofstream t3(base_dir / t3_name, std::ios::binary);
	if ( !t3 ) return false;
	t3.write((const char*)t3_iso_buffer.data(), t3_iso_buffer.size());
	t3.close();

	// write .gdi mapping (maps everything to one file)
	std::ofstream gdi(gdi_file);
	if ( !gdi ) return false;
	gdi << "3\n";
	gdi << "1 150 4 2048 " << t3_name << " 0\n";
	gdi << "2 450 0 2352 " << t3_name << " 0\n";
	gdi << "3 45000 4 2048 " << t3_name << " 0\n";
	gdi.close();

	return true;
}