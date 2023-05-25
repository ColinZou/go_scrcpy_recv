#include <stdint.h>
#include "model.h"
#include "socket_lib.h"
#include "logging.h"
#include "scrcpy_recv/scrcpy_recv.h"

using namespace std;

SCRCPY_API scrcpy_listener_t scrcpy_new_receiver(char *token) {
	socket_lib* instance = new socket_lib(std::string(token));
	return (scrcpy_listener_t) instance;
}
SCRCPY_API void scrcpy_free_receiver(scrcpy_listener_t handle) {
	delete static_cast<socket_lib*>(handle);
}
SCRCPY_API void scrcpy_start_receiver(scrcpy_listener_t handle, char* listen_address, int net_buffer_size, int video_buffer_size) {
	static_cast<socket_lib*>(handle)->startup(listen_address, net_buffer_size, video_buffer_size);
}

SCRCPY_API void scrcpy_shutdown_receiver(scrcpy_listener_t handle) {
	static_cast<socket_lib*>(handle)->shutdown_svr();
    logging_cleanup();
}

SCRCPY_API void scrcpy_set_image_size(scrcpy_listener_t handle, char* device_id, int width, int height) {
	static_cast<socket_lib*>(handle)->config_image_size(device_id, width, height);
}

SCRCPY_API scrcpy_rect scrcpy_get_cfg_image_size(scrcpy_listener_t handle, char* device_id) {
	auto size = static_cast<socket_lib*>(handle)->get_configured_img_size(device_id);
	if (nullptr == size) {
		return scrcpy_rect{-1, -1};
	}
	return scrcpy_rect{ size->width, size->height };
}

SCRCPY_API scrcpy_rect scrcpy_get_device_image_size(scrcpy_listener_t handle, char* device_id) {
	auto size = static_cast<socket_lib*>(handle)->get_original_screen_size(device_id);
	if (nullptr == size) {
		return scrcpy_rect{ -1, -1 };
	}
	return scrcpy_rect{size->width, size->height};
}

SCRCPY_API void scrcpy_frame_register_callback(scrcpy_listener_t handle, char* device_id, scrcpy_frame_img_callback handler) {
	static_cast<socket_lib*>(handle)->register_callback(device_id, handler);
}

SCRCPY_API void scrcpy_frame_unregister_all_callbacks(scrcpy_listener_t handle, char* device_id) {
	static_cast<socket_lib*>(handle)->remove_all_callbacks(device_id);
}

SCRCPY_API void scrcpy_device_info_register_callback(scrcpy_listener_t handle, char *device_id, scrcpy_device_info_callback handler) {
	static_cast<socket_lib*>(handle)->register_device_info_callback(device_id, handler);
}

SCRCPY_API void scrcpy_device_info_unregister_all_callbacks(scrcpy_listener_t handle, char *device_id) {
	static_cast<socket_lib*>(handle)->unregister_all_device_info_callbacks(device_id);
}

SCRCPY_API void scrcpy_device_set_ctrl_msg_send_callback(scrcpy_listener_t handle, char *device_id, scrcpy_device_ctrl_msg_send_callback callback) {
	static_cast<socket_lib*>(handle)->set_ctrl_msg_send_callback(device_id, callback);
}

SCRCPY_API void scrcpy_device_send_ctrl_msg(scrcpy_listener_t handle, char *device_id, char *msg_id, uint8_t *data, int data_len) {
	static_cast<socket_lib*>(handle)->send_ctrl_msg(device_id, msg_id, data, data_len);
}

SCRCPY_API void scrcpy_set_device_disconnected_callback(scrcpy_listener_t handle, scrcpy_device_disconnected_callback callback) {

}
