package main

import (
	"errors"
	"fmt"
	"os"

	scrcpy_recv "github.com/ColinZou/go_scrcpy_recv"
)

var listener scrcpy_recv.Receiver
var frameNo = 1

const imageFolder string = "images"

func onDeviceInfoCallback(deviceId string, screenWidth int, screenHeight int) {
	fmt.Printf("Got device info: id=%v width=%v height=%v,  will scale it\n", deviceId, screenWidth, screenHeight)
	listener.SetFrameImageSize(deviceId, screenWidth/3, screenHeight/3)
}
func onFrameImageCallback(deviceId string, imgData []byte, imgSize *scrcpy_recv.ImageSize, screenSize *scrcpy_recv.ImageSize) {
	fmt.Printf("Got frame %v from %s, screen is %v, img bytes is %d\n", imgSize, deviceId, screenSize, len(imgData))
	if _, err := os.Stat(imageFolder); errors.Is(err, os.ErrNotExist) {
		if err = os.MkdirAll(imageFolder, os.ModePerm); err != nil {
			fmt.Printf("Failed to create folder %s\n", imageFolder)
			return
		}
	}
	imgPath := fmt.Sprintf("%s/%03d.png", imageFolder, frameNo)
	if err := os.WriteFile(imgPath, imgData, os.ModePerm); err != nil {
		fmt.Printf("Failed to write image %v: %v\n", imgPath, err)
	} else {
		fmt.Printf("Wrote frame image %s\n", imgPath)
	}
	frameNo += 1
}
func main() {
	deviceId := "session001"
	receiver := scrcpy_recv.New(deviceId)
	listener = receiver
	receiver.AddDeviceInfoCallback(deviceId, onDeviceInfoCallback)
	receiver.AddFrameImageCallback(deviceId, onFrameImageCallback)
	receiver.Startup("27183", 2048, 4096)
	scrcpy_recv.Release(receiver)
}
