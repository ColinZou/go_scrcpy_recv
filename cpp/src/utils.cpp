#include "string.h"
#include "utils.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include "logging.h"

int string_compartor(void* a, void* b) {
    if (!a || !b) {
        return 0;
    }
    return _strcmpi((char*)a, (char*)b);
}
uint64_t to_long(char* from_data, int total_length, int from_index, int size) {
    if (!from_data || size > 8 || size <= 0 || from_index < 0) {
        return 0;
    }
    if (from_index + size > total_length) {
        return 0;
    }
    uint64_t result = 0;
    for (int i = 0; i < size; i++) {
        result <<= 8;
        UINT8 value = (UINT8)from_data[from_index + i];
        result |= value;
    }
    return result;
}
uint32_t to_int(char* from_data, int total_length, int from_index, int size) {
    if (!from_data || size > 4 || size <= 0 || from_index < 0) {
        return 0;
    }
    if (from_index + size > total_length) {
        return 0;
    }
    uint32_t result = 0;
    for (int i = 0; i < size; i++) {
        result <<= 8;
        UINT8 value = (UINT8)from_data[from_index + i];
        result |= value;
    }
    return result;
}
void array_copy_to(char* src, char* dest, const int dest_start_index, const int copy_length) {
    for (int i = 0; i < copy_length; i++) {
        dest[dest_start_index + i] = src[i];
    }
}
void array_copy_to2(char *src, char *dest, int src_start_index, int dest_start_index, int copy_length) {
    for (int i = 0; i < copy_length; i++) {
        dest[dest_start_index + i] = src[src_start_index + i];
    }
}
void print_bytes(char *header, char *data, int length) {
    char *buffer = (char*)malloc(sizeof(char) * 128);
    bool has_data = false;
    for(int i = 0; i < length; i++) {
        int index = i%8;
        if (index == 0) {
            if (has_data) {
                SPDLOG_DEBUG("{} {}", header, buffer);
            }
            memset(buffer, 0, 128);
            has_data = false;
        }
        sprintf_s(buffer, 128, "%s%s0x%02X", buffer, index > 0 ? " ": "", (uint8_t)data[i]);
        has_data = true;
    }
    if (has_data) {
        SPDLOG_DEBUG("{} {}", header, buffer);
    }
    free(buffer);
}

bool icompare_pred(unsigned char a, unsigned char b) {
    return std::tolower(a) == std::tolower(b);
}

bool icompare(std::string const& a, std::string const& b) {
    if (a.length() == b.length()) {
        return std::equal(b.begin(), b.end(),
                a.begin(), icompare_pred);
    }
    else {
        return false;
    }
}
