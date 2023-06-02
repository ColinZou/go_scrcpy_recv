#include "utils.h"
#include "assert.h"
#include "logging.h"

void test_string_compartor() {
    SPDLOG_INFO("test_string_compartor");
    log_flush();
    char *str_a = "123";
    assert(string_compartor(NULL, (void *)str_a) == 0);
    assert(string_compartor((void *)str_a, NULL) == 0);
    assert(string_compartor(NULL, NULL) == 0);
    assert(string_compartor((void *)str_a, (void *)"123") == 0);
    assert(string_compartor((void *)str_a, (void *)"023") > 0);
    assert(string_compartor((void *)str_a, (void *)"423") < 0);
}
void test_to_long() {
    SPDLOG_INFO("test_to_long");
    log_flush();
    char zero[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert(to_long(NULL, 0, 0, 0) == 0);
    assert(to_long(zero, 1, 1, 1) == 0);
    assert(to_long(zero, 8, 0, 8) == 0);

    assert(to_long(zero, 1, 0, 1) == 0);

    char one[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    auto one_value = to_long(one, 8, 0, 8);
    assert(one_value == 1);
    
    unsigned char highest[] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto higest_value = to_long((char *)highest, 8, 0, 8);
    uint64_t one_u64 = ((uint64_t)1) << 63;
    assert(higest_value== one_u64);
}
void test_to_int() {
    SPDLOG_INFO("test_to_int");
    log_flush();
    char zero[] = {0x00, 0x00, 0x00, 0x00};
    assert(to_int(NULL, 0, 0, 0) == 0);
    assert(to_int(zero, 1, 1, 1) == 0);
    assert(to_int(zero, 4, 0, 1) == 0);
    assert(to_int(zero, 4, 0, 5) == 0);

    char one[] = {0x00, 0x00, 0x00, 0x01};
    auto one_value = to_int(one, 4, 0, 4);
    assert(one_value == 1);

    unsigned char high[] = {0x80, 0x00, 0x00, 0x00};
    auto high_value = to_int((char*)high, 4, 0, 4);
    assert(high_value == (((uint32_t)1) << 31));
}

void test_array_copy_to() {
    SPDLOG_INFO("test_array_copy_to");
    log_flush();
    std::string a_str = "abcDef"; 
    std::string b_str = "hijKlm";
    std::string b_str_oroginal = std::string(b_str);
    array_copy_to((char*)a_str.c_str(), (char *)b_str.c_str(), 0, 6);
    assert(strcmp(a_str.c_str(), b_str.c_str()) == 0);
    assert(strcmp(a_str.c_str(), b_str_oroginal.c_str()) != 0);
}

int main() {
    SPDLOG_INFO("test_utils");
    log_flush();
    test_string_compartor();
    test_to_long();
    test_to_int();
    test_array_copy_to();
    logging_cleanup();
    return 0;
}
