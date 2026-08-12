#include "scramble.h"
#include <cstring>
#include <vector>

namespace {
	static unsigned int seed;
	inline void my_srand(unsigned int n) { seed = n & 0xffff; }
	inline unsigned int my_rand() {
		seed = (seed * 2109 + 9273) & 0x7fff;
		return (seed + 0xc000) & 0xffff;
	}

	constexpr size_t MAXCHUNK = 2048 * 1024;

	void scramble_chunk(const uint8_t* src, uint8_t* dest, size_t sz) {
		size_t slices = sz / 32;
		std::vector<int> idx(slices);
		for (size_t i = 0; i < slices; i++) idx[i] = i;

		size_t out_slice_idx = 0;
		for (int i = static_cast<int>(slices) - 1; i >= 0; --i) {
			int x = (my_rand() * i) >> 16;
			int tmp = idx[i];
			idx[i] = idx[x];
			idx[x] = tmp;

			memcpy(dest + 32 * out_slice_idx, src + 32 * idx[i], 32);
			out_slice_idx++;
		}
	}
}

std::vector<uint8_t> scramble(const std::vector<uint8_t>& input) {
	std::vector<uint8_t> output(input.size());
	size_t filesz = input.size();
	my_srand(static_cast<unsigned int>(filesz));

	const uint8_t* src_ptr = input.data();
	uint8_t* dest_ptr = output.data();

	for ( size_t chunksz = MAXCHUNK; chunksz >= 32; chunksz >>= 1 ) {
		while ( filesz >= chunksz ) {
			scramble_chunk(src_ptr, dest_ptr, chunksz);
			filesz -= chunksz;
			src_ptr += chunksz;
			dest_ptr += chunksz;
		}
	}
	if ( filesz > 0 ) memcpy(dest_ptr, src_ptr, filesz);
	return output;
}