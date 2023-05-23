#include "string.h"
#include "utils.h"
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include "fmt/core.h"

int string_compartor(void* a, void* b) {
	return _strcmpi((char*)a, (char*)b);
}
uint64_t to_long(char* from_data, int total_length, int from_index, int size) {
	if (from_index + size > total_length) {
		return -1;
	}
	uint64_t result = 0;
	for (int i = 0; i < size; i++) {
		result <<= 8;
		UINT8 value = (UINT8)from_data[from_index + i];
		result |= value;
	}
	return result;
}
int to_int(char* from_data, int total_length, int from_index, int size) {
	if (from_index + size > total_length) {
		return -1;
	}
	int result = 0;
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
	for (int i = src_start_index; i < copy_length; i++) {
		dest[dest_start_index + i] = src[i];
	}
}
void print_bytes(char *data, int length) {
    std::string buffer;
    bool has_data = false;
    for(int i = 0; i < length; i++) {
        int index = i%8;
        if (index == 0) {
            if (has_data) {
                fmt::print("{}\n", buffer);
            }
            buffer = "";
            has_data = false;
        }
        buffer = fmt::format("{}{}{0:#x}", buffer, index > 0 ? " ": "", (uint8_t)data[i]);
    }
    if (has_data) {
        fmt::print("{}\n", buffer);
    }
}
