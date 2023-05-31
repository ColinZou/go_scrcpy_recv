#ifndef FRAME_IMG_CALLBACK_DEF
#define FRAME_IMG_CALLBACK_DEF
#include "model.h"
#include <map>
#include <mutex>
#include <deque>
#include <thread>
#include <Windows.h>

#define PRE_ALLOC_CALLBASCK_SIZE 4
#define MAX_PENDING_FRAMES 4
#define CALLBACK_PARAM_SENT 0
#define CALLBACK_PARAM_EMPTY 1
#define CALLBACK_PARAM_PENDING 2
#define CALLBACK_PARAM_SENDING 2
// callback params for a single frame image
typedef struct frame_img_callback_params {
	// lock
	std::mutex lock;
	// device id
	std::string device_id;
	// the image data
	uint8_t* frame_data = nullptr;
	// token
	char* token = nullptr;
	// the image data length
	uint32_t frame_data_size = 0;
	// the image width
	int w = -1;
	// the image height
	int h = -1;
	// the screen width
	int raw_w = -1;
	//the screen height
	int raw_h = -1;
	// the param status, @see CALLBACK_PARAM_EMPTY CALLBACK_PARAM_PENDING CALLBACK_PARAM_SENDING CALLBACK_PARAM_SENT
	int status = CALLBACK_PARAM_EMPTY;
    // buffer size
    int buffer_size = 0;
} frame_img_callback_params;

// callback setup for a device
typedef struct device_frame_img_callback {
	// the device id
	char * device_id = nullptr;
	// token
	char* token = nullptr;
	// handles configured
	int handler_count = 0;
	// allocated handler array size
	int allocated_handler_space = 0;
	// thread handle for the device
	std::thread::native_handle_type thread_handle = NULL;
	// handlers
	frame_callback_handler* handlers = nullptr;
	// lock object
	std::mutex lock;
	// buffered frames
	std::deque<frame_img_callback_params*> *frames = nullptr;
	// allocated frames for buffering
	int allocated_frames = 0;
	// stopping flag for this device
	int stop = 0;
} device_frame_img_callback;
/*
* image process for device's frames
*/
class frame_img_processor {
private:
	// callback registry
	std::map<std::string, device_frame_img_callback*> *registry = nullptr;
	// locker for the handler
	std::mutex lock;

	/*
	* start callback thread for the device
	* @param			device_id			the device's id
	* @param			handler_container	handle's container object
	* @return			0 if ok
	*/
	int start_callback_thread(char* device_id, device_frame_img_callback* handler_container);
	/*
	* the thread body
	*/
	int callback_thread(device_frame_img_callback* callback_item);

    int calc_buffer_size(int frame_data_size, int current_buffer_size);

    void release_device_img_callback(device_frame_img_callback* callback_item);
public:
	frame_img_processor();
	~frame_img_processor();
	/*
	* add a callback for device
	* @param		device_id		the device's id
	* @param		callback			the callback function 
	* @param		token			server's token
	*/
	void add(char* device_id, frame_callback_handler callback, char *token);
	/*
	* delete a callback for device
	* @param		device_id		the device's id
	* @param		callback			the callback function
	*/
	void del(char* device_id, frame_callback_handler callback);
	/*
	* delete all callbacks for specified device id
	* @param		device_id		the devices' id
	*/
	void del_all(char* device_id);
	/*
	* invoke callback handler(s) for specified device
	* @param		token				token of the server
	* @param		device_id			the device's id
	* @param		frame_data			the image data
	* @param		frame_data_size		data length of the frame image
	* @param		w					image width
	* @param		h					image height
	* @param		raw_w				original screen width
	* @param		raw_h				original screen height
	*/
	void invoke(char * token, char* device_id, uint8_t* frame_data, uint32_t frame_data_size, int w, int h, int raw_w, int raw_h);
};
#endif // !FRAME_IMG_CALLBACK_DEF
