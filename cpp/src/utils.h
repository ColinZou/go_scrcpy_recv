#ifndef SCRCPY_UTILS
#define SCRCPY_UTILS
#include <stdint.h>
#include <string>

/*
* A simple string compare method for sorting
* @param	a		first string
* @param	b		second string
* @return 1 if a should be after b, -1 if a should be before b, 0 other wise.
*/
int string_compartor(void* a, void* b);
/*
* converting bytes to long for scrcpy data from android
* @param	from_data		the data need to convert
* @param	total_length		total length of from_data
* @param	from_index		start index
* @param	size				size of the long
* @return	converted long result, or -1 if the parameters are wrong.
*/
uint64_t to_long(char* from_data, int total_length, int from_index, int size);

/*
* simlimar to to_long, but it only convert from bytes to int
*/
int to_int(char* from_data, int total_length, int from_index, int size);
/*
* copy data from one array to another
* @param	src					data source array
* @param	dest				destination array
* @param	dest_start_index		start index of the dest array for filling data
* @param	copy_length			data length for copying from src into dest
*/
void array_copy_to(char* src, char* dest, const int dest_start_index, const int copy_length);

/*
* copy data from one array to another
* @param	src					data source array
* @param	dest				destination array
* @param    src_start_index     src start index
* @param	dest_start_index	start index of the dest array for filling data
* @param	copy_length			data length for copying from src into dest
*/
void array_copy_to2(char *src, char *dest, int src_start_index, int dest_start_index, int copy_length);
void print_bytes(char *header, char *data, int length);

bool icompare(std::string const& a, std::string const& b);
#endif // !SCRCPY_UTILS
