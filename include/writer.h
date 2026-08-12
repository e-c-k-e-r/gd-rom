#pragma once

#include <vector>

class Writer {
private:
	std::vector<uint8_t>& m_buffer;
	uint32_t m_offset;
	bool m_aligned;

	inline void ensure_space( size_t bytes ) {
		if ( m_offset + bytes > m_buffer.size() ) {
			m_buffer.resize( m_offset + bytes );
		}
	}
	// to-do: actually, properly implement this in a way that isn't a headache
	inline void align( size_t ) {
		return;
	/*
		if ( !m_aligned ) return;

		uint32_t misaligned = m_offset % alignment;
		if ( misaligned != 0 ) m_offset += ( alignment - misaligned );
	*/
	}
public:
	Writer( std::vector<uint8_t>& buffer, uint32_t offset = 0, bool aligned = false );
	Writer( std::vector<uint8_t>& buffer, bool aligned );

	inline uint32_t offset() const { return m_offset; }
	inline void seek( uint32_t offset ) { m_offset = offset; }
	inline void skip( size_t bytes ) {
		m_offset += bytes;
		ensure_space(0);
	}


	template<typename T>
	T* reserve( size_t writeSize = sizeof(T) ) {
		align( alignof(T) );
		ensure_space( writeSize );
		T* ptr = reinterpret_cast<T*>( m_buffer.data() + m_offset );
		m_offset += writeSize;
		return ptr;
	}

	template<typename T, typename std::enable_if<!std::is_pointer<T>::value, int>::type = 0>
	bool write( const T& value, size_t writeSize = sizeof(T) ) {
		align( alignof(T) );
		ensure_space( writeSize );
		std::memcpy( m_buffer.data() + m_offset, &value, std::min(sizeof(T), writeSize) );
		m_offset += writeSize;
		return true;
	}

	template<typename T>
	bool write( const T* data, size_t count ) {
		if ( !data || count == 0 ) return false;

	#if UF_ENV_DREAMCAST
		if (m_aligned) align(32);
	#else
		align( alignof(T) );
	#endif

		size_t bytes = count * sizeof(T);
		ensure_space( bytes );

		std::memcpy( m_buffer.data() + m_offset, data, bytes );

		m_offset += bytes;
		return true;
	}

	template<typename T>
	bool write( const std::vector<T>& inArray ) {
		return write( inArray.data(), inArray.size() );
	}
};