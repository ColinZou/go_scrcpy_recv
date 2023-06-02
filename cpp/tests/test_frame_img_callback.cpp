#include "logging.h"
#include "frame_img_callback.h"
#include <queue>
#include <mutex>
#include "Windows.h"

std::string test_token = "123";
std::string test_device_id = "456";
uint8_t data[] = {1,2,3};
uint32_t data_len = 3;
scrcpy_rect current_img_size = scrcpy_rect {
    100,
    100
};
scrcpy_rect original_img_size = scrcpy_rect {
    200,
    200
};

std::mutex global_lock;
std::queue<bool> passed_flags;

void frame_img_callback_handler(char *token, char *device_id, uint8_t *img_data, uint32_t img_data_len, scrcpy_rect img_size, scrcpy_rect orig_size) {
    std::lock_guard<std::mutex> lock(global_lock);
    SPDLOG_INFO("Got a frame image callback, token={}, device_id={}, img_data_len={}, img_size.width={}, img_size.height={}" 
            " orig_size.width={}, orig_size.height={}", 
            token ,device_id,
            img_data_len, img_size.width, 
            img_size.height, orig_size.width,
            orig_size.height);
    log_flush();
    auto is_correct = strcmp((char *)test_token.c_str(), token) == 0 &&
        strcmp((char *)test_device_id.c_str(), device_id) == 0 &&
        strcmp((char *)&data, (char *)img_data) == 0 &&
        img_data_len == data_len && 
        img_size.width == current_img_size.width && img_size.height == current_img_size.height &&
        orig_size.width == original_img_size.width && orig_size.height == original_img_size.height;
    assert(is_correct);
    passed_flags.push(is_correct);
}

void test_setup_callback(frame_img_processor *processor) {
    auto device_id = test_device_id;
    auto token = test_token;

    SPDLOG_DEBUG("Trying to register a callback");
    log_flush();
    processor->add((char *)device_id.c_str(), frame_img_callback_handler, (char *) token.c_str());

    SPDLOG_DEBUG("Trying to unregister a callback");
    log_flush();
    processor->del((char *)device_id.c_str(), frame_img_callback_handler);

    SPDLOG_DEBUG("Trying to register a callback again");
    log_flush();
    processor->add((char *)device_id.c_str(), frame_img_callback_handler, (char *) token.c_str());

    SPDLOG_DEBUG("Trying to unregister all callback this time");
    log_flush();
    processor->del_all((char *)device_id.c_str());
}
void test_callback(frame_img_processor *processor) {
    auto device_id = test_device_id;
    auto token = test_token;

    SPDLOG_DEBUG("Trying to register a callback again");
    log_flush();
    processor->add((char *)device_id.c_str(), frame_img_callback_handler, (char *) token.c_str());

    // performing tests
    SPDLOG_DEBUG("Sending a frame");
    log_flush();
    processor->invoke((char *)token.c_str(), (char *)device_id.c_str(), data, data_len, 100, 100, 200, 200);

    SPDLOG_DEBUG("Wait for frame callback");
    log_flush();
    bool got_result = false;
    for(int i = 0; i < 10; i ++) {
        size_t queue_size = 0;
        {
            std::lock_guard<std::mutex> lock(global_lock);
            queue_size = passed_flags.size();
        }
        if (queue_size <= 0) {
            SPDLOG_DEBUG("No callback received, wait for 100ms");
            log_flush();
            Sleep(100);
            continue;
        }
        std::lock_guard<std::mutex> lock(global_lock);
        auto value = passed_flags.front();
        SPDLOG_DEBUG("Is callback correct ? {}", value ? "yes, it is correct." : "no, it is wrong.");
        got_result = true;
        log_flush();
        if(!value) {
            SPDLOG_DEBUG("Trying to unregister all callback this time");
            log_flush();
            processor->del_all((char *)device_id.c_str());
        }
        assert(value);
        break;
    }
    if (!got_result) {
        SPDLOG_DEBUG("Timed out waiting for result");
        SPDLOG_DEBUG("Trying to unregister all callback this time");
        log_flush();
        processor->del_all((char *)device_id.c_str());
        assert(false);
    }
    
    SPDLOG_DEBUG("Trying to unregister all callback this time");
    log_flush();
    processor->del_all((char *)device_id.c_str());
}
int main() {
    SPDLOG_INFO("test_utils");
    log_flush();
    frame_img_processor *img_processor = new frame_img_processor();
    test_setup_callback(img_processor);
    test_callback(img_processor);
    delete img_processor;
    // wait the callback thread to shutdown
    Sleep(100);
    logging_cleanup();
    return 0;
}
