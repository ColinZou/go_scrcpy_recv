package go_scrcpy_recv

/*
#include "scrcpy_recv/scrcpy_recv.h"

#include <stdlib.h>
#include <stdint.h>

extern void goScrcpyFrameImageCallback(char*, char*, uint8_t*, uint32_t, scrcpy_rect, scrcpy_rect);
void c_goScrcpyFrameImageCallback(char * token,char *device_id, uint8_t * img_data, uint32_t img_data_len, scrcpy_rect img_size, scrcpy_rect screen_size) {
    goScrcpyFrameImageCallback(token, device_id, img_data, img_data_len, img_size, screen_size);
}

extern void goScrcpyDeviceInfoCallback(char*, char*, int, int);
void c_goScrcpyDeviceInfoCallback(char *token, char *device_id, int width, int height) {
    goScrcpyDeviceInfoCallback(token, device_id, width, height);
}
*/
import "C"
