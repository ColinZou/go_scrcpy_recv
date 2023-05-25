#ifndef SCRCPY_SOCKET_LIB
#define SCRCPY_SOCKET_LIB

#include <WinSock2.h>
#include <map>
#include <mutex>
#include "model.h"
#include <vector>
#include "frame_img_callback.h"
#include "scrcpy_ctrl_handler.h"
/*
* Client connection for the video socket
*/
typedef struct ClientConnection {
	// connection buffer config
	struct connection_buffer_config* buffer_cfg;
	// socket handle
	SOCKET client_socket;
    std::string *connection_type = nullptr;
    std::string *device_id = nullptr;
} ClientConnection;

// socket lib for handling server socket and clietn connection
class socket_lib : video_decode_callback {
public:
	socket_lib(std::string token);
	~socket_lib();
	/*
	* register a video image callback handler
	* @param		device_id			the device
	* @param		callback				callback handler(function pointer)
	* @return		not needed
	*/
	int register_callback(char* device_id, frame_callback_handler callback);
	/*
	* unregister a video image callback handler
	* @param		device_id			the device's indentifier
	* @param		callback				callback handler(function pointer)
	*/
	void unregister_callback(char* device_id, frame_callback_handler callback);
	/*
	* remove all callbacks for a device
	* @param		device_id		the device's identifier
	*/
	void remove_all_callbacks(char* device_id);
	/*
	* config the image size from the video image of a device
	* so this lib will resize image to width and height.
	* @param		device_id			the devices' identifier
	* @param		width				image width
	* @param		height				image height
	*/
	void config_image_size(char* device_id, int width, int height);
	/*
	* startup a listener at the address, you can just pass a port no.
	* CAUTION: this is a blocking method, the thread will be blocked until the listener stopped working.
	* @param		address						tcp listener address
	* @param		network_buffer_size_kb		networking buffer size in kb, 2048 KB = 2 MB. 
	* @param		video_packet_buffer_size_kb	video packet buffer size in kb.
	* @return		the server status after the listener ends. 0 means ok.
	*/
	int startup(char* address, int network_buffer_size_kb, int video_packet_buffer_size_kb);
	/*
	* stop accepting new connections and shutdown the socket
	*/
	void shutdown_svr();
	/*
	* global callback entry handler for video image
	* @param		device_id			the device's identifier
	* @param		frame_data			the image data in png format
	* @param		frame_data_size		the image data's length
	* @param		w					the image's width
	* @param		h					the image's height
	* @param		raw_w				the original screen width
	* @param		raw_h				the original scrren height
	*/
	void on_video_callback(char* device_id, uint8_t* frame_data, uint32_t frame_data_size, int w, int h, int raw_w, int raw_h);
	/*
	* get the configured image size for any device
	* @param		device_id			the device's identifier
	* @return		a pointer to ImageSize
	*/
	image_size* get_configured_img_size(char* device_id);
	/*
	* callback handler when getting device info
	* @param		device_id		the device's identifier
	* @param		screen_width		original screen width
	* @param		screen_height	original screen height
	*/
	void on_device_info(char* device_id, int screen_width, int screen_height);
	/*
	* get the original screen size
	* @param		device_id		the device's identifier
	* @return	original screen size of the device
	*/
	image_size* get_original_screen_size(char* device_id);
	/*
	* register a device info callback method
	* @param		device_id		the device's identifier
	* @param		callback			the callback handler(function pointer)
	*/
	void register_device_info_callback(char *device_id, scrcpy_device_info_callback callback);
	/*
	* remove all callbacks of a device
	* @param		device_id		the device's indentifier
	*/
	void unregister_all_device_info_callbacks(char *device_id);
    /**
     * set the callback handler of a device's ctrl message sending
     * @param       device_id       the device's identifier
     * @param       callback        the callback handler
     */
    void set_ctrl_msg_send_callback(char *device_id, scrcpy_device_ctrl_msg_send_callback callback);
    /**
     * send ctrl message to  device
     * @param       device_id       the device's identifier
     * @param       msg_id          the internal msg id for the sender
     * @param       data            the data to send
     * @param       data_len        the length of the data
     */
    void send_ctrl_msg(char *device_id, char *msg_id, uint8_t* data, int data_len);
	
private:
	SOCKET listen_socket = INVALID_SOCKET;
	std::string m_token;
	int keep_accept_connection = 1;
	std::map<std::string, image_size*>* image_size_dict = nullptr;
	std::map<std::string, image_size*>* original_image_size_dict = nullptr;
	std::map<std::string, std::vector<scrcpy_device_info_callback>*>* device_info_callback_dict = nullptr;
    std::map<std::string, scrcpy_ctrl_socket_handler*>* ctrl_socket_handler_map = nullptr;
    std::map<std::string, scrcpy_device_ctrl_msg_send_callback> *ctrl_sending_callback_map = nullptr;

	std::mutex keep_accept_connection_lock;
	std::mutex image_size_lock;
	std::mutex device_info_callback_dict_lock;
    std::mutex ctrl_socket_handler_map_lock;
    std::mutex ctrl_sending_callback_map_lock;

	frame_img_processor *callback_handler = new frame_img_processor();

    

	// internal callback handling
	void internal_video_frame_callback(std::string device_id, uint8_t* frame_data, uint32_t frame_data_size, int w, int h, int raw_w, int raw_h);
	// release image size config
	void free_image_size_dict(std::map<std::string, image_size*>* dict);
	// get image size config for device
	image_size* internal_get_image_size(std::map<std::string, image_size*>* dict, std::string device_id);
	// handle connection
	int handle_connetion(ClientConnection* connection);
	// accept new connection
	int accept_new_connection(connection_buffer_config* cfg);
	/*
	* invoke callback handlers for device info
	* @param		device_id		the devce's id
	* @param		screen_width		original screen width
	* @param		screen_height	original screen height
	*/
	void invoke_device_info_callbacks(char* device_id, int screen_width, int screen_height);
    /**
     * read socket type, it should return video/ctrl
     * @param       connection          the client connection
     * @return  socket type string
     */
    std::string* read_socket_type(ClientConnection* connection);
    /**
     * detect if a connection is a controll socket
     * @param       connection          the client connection
     * @return      true if the connection is a ctrl connection, false otherwise
     */
    bool is_controll_socket(ClientConnection* connection);

    void internal_on_ctrl_msg_sent_callback(std::string device_id, std::string msg_id, int status, int data_len);
};
#endif // !SCRCPY_SOCKET_LIB
