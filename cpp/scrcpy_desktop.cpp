// scrcpy_desktop.cpp : Defines the entry point for the application.
//

#include "scrcpy_support.h"
#include "scrcpy_desktop.h"
#include "windows.h"
#include "fmt/core.h"
#include <direct.h>
#include <deque>

using namespace std;
int global_frame_no = 0;
char filename_buf[256];
const char *device_id = "session001";
void test_internal_video_frame_callback(char *token, char* device_id, uint8_t* frame_data, uint32_t frame_data_size, scrcpy_rect img_size,
	scrcpy_rect screen_size) {
	global_frame_no++;
	fmt::print("Got video frame for token = {} device = {} data size = {} scaled from {}x{} to {}x{}\n", token, device_id, frame_data_size, screen_size.width, screen_size.height, img_size.width, img_size.height);
	_mkdir("images");
	sprintf_s(filename_buf, "images/frame-%02d.png", global_frame_no);
	FILE* f;
	errno_t err = fopen_s(&f, filename_buf, "wb");
	if (err) {
		fmt::print("Could not open file {} for write\n", filename_buf);
		return;
	}
	fwrite(frame_data, sizeof(uint8_t), frame_data_size, f);
	fclose(f);
}
void test_deque() {
	std::deque<int> my_queue = { 1, 2, 3 };
	int size = my_queue.size();
	while (size > 0) {
		fmt::print("Got {} \n", my_queue.back());
		my_queue.pop_back();
		size --;
	}
}
void device_info_callback(char *token, char* device_id, int w, int h) {
	fmt::print("device_info_callback device_id={} screen_width={} screen_height={}\n", device_id, w, h);
}
int main(){
	char address[] = "27183";
	int kb_2048 = 1024 * 2;
	fmt::print("Trying to listen at port {}\n", address);
	char token[] = "test";
	scrcpy_listener_t listener = scrcpy_new_receiver(token);
	scrcpy_device_info_register_callback(listener, (char*)device_id, device_info_callback);
	scrcpy_set_image_size(listener, (char*)device_id, 540, 1076);
	scrcpy_frame_register_callback(listener, (char*)device_id, test_internal_video_frame_callback);
	fmt::print("Trying to start listener\n");
	scrcpy_start_receiver(listener, address, kb_2048, kb_2048 * 2);
	free((char*)device_id);
	return 0;
}
