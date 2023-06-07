package main

import (
	"errors"
	"fmt"
	"os"
	"strings"
	"time"

	scrcpy_recv "github.com/ColinZou/go_scrcpy_recv"
)

var listener scrcpy_recv.Receiver
var frameNo = 1
var saveFramesToFiles = false
var deviceId string

const imageFolder string = "images"

func onDeviceInfoCallback(deviceId string, screenWidth int, screenHeight int) {
	fmt.Printf("GOLANG:: Got device info: id=%v width=%v height=%v,  will scale it\n", deviceId, screenWidth, screenHeight)
	listener.SetFrameImageSize(deviceId, screenWidth/3, screenHeight/3)
	time.Sleep(1 * time.Second)
	fmt.Println("GOLANG:: About to send a ctrl event")
}
func sendFakeCtrlEvent() {
	data := make([]byte, 14)
	data[0] = 0
	data[1] = byte(2)
	data[2] = byte(3)
	data[3] = byte(40)
	listener.SendCtrlEvent(deviceId, deviceId, &data)
}
func onFrameImageCallback(deviceId string, imgData *[]byte, imgSize *scrcpy_recv.ImageSize, screenSize *scrcpy_recv.ImageSize) {
	frameNo += 1
	fmt.Printf("GOLANG:: Got frame[%d] %v from %s, screen is %v, img bytes is %d\n", frameNo, imgSize, deviceId, screenSize, len(*imgData))
	if !saveFramesToFiles {
		return
	}
	if _, err := os.Stat(imageFolder); errors.Is(err, os.ErrNotExist) {
		if err = os.MkdirAll(imageFolder, os.ModePerm); err != nil {
			fmt.Printf("Failed to create folder %s\n", imageFolder)
			return
		}
	}
	imgPath := fmt.Sprintf("%s/%03d.png", imageFolder, frameNo)
	if err := os.WriteFile(imgPath, *imgData, os.ModePerm); err != nil {
		fmt.Printf("GOLANG:: Failed to write image %v: %v\n", imgPath, err)
	} else {
		fmt.Printf("GOLAGN:: Wrote frame image %s\n", imgPath)
	}
}
func onCtrlEventSent(deviceId string, msgId string, sendStatus int, dataLen int) {
	fmt.Printf("GOLANG::Invoking ctrl event sent callback, deviceId=%s msgId=%s, sendStatus=%d, dataLen=%d\n", deviceId, msgId, sendStatus, dataLen)
}
func configFromEnv() {
	// read env
	var saveFrameValue, found = os.LookupEnv("SCRCPY_SAVE_FRAMES")
	if !found {
		saveFrameValue = "no"
	}
	saveFramesToFiles = strings.ToLower(saveFrameValue) == "y"
	var msgPrefix = ""
	if saveFramesToFiles {
		msgPrefix = "Will "
	} else {
		msgPrefix = "Will NOT "
	}
	fmt.Printf("GOLANG:: %s save frame images into images/ folder.\n", msgPrefix)
}
func onDeviceDisconnected(token string, deviceId string, connectionType string) {
	fmt.Printf("GOLANG:: %s connection disconected for device %v, token=%v\n", connectionType, deviceId, token)
}

func registerEvents(deviceId string, receiver scrcpy_recv.Receiver) {
	receiver.AddDeviceInfoCallback(deviceId, onDeviceInfoCallback)
	receiver.AddFrameImageCallback(deviceId, onFrameImageCallback)
	receiver.AddCtrlEventSendCallback(deviceId, onCtrlEventSent)
	receiver.AddDeviceDisconnectedCallback(deviceId, onDeviceDisconnected)
}
func unregisterAllEvents(deviceId string, receiver scrcpy_recv.Receiver) {
	receiver.RemoveAllDeviceInfoCallbacks(deviceId)
	receiver.RemoveAllCtrlEventSendCallback(deviceId)
	receiver.RemoveAllImageCallbacks(deviceId)
	receiver.RemoveAllDisconnectedCallbck(deviceId)
}

func run_server() {
	deviceId = "session001"
	receiver := scrcpy_recv.New(deviceId)
	listener = receiver
	configFromEnv()
	receiver.Startup("27183", 2048, 4096)
	scrcpy_recv.Release(receiver, 2)
}

func main() {
	go run_server()
	time.Sleep(time.Second * 1)
	for {
		fmt.Println("What do you want?")
		fmt.Println("0: shutdown scrcpy receiver socket")
		fmt.Println("1: registerEvents")
		fmt.Println("2: unregisterEvents")
		fmt.Println("3: sendFakeCtrlEvent")
		fmt.Println("q: quit")
		var choice string
		n, err := fmt.Scanln(&choice)
		if err != nil {
			_ = fmt.Errorf("GOLANG:: Could not get input: %v\n", err)
			continue
		}
		if n == 0 && len(choice) == 0 {
			_ = fmt.Errorf("GOLANG:: Input needed\n")
			continue
		}
		break_out := false
		switch choice {
		case "0":
			listener.Shutdown()
		case "1":
			registerEvents(deviceId, listener)
		case "2":
			unregisterAllEvents(deviceId, listener)
		case "3":
			sendFakeCtrlEvent()
		case "q":
			break_out = true
		default:
			continue
		}
		if break_out {
			break
		}
	}
}
