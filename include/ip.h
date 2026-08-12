#pragma once

#include "config.h"
#include <vector>
#include <string>

void UF_API ip_initialize_default_headers( std::vector<uint8_t>& ip );
void UF_API ip_set_field( std::vector<uint8_t>& ip, size_t offset, size_t length, const std::string& value );
void UF_API ip_update_crc( std::vector<uint8_t>& ip );

#define INITIAL_PROGRAM_SIZE 32768
extern const uint8_t IP_DATA[INITIAL_PROGRAM_SIZE];