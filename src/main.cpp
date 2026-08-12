#include <gd.h>
#include <ip.h>
#include <elf.h>
#include <scramble.h>

#include <cstdio>
#include <ctime>
#include <cstring>
#include <fstream>

namespace {
	// I refuse to compile for C++20
	bool ends_with(const std::string& str, const std::string& suffix) {
		if ( str.length() < suffix.length() ) return false;
		std::string lower_str = str;
		std::string lower_suffix = suffix;
		return lower_str.rfind(lower_suffix) == ( lower_str.length() - lower_suffix.length() );
	}

	std::string get_current_date() {
		time_t t = std::time(nullptr);
		std::tm* now = std::localtime(&t);
		char buf[16];
		strftime(buf, sizeof(buf), "%Y%m%d", now);
		return std::string(buf);
	}

	void verify_track03( const std::string& track03_path ) {
		std::ifstream f(track03_path.c_str(), std::ios::binary);
		if ( !f ) {
			fprintf(stderr, "Verification error: Cannot open %s\n", track03_path.c_str());
			return;
		}

		printf("\n============================================\n");
		printf("\tGD-ROM TRACK 3\n");
		printf("============================================\n");

		// read PVD at offset 32768 (sector 16 of track 3)
		f.seekg(16 * 2048, std::ios::beg);
		IsoPvd pvd;
		f.read((char*)&pvd, sizeof(IsoPvd));

		std::string sys_id(pvd.system_id, 32);
		std::string vol_id(pvd.volume_id, 32);
		printf("[PVD] System Identifier : '%.32s'\n", sys_id.c_str());
		printf("[PVD] Volume Identifier : '%.32s'\n", vol_id.c_str());
		printf("[PVD] Volume Space Size : %u sectors\n", pvd.volume_space_size.le);
		printf("[PVD] L-Path Table LBA  : %u\n", pvd.l_path_table_lba);

		IsoDirectoryRecord* root_rec = (IsoDirectoryRecord*)(pvd.root_directory_record);
		uint32_t root_lba = root_rec->extent_lba.le;
		uint32_t root_size = root_rec->data_length.le;
		printf("[PVD] Root Directory LBA: %u\n", root_lba);
		printf("[PVD] Root Dir Size	 : %u bytes\n", root_size);

		// validate LBA bounds
		if ( root_lba < 45000 ) {
			printf("[!] [ERROR] Root LBA (%u) is below GD-ROM base 45000!\n", root_lba);
			return;
		}

		uint32_t root_file_sector = root_lba - 45000;
		uint64_t root_file_offset = (uint64_t)root_file_sector * 2048;
		printf("[PVD] Root Dir Sector   : %u (Offset: %llu)\n", root_file_sector, (unsigned long long)root_file_offset);

		// read the Root Directory Sector
		f.seekg(root_file_offset, std::ios::beg);
		std::vector<uint8_t> dir_sector(2048);
		f.read((char*)dir_sector.data(), 2048);

		printf("\n--- Root Directory Sector ---\n");
		uint32_t offset = 0;
		bool found_1st_read = false;

		while ( offset < 2048 ) {
			IsoDirectoryRecord* rec = (IsoDirectoryRecord*)(dir_sector.data() + offset);
			if ( rec->length == 0 ) break; // end of directory records

			char name_buf[256] = {0};
			memcpy(name_buf, dir_sector.data() + offset + sizeof(IsoDirectoryRecord), rec->name_length);

			std::string name(name_buf, rec->name_length);
			if ( name[0] == 0 ) name = ".";
			else if ( name[0] == 1 ) name = "..";

			printf("  File: %-20s | LBA: %-6u | Size: %-8u bytes | Flags: 0x%02x\n",
						name.c_str(), rec->extent_lba.le, rec->data_length.le, (int)rec->flags);

			// match 1ST_READ.BIN
			if (name.find("1ST_READ.BIN") != std::string::npos) {
				found_1st_read = true;
				uint32_t file_lba = rec->extent_lba.le;
				uint32_t file_size = rec->data_length.le;
				uint32_t file_sector = file_lba - 45000;
				uint64_t file_offset = (uint64_t)file_sector * 2048;

				printf("  --> Found Boot Binary!\n");
				printf("\t  File Location Sector : %u (Absolute Offset: %llu)\n", file_sector, (unsigned long long)file_offset);

				// peek into the first 16 bytes of the binary
				f.seekg(file_offset, std::ios::beg);
				std::vector<uint8_t> head(16, 0);
				f.read((char*)head.data(), 16);

				printf("\t  Header Magic Peek	: ");
				for ( int i = 0; i < 16; i++ ) {
					printf("%02x ", (int)head[i]);
				}
				printf("\n");

				// checks
				if ( head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F' ) {
					printf("\t  [!] [WARNING] Expected raw binary (BIN) format, got ELF format!\n");
				} else if (file_size == 0) {
					printf("\t  [!] [ERROR] Binary size is 0 bytes!\n");
				} else {
					printf("\t  [i] Binary passed validation.\n");
				}
			}

			offset += rec->length;
		}

		if ( !found_1st_read ) {
			printf("[!] [ERROR] 1ST_READ.BIN was NOT found in the Root Directory!\n");
		}
		printf("============================================\n\n");
	}

	void print_help() {
		printf("Usage: gd-rom.exe [options] [src_dirs...]\n\n"
			   "Options:\n"
			   "  --elf <file>           Input ELF executable to extract and compile.\n"
			   "  --ip <file>            Base IP.BIN template to patch (required if compiling).\n"
			   "  --output <file>        Output file destination (.cdi, .gdi, .iso).\n"
			   "  -g, --title <text>     Game Title to write to IP.BIN (overwrites template).\n"
			   "  -c, --company <text>   Company Name (default: 'KallistiOS').\n"
			   "  -d, --date <YYYYMMDD>  Release date patch (default: Today's date).\n"
			   "  -s, --scramble         Force scramble 1ST_READ.BIN (CDI default).\n"
			   "  -u, --unscramble       Force leave 1ST_READ.BIN plain (GDI default).\n"
			   "  -h, --help             Show this help text.\n"
			   "  -v, --validate         Runs validation on GDIs.\n");
	}
}

int main( int argc, char* argv[] ) {
	std::string elf_path;
	std::string ip_template_path;
	std::string output_path;
	std::string game_title;
	std::string company_name = "KallistiOS";
	std::string release_date = get_current_date().c_str();
	bool validate = false;

	enum ScrambleMode { AUTO, FORCE_SCRAMBLE, FORCE_PLAIN };
	ScrambleMode scramble_mode = AUTO;

	std::vector<std::string> src_dirs;

	Context ctx;
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--elf") {
			if (i + 1 < argc) elf_path = argv[++i];
		} else if (arg == "--ip") {
			if (i + 1 < argc) ip_template_path = argv[++i];
		} else if (arg == "--output") {
			if (i + 1 < argc) output_path = argv[++i];
		} else if (arg == "-g" || arg == "--title") {
			if (i + 1 < argc) game_title = argv[++i];
		} else if (arg == "-c" || arg == "--company") {
			if (i + 1 < argc) company_name = argv[++i];
		} else if (arg == "-d" || arg == "--date") {
			if (i + 1 < argc) release_date = argv[++i];
		} else if (arg == "-s" || arg == "--scramble") {
			scramble_mode = FORCE_SCRAMBLE;
		} else if (arg == "-u" || arg == "--unscramble") {
			scramble_mode = FORCE_PLAIN;
		} else if (arg == "-v" || arg == "--validate") {
			validate = true;
		} else if (arg == "-h" || arg == "--help") {
			print_help();
			return 0;
		} else if (arg == "-i" || arg == "--ignore"  ) {
			if (i + 1 < argc) {
				auto a = argv[++i];
				// is extension
				if ( a[0] == '.' ) {
					// printf("Ignoring extension: '%s'\n", a);
					ctx.filter.skip_extensions.emplace_back( a );
				}
				else {
					// printf("Ignoring path substr: '%s'\n", a);
					ctx.filter.skip_substrings.emplace_back( a );
				}
			}
		} else {
			src_dirs.push_back(arg);
		}
	}

	if ( output_path.empty() ) {
		fprintf(stderr, "Error: Output path unspecified. Run with --help for options.\n");
		return 1;
	}

	// process base IP.BIN template
	if ( ip_template_path.empty() && !ctx.load_bootstrap( IP_DATA, INITIAL_PROGRAM_SIZE ) ) {
		fprintf(stderr, "Failed to load base IP.BIN template\n");
		return 1;
	} else if ( !ip_template_path.empty() && !ctx.load_bootstrap(ip_template_path.c_str()) ) {
		fprintf(stderr, "Failed to load base IP.BIN template from file: %s\n", ip_template_path.c_str());
		return 1;
	}

	// extract ELF and scramble if requested
	fs::path tmp_dir = fs::current_path() / "tmp"; // to-do: skip needing to write to a tempdir and instead pass from memory
	std::vector<uint8_t> raw_bin;
	std::vector<uint8_t> processed_bin;

	if ( !elf_path.empty() ) {
		std::ifstream f(elf_path, std::ios::binary);
		if ( !f ) {
			fprintf(stderr, "Failed to open input ELF file: %s\n", elf_path.c_str());
			return 1;
		}
		f.seekg(0, std::ios::end);
		size_t size = f.tellg();
		f.seekg(0, std::ios::beg);
		std::vector<uint8_t> elf_data(size);
		f.read((char*)elf_data.data(), size);

		if ( !elf_to_binary(elf_data, raw_bin) ) {
			fprintf(stderr, "Error: Failed to map ELF sections into flat binary.\n");
			return 1;
		}
		printf("Successfully extracted %zu byte raw binary from ELF.\n", raw_bin.size());

		// scramble
		bool should_scramble = false;
		if (scramble_mode == FORCE_SCRAMBLE) should_scramble = true;
		else if (scramble_mode == FORCE_PLAIN) should_scramble = false;
		else {
			should_scramble = !ends_with(output_path.c_str(), ".gdi");
		}

		if ( should_scramble ) {
			printf("Scrambling 1ST_READ.BIN content...\n");
			processed_bin = scramble(raw_bin);
		} else {
			printf("Keeping 1ST_READ.BIN flat (unscrambled).\n");
			processed_bin = raw_bin;
		}

		// save temp file for image ctx to read from
		fs::create_directories(tmp_dir);
		std::ofstream out_f(tmp_dir / "1ST_READ.BIN", std::ios::binary);
		out_f.write((const char*)processed_bin.data(), processed_bin.size());
		out_f.close();
	}

	// create IP.BIN metadata
	if ( !ctx.ip_bin.empty() ) {
		ip_initialize_default_headers(ctx.ip_bin);
		if ( !game_title.empty() ) {
			ip_set_field(ctx.ip_bin, 0x80, 128, game_title.c_str());
		}
		ip_set_field(ctx.ip_bin, 0x70, 16, company_name.c_str());
		ip_set_field(ctx.ip_bin, 0x50, 16, release_date.c_str());
		ip_set_field(ctx.ip_bin, 0x60, 16, "1ST_READ.BIN");

		ip_update_crc(ctx.ip_bin);
		printf("IP.BIN created. CRC: %02x%02x%02x%02x\n",
					ctx.ip_bin[0x20], ctx.ip_bin[0x21], ctx.ip_bin[0x22], ctx.ip_bin[0x23]);
	}

	// merge directories
	if ( !elf_path.empty() ) ctx.merge_directory(tmp_dir);
	for ( const auto& dir : src_dirs ) ctx.merge_directory(dir.c_str());

	// build image
	bool success = false;
	if ( ends_with(output_path.c_str(), ".cdi") ) {
		success = write_cdi(ctx, output_path.c_str());
	} else if ( ends_with(output_path.c_str(), ".gdi") ) {
		success = write_gdi(ctx, output_path.c_str());
	} else {
		success = write_iso(ctx, output_path.c_str());
	}

	// cleanup
	if ( fs::exists(tmp_dir) ) fs::remove_all(tmp_dir);

	if ( success ) {
		printf("Successfully generated image to: %s\n", output_path.c_str());

		if ( validate && ends_with(output_path.c_str(), ".gdi") ) {
			std::string base_dir = fs::path(output_path).parent_path().string();
			if (base_dir.empty()) base_dir = ".";
			std::string base_name = fs::path(output_path).stem().string();
			std::string track3_path = base_dir + "/" + base_name + "_track03.bin";

			verify_track03( track3_path.c_str() );
		}
		return 0;
	} else {
		fprintf(stderr, "Generation failed.\n");
		return 1;
	}
}
