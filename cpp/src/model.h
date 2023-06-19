#ifndef SCRCPY_MODEL_DEFINE
#define SCRCPY_MODEL_DEFINE
#include "stdint.h"
#include "scrcpy_recv/scrcpy_recv.h"
#include <functional>
/*
* Netowork buffer config
*/
typedef struct connection_buffer_config {
	int network_buffer_size_kb;
	int video_packet_buffer_size_kb;
} connection_buffer_config;
/*
* image size
*/
typedef scrcpy_rect image_size;

// frame image callback handler
typedef scrcpy_frame_img_callback frame_callback_handler;

// frame image size configured callback method
typedef std::function<void(char*, scrcpy_rect)> scrcpy_frame_img_size_cfg_callback;


/*
* video decode callback handler class
*/
class video_decode_callback {
public:
	/*
	* video image callback handler
	* @param			device_id				the device's identifier
	* @param			frame_data				frame image data
	* @param			frame_data_size			frame image data length
	* @param			w						image width
	* @param			h						image height
	* @param			raw_w					original screen width
	* @param			raw_h					original screen height
	*/
	virtual void on_video_callback(char* device_id, uint8_t* frame_data, uint32_t frame_data_size, int w, int h, int raw_w, int raw_h) = 0;
	/*
	* get configured image size of a device
	* @param			device_id				the device's identifier
	* @return			image_size data
	*/
	virtual image_size* get_configured_img_size(char* device_id) = 0;
	/*
	* a callback handler for device info
	* @param			device_id				the deivce's identifier
	* @param			screen_width				the device's screen width
	* @param			screen_height			the device's screen height
	*/
	virtual void on_device_info(char* device_id, int screen_width, int screen_height) = 0;
    /*
     * add frame image size configured frame_callback_handler
     * @param       device_id               the device's identifier
     * @param       callback                the callback method
    */
    virtual void add_frame_img_size_cfg_callback(char *device_id, scrcpy_frame_img_size_cfg_callback callback) = 0;
    /**
     * remove frame image size configured frame_callback_handler
     * @param       device_id               the device's identifier
    */
    virtual void remove_frame_img_size_cfg_callback(char *device_id) = 0;
};

#endif // !SCRCPY_MODEL_DEFINE
