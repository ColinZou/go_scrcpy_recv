#ifndef SRCPY_RECV
#define SRCPY_RECV

#ifndef SCRCPY_API
#ifdef WIN_DLL
#define SCRCPY_API __declspec(dllexport)
#else
#define SCRCPY_API extern
#endif
#endif

#include <stdint.h>
#ifdef __cplusplus
// must declare c style header, or golang won't be able to link
extern "C" {
#endif
typedef void *scrcpy_listener_t;

typedef struct scrcpy_rect {
    int width;
    int height;
} scrcpy_rect;

// callback handler for frame image
typedef void (*scrcpy_frame_img_callback) 
    (char *token, char *device_id, uint8_t *img_data, uint32_t img_data_len, scrcpy_rect img_size, scrcpy_rect orig_size);

// callback for device screen size
typedef void (*scrcpy_device_info_callback)
    (char *token, char *device_id, int screen_width, int screen_height);

// callback for sending device's ctrl message
// status will equals to data_len if sents ok.
// status will be -9999 if there's no ctrl socket connected.
typedef void (*scrcpy_device_ctrl_msg_send_callback) (char* token, char *device_id, char *msg_id, int status, int data_len);

/**
 * create a new handle
 *
 * @return the receiver's handle. You need to keep the handle in order to shut it down.
 */
SCRCPY_API scrcpy_listener_t scrcpy_new_receiver(char *token);

/**
 * free a receiver
 * @param       handle          a receiver handle
 */
SCRCPY_API void scrcpy_free_receiver(scrcpy_listener_t handle);

/**
 * Start a receiver for accepting scrcpy video data
 * CAUTION: this is a BLOCKING method, you may want to call it in a thread.
 * @param   listen_address    listen address in order to accept video data
 * @param   net_buffer_size   network buffer size, 2MB for 1080 * 2512 is working so far
 * @param   video_buffer_size decoder buffer size, should be twice as @net_buffer_size
 */
SCRCPY_API void scrcpy_start_receiver(scrcpy_listener_t handle, char* listen_address, int net_buffer_size, int video_buffer_size);

/**
 * Shutdown receiver
 * @param   handle    the receiver handle
 */
SCRCPY_API void scrcpy_shutdown_receiver(scrcpy_listener_t handle);

/**
 * Set image size of received video data
 * This lib will try to scale the image into the width and height
 * @param   handle        the handle
 * @param   device_id     the device
 * @param   width         image width
 * @param   height        image height
 */
SCRCPY_API void scrcpy_set_image_size(scrcpy_listener_t handle, char *device_id, int width, int height);

/**
 * Get configured image size for a device
 * @param   handle        the handle
 * @param   device_id the device
 * @return configured device size
 */
SCRCPY_API scrcpy_rect scrcpy_get_cfg_image_size(scrcpy_listener_t handle, char *device_id);

/**
 * Get original image size reported from scrcpy
 * @param   handle        the handle
 * @param   device_id    the device id
 * @return    original image size reported from device
 */
SCRCPY_API scrcpy_rect scrcpy_get_device_image_size(scrcpy_listener_t handle, char *device_id);

/**
 * Register a callback handler for frame image
 * @param   handle        the handle
 * @param   device_id    device id 
 * @param   handler      the pointer to the callback  
 */
SCRCPY_API void scrcpy_frame_register_callback(scrcpy_listener_t handle, char *device_id, scrcpy_frame_img_callback handler);

/**
 * Remove all callbacks for a device id 
 * @param   handle        the handle
 * @param  device_id device id
 */
SCRCPY_API void scrcpy_frame_unregister_all_callbacks(scrcpy_listener_t handle, char *device_id);

/**
 * Register a callback handler for device info
 * @param       handle      receiver handle
 * @param       device_id   the device's id
 * @param       handler     function pointer to the callback
 */
SCRCPY_API void scrcpy_device_info_register_callback(scrcpy_listener_t handle, char *device_id, scrcpy_device_info_callback handler);

/**
 * Remove all callbacks for a device 
 * @param       handle      receiver handle
 * @param       device_id   the device's id
 */
SCRCPY_API void scrcpy_device_info_unregister_all_callbacks(scrcpy_listener_t handle, char *device_id);

/**
 * Set callback handler of sending ctrl message
 * @param       handler     receiver handle
 * @param       token       the server's token
 * @param       device_id   the device_id
 * @param       callback    the callback method
 */
SCRCPY_API void scrcpy_device_set_ctrl_msg_send_callback(scrcpy_listener_t handle, char *device_id, scrcpy_device_ctrl_msg_send_callback callback);

/**
 * Send a ctrl message to device
 * @param       handler         the receiver handle
 * @param       device_id       the device
 * @param       msg_id          id of the message
 * @param       data            data of the msg
 * @param       data_len        length of the data
 */
SCRCPY_API void scrcpy_device_send_ctrl_msg(scrcpy_listener_t handle, char *device_id, char *msg_id, uint8_t *data, int data_len);

#ifdef __cplusplus
}
#endif

#endif //SRCPY_RECV
