package go_scrcpy_recv

/*
#cgo windows CXXFLAGS: -std=c++17 -I${SRCDIR}
#cgo windows LDFLAGS: -L${SRCDIR}/lib/scrcpy_recv_bin -lstdc++ -lscrcpy_recv

#include "scrcpy_recv/scrcpy_recv.h"

#include <stdlib.h>
#include <stdint.h>

extern void c_goScrcpyFrameImageCallback(char *token, char *device_id, uint8_t * img_data, uint32_t img_data_len, scrcpy_rect img_size, scrcpy_rect screen_size);
extern void c_goScrcpyDeviceInfoCallback(char *token, char *device_id, int width, int height);
extern void c_goScrcpyCtrlSendCallback(char *token, char *device_id, char *msg_id, int status, int data_len);
void c_goScrcpyDeviceDisconnectedCallback(char *token, char *device_id, char *con_type);
*/
import "C"
import (
	"fmt"
	"strings"
	"sync"
	"unsafe"
)

type ImageSize struct {
	Width  int
	Height int
}

func (i *ImageSize) String() string {
	return fmt.Sprintf("%dx%d", i.Width, i.Height)
}

type Receiver interface {
	/**
	* start up the receiver
	* CAUTION: this a blocking method
	* @param            listenAddr              receiver's listening address
	* @param            networkBufferSizeKb     netowrk buffer size, recommend 2048KB for lagger screens
	* @param            videoBufferSizeKb       video deocder's buffer size
	 */
	Startup(listenAddr string, networkBufferSizeKb int, videoBufferSizeKb int)

	/**
	 * shutdown the receiver
	 */
	Shutdown()

	/**
	* setup image size for a device
	* @param           deviceId                device's id
	* @param           width                   image width
	* @param           height                  image height
	 */
	SetFrameImageSize(deviceId string, width int, height int)

	/**
	 * Get frame image size configured for a device
	 * @param            deviceId            device's include
	 * @return       the image size struct
	 */
	GetFrameImageSize(deviceId string) *ImageSize

	/**
	 * Get original frame image size configured for a device
	 * @param            deviceId            device's include
	 * @return       the image size struct
	 */
	GetOriginalFrameImageSize(deviceId string) *ImageSize

	/**
	 * Add frame image callback for device
	 * @param            deviceId            device's id
	 * @param            callbackMethod      the callback method. (deviceId, png image data, png image size, screen size) in order.
	 */
	AddFrameImageCallback(deviceId string, callbackMethod func(string, *[]byte, *ImageSize, *ImageSize))

	/**
	 * Remove all frame image callback methods for a device
	 * @param           deviceId            device's id
	 */
	RemoveAllImageCallbacks(deviceId string)

	/**
	 * Add device info callback
	 * @param           deviceId            device's id
	 * @param           callbackMethod      the callback method. (deviceId, screen width, screen height) in order.
	 */
	AddDeviceInfoCallback(deviceId string, callbackMethod func(string, int, int))

	/**
	 * Remove all device info callback methods
	 * @param           deviceId            device's id
	 */
	RemoveAllDeviceInfoCallbacks(deviceId string)
	/**
	 * Get instance token name
	 */
	GetToken() string

	/**
	 * Add controll event send callback
	 * @param       deviceId        the device's identifier
	 * @param       callbackMethod  callback method ref
	 * callbackMethod will receive deviceId, msgId, sendStatus, dataLen in order
	 * dataLen should equals to sendStatus  if send ok
	**/
	AddCtrlEventSendCallback(deviceId string, callbackMethod func(string, string, int, int))
	/**
	 * Remove all callback for controll event sending
	 * @param       deviceId        the device's id
	**/
	RemoveAllCtrlEventSendCallback(deviceId string)

	/**
	 * Send controll events to device
	 * @param       deviceId        the device's identifier
	 * @param       msgId           msg identifier
	 * @param       data            event data
	**/
	SendCtrlEvent(deviceId string, msgId string, data *[]byte)

	/**
	 * Add a callback for device disconnected
	 * @param        deviceId        the device's identifier
	 * @param        callback        the callback method
	 **/
	AddDeviceDisconnectedCallback(deviceId string, callback DeviceDisconnectedCallback)
	/**
	 * Remove all callbacks of a device's disconnected event
	 * @param         deviceId        the device's identifier
	 **/
	RemoveAllDisconnectedCallbck(deviceId string)
}

var globalTokenAndReceiverMap = make(map[string][]*receiver)

// device disconnected callback, arguments will be: token deviceId and connectionType(video/ctrl)
type DeviceDisconnectedCallback func(string, string, string)

type receiver struct {
	r                      C.scrcpy_listener_t
	token                  string
	frameImageCallbacks    map[string][]func(string, *[]byte, *ImageSize, *ImageSize)
	deviceInfoCallbacks    map[string][]func(string, int, int)
	ctrlEventSendCallbacks map[string][]func(string, string, int, int)
	disconnectedCallbacks  map[string][]DeviceDisconnectedCallback
}

func (r *receiver) Startup(listenAddr string, networkBufferSizeKb int, videoBufferSizeKb int) {
	addr := C.CString(listenAddr)
	defer C.free(unsafe.Pointer(addr))
	C.scrcpy_start_receiver(r.r, addr, C.int(networkBufferSizeKb), C.int(videoBufferSizeKb))
}

func (r *receiver) Shutdown() {
	C.scrcpy_shutdown_receiver(r.r)
}

func (r *receiver) SetFrameImageSize(deviceId string, width int, height int) {
	deviceIdCStr := C.CString(deviceId)
	defer C.free(unsafe.Pointer(deviceIdCStr))
	C.scrcpy_set_image_size(r.r, deviceIdCStr, C.int(width), C.int(height))
}
func scrcpyRectToImageSize(from C.struct_scrcpy_rect) *ImageSize {
	return &ImageSize{Width: int(from.width), Height: int(from.height)}
}
func (r *receiver) GetFrameImageSize(deviceId string) *ImageSize {
	deviceIdCStr := C.CString(deviceId)
	defer C.free(unsafe.Pointer(deviceIdCStr))
	cReturn := C.scrcpy_get_cfg_image_size(r.r, deviceIdCStr)
	return scrcpyRectToImageSize(cReturn)
}

func (r *receiver) GetOriginalFrameImageSize(deviceId string) *ImageSize {
	deviceIdCStr := C.CString(deviceId)
	defer C.free(unsafe.Pointer(deviceIdCStr))
	cReturn := C.scrcpy_get_device_image_size(r.r, deviceIdCStr)
	return scrcpyRectToImageSize(cReturn)
}

func (r *receiver) addToGlobalMap() {
	token := r.token
	globalCallbackItems, globalCallbackFound := globalTokenAndReceiverMap[token]
	if globalCallbackFound {
		var found = false
		for _, item := range globalCallbackItems {
			if item == r {
				found = true
				break
			}
		}
		// add the item any when it was not found
		if !found {
			globalCallbackItems = append(globalCallbackItems, r)

		}
	} else {
		globalCallbackItems = make([]*receiver, 1)
		globalCallbackItems[0] = r
	}
	globalTokenAndReceiverMap[token] = globalCallbackItems
}
func (r *receiver) removeFromGlobalMap() {
	// remove from global only when there's no callbacks
	if len(r.deviceInfoCallbacks) == 0 && len(r.frameImageCallbacks) == 0 && len(r.ctrlEventSendCallbacks) == 0 && len(r.disconnectedCallbacks) == 0 {
		delete(globalTokenAndReceiverMap, r.token)
	}
}

func (r *receiver) AddFrameImageCallback(deviceId string, callbackMethod func(string, *[]byte, *ImageSize, *ImageSize)) {
	items, found := r.frameImageCallbacks[deviceId]
	if found {
		items = append(items, callbackMethod)
	} else {
		items = make([]func(string, *[]byte, *ImageSize, *ImageSize), 1)
		items[0] = callbackMethod
		r.addToGlobalMap()
	}
	r.frameImageCallbacks[deviceId] = items

	cDeviceId := C.CString(deviceId)
	defer func() {
		C.free(unsafe.Pointer(cDeviceId))
	}()
	// unregister all callbacks for device first
	C.scrcpy_frame_unregister_all_callbacks(r.r, cDeviceId)

	c_goScrcpyFrameImageCallback := C.scrcpy_frame_img_callback(C.c_goScrcpyFrameImageCallback)
	C.scrcpy_frame_register_callback(r.r, cDeviceId, c_goScrcpyFrameImageCallback)
}

func (r *receiver) RemoveAllImageCallbacks(deviceId string) {
	_, found := r.frameImageCallbacks[deviceId]
	if found {
		delete(r.frameImageCallbacks, deviceId)
		r.removeFromGlobalMap()
		cDeviceId := C.CString(deviceId)
		defer func() {
			C.free(unsafe.Pointer(cDeviceId))
		}()
		C.scrcpy_frame_unregister_all_callbacks(r.r, cDeviceId)
	}
}

func (r *receiver) AddDeviceInfoCallback(deviceId string, callbackMethod func(string, int, int)) {
	items, found := r.deviceInfoCallbacks[deviceId]
	if found {
		r.deviceInfoCallbacks[deviceId] = append(items, callbackMethod)
	} else {
		items = make([]func(string, int, int), 1)
		items[0] = callbackMethod
		r.deviceInfoCallbacks[deviceId] = items
		r.addToGlobalMap()
	}
	cDeviceId := C.CString(deviceId)
	defer func() {
		C.free(unsafe.Pointer(cDeviceId))
	}()
	c_goScrcpyDeviceInfoCallback := C.scrcpy_device_info_callback(C.c_goScrcpyDeviceInfoCallback)
	C.scrcpy_device_info_register_callback(r.r, cDeviceId, c_goScrcpyDeviceInfoCallback)
}

func (r *receiver) RemoveAllDeviceInfoCallbacks(deviceId string) {
	_, found := r.deviceInfoCallbacks[deviceId]
	if found {
		delete(r.deviceInfoCallbacks, deviceId)
		r.removeFromGlobalMap()

		cDeviceId := C.CString(deviceId)
		defer func() {
			C.free(unsafe.Pointer(cDeviceId))
		}()
		C.scrcpy_device_info_unregister_all_callbacks(r.r, cDeviceId)
	}
}

func (r *receiver) GetToken() string {
	return r.token
}
func (r *receiver) release() {
	C.scrcpy_free_receiver(r.r)
}
func (r *receiver) invokeFrameImageCallbacks(deviceId string, imgData *[]byte, imgSize *ImageSize, screenSize *ImageSize) {
	cfgMap := r.frameImageCallbacks
	callbacks, found := cfgMap[deviceId]
	if !found {
		fmt.Printf("No frame image callback configured for device %v, got png size %v %d bytes, screen size is %v\n", deviceId, imgSize, len(*imgData), screenSize)
		return
	}
	// using waitgroup to make sure all callbacks was invoked
	var internalWg sync.WaitGroup
	internalWgPointer := &internalWg
	for _, item := range callbacks {
		internalWg.Add(1)
		callback := item
		go func() {
			defer internalWgPointer.Done()
			callback(deviceId, imgData, imgSize, screenSize)
		}()
	}
	internalWg.Wait()
}
func (r *receiver) invokeDeviceInfoCallbacks(deviceId string, width int, height int) {
	callbacks, found := r.deviceInfoCallbacks[deviceId]
	if !found {
		fmt.Printf("No device info callbacks found for device %s\n", deviceId)
		return
	}
	for _, item := range callbacks {
		callback := item
		go func() {
			callback(deviceId, width, height)
		}()
	}
}

func (r *receiver) AddCtrlEventSendCallback(deviceId string, callbackMethod func(string, string, int, int)) {
	cfgMap := r.ctrlEventSendCallbacks
	callbacks, found := cfgMap[deviceId]
	if !found {
		callbacks = make([]func(string, string, int, int), 1)
		callbacks[0] = callbackMethod
		r.addToGlobalMap()
	} else {
		callbacks = append(callbacks, callbackMethod)
	}
	cfgMap[deviceId] = callbacks
	// register callback with c
	cDeviceId := C.CString(deviceId)
	defer func() {
		C.free(unsafe.Pointer(cDeviceId))
	}()
	c_goScrcpyCtrlSendCallback := C.scrcpy_device_ctrl_msg_send_callback(C.c_goScrcpyCtrlSendCallback)
	C.scrcpy_device_set_ctrl_msg_send_callback(r.r, cDeviceId, c_goScrcpyCtrlSendCallback)
}

func (r *receiver) RemoveAllCtrlEventSendCallback(deviceId string) {
	cfgMap := r.ctrlEventSendCallbacks
	_, found := cfgMap[deviceId]
	if found {
		delete(cfgMap, deviceId)
		r.removeFromGlobalMap()
	}
}

func (r *receiver) SendCtrlEvent(deviceId string, msgId string, data *[]byte) {
	cDeviceId := C.CString(deviceId)
	cMsgId := C.CString(msgId)
	rawData := *data
	cData := unsafe.Pointer(&rawData[0])
	defer func() {
		C.free(unsafe.Pointer(cDeviceId))
		C.free(unsafe.Pointer(cMsgId))
	}()
	dataLen := len(*data)
	fmt.Printf("Sending ctrl event, deviceId=%s msgId=%s dataLen=%d\n", deviceId, msgId, dataLen)
	C.scrcpy_device_send_ctrl_msg(r.r, cDeviceId, cMsgId, (*C.uchar)(cData), C.int(dataLen))
}
func (r *receiver) invokeCtrlEventSendCallbacks(deviceId string, msgId string, sendStatus int, dataLen int) {
	cfgMap := r.ctrlEventSendCallbacks
	callbackHandlers, found := cfgMap[deviceId]
	if !found {
		fmt.Printf("No ctrl event send callback binding for device %s\n", deviceId)
		return
	}
	fmt.Printf("Invoking registered callback in golang deviceId=%s msgId=%s sendStatus=%d dataLen=%d\n", deviceId, msgId, sendStatus, dataLen)
	for _, callback := range callbackHandlers {
		callback(strings.Clone(deviceId), strings.Clone(msgId), sendStatus, dataLen)
	}
}

func (r *receiver) AddDeviceDisconnectedCallback(deviceId string, callback DeviceDisconnectedCallback) {
	cfgMap := r.disconnectedCallbacks
	callbacks, found := cfgMap[deviceId]
	if !found {
		callbacks = make([]DeviceDisconnectedCallback, 1)
		callbacks[0] = callback
		r.addToGlobalMap()
	} else {
		callbacks = append(callbacks, callback)
	}
	cfgMap[deviceId] = callbacks
	// register callback method
	c_goScrcpyDeviceDisconnectedCallback := C.scrcpy_device_disconnected_callback(C.c_goScrcpyDeviceDisconnectedCallback)
	C.scrcpy_set_device_disconnected_callback(r.r, c_goScrcpyDeviceDisconnectedCallback)
}

func (r *receiver) RemoveAllDisconnectedCallbck(deviceId string) {
	cfgMap := r.disconnectedCallbacks
	_, found := cfgMap[deviceId]
	if found {
		delete(cfgMap, deviceId)
		r.removeFromGlobalMap()
	}
}

func (r *receiver) invokeDeviceDisconnectedCallbacks(deviceId string, connectionType string) {
	callbackMethods, found := r.disconnectedCallbacks[deviceId]
	if !found {
		fmt.Printf("No callback method register for token %v\n", r.token)
		return
	}
	for _, method := range callbackMethods {
		m := method
		go func() {
			m(r.token, deviceId, connectionType)
		}()
	}
}

func New(token string) Receiver {
	cToken := C.CString(token)
	res := C.scrcpy_new_receiver(cToken)
	if res == nil {
		return nil
	}
	return &receiver{
		r: res, token: token,
		frameImageCallbacks:    make(map[string][]func(string, *[]byte, *ImageSize, *ImageSize)),
		deviceInfoCallbacks:    make(map[string][]func(string, int, int)),
		ctrlEventSendCallbacks: make(map[string][]func(string, string, int, int)),
		disconnectedCallbacks:  make(map[string][]DeviceDisconnectedCallback),
	}
}
func Release(handle Receiver) {
	handle.(*receiver).release()
}

//export goScrcpyFrameImageCallback
func goScrcpyFrameImageCallback(cToken *C.char, cDeviceId *C.char, cImgData *C.uint8_t, cImgDataLen C.uint32_t, cImgSize C.struct_scrcpy_rect, cScreenSize C.struct_scrcpy_rect) {
	token := C.GoString(cToken)
	deviceId := C.GoString(cDeviceId)
	imgDataLen := int(cImgDataLen)
	receiverList, found := globalTokenAndReceiverMap[token]
	if !found {
		return
	}
	// using WaitGroup to making sure the bytes won't be released before callbacks invoked
	var wg sync.WaitGroup
	imgSize := scrcpyRectToImageSize(cImgSize)
	screenSize := scrcpyRectToImageSize(cScreenSize)
	// copy the bytes into go's ram
	// copied from https://stackoverflow.com/questions/27532523/how-to-convert-1024c-char-to-1024byte
	imgBytes := C.GoBytes(unsafe.Pointer(cImgData), C.int(imgDataLen))
	for _, r := range receiverList {
		wg.Add(1)
		receiveInstance := r
		go func() {
			defer wg.Done()
			receiveInstance.invokeFrameImageCallbacks(deviceId, &imgBytes, imgSize, screenSize)
		}()
	}
	wg.Wait()
}

//export goScrcpyDeviceInfoCallback
func goScrcpyDeviceInfoCallback(cToken *C.char, cDeviceId *C.char, cWidth C.int, cHeight int) {
	token := C.GoString(cToken)
	receiverList, found := globalTokenAndReceiverMap[token]
	if !found {
		return
	}
	deviceId := C.GoString(cDeviceId)
	width := int(cWidth)
	height := int(cHeight)
	for _, r := range receiverList {
		r.invokeDeviceInfoCallbacks(deviceId, width, height)
	}
}

//export goScrcpyCtrlSendCallback
func goScrcpyCtrlSendCallback(cToken *C.char, cDeviceId *C.char, cMsgId *C.char, cStatus C.int, cDataLen C.int) {
	token := C.GoString(cToken)
	receiverList, found := globalTokenAndReceiverMap[token]
	if !found {
		return
	}
	deviceId := C.GoString(cDeviceId)
	msgId := C.GoString(cMsgId)
	for _, r := range receiverList {
		r.invokeCtrlEventSendCallbacks(deviceId, msgId, int(cStatus), int(cDataLen))
	}
}

//export goScrcpyDeviceDisconnectedCallback
func goScrcpyDeviceDisconnectedCallback(cToken *C.char, cDeviceId *C.char, cConnectionType *C.char) {
	token := strings.Clone(C.GoString(cToken))
	receiverList, found := globalTokenAndReceiverMap[token]
	if !found {
		return
	}
	deviceId := strings.Clone(C.GoString(cDeviceId))
	connectionType := strings.Clone(C.GoString(cConnectionType))
	for _, r := range receiverList {
		r.invokeDeviceDisconnectedCallbacks(deviceId, connectionType)
	}
}
