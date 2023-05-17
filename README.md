# go-scrcpy-video-receiver
A simple golang lib for receiving and decoding scrcpy frames in png format for Windows only.

It is designed to listen to a tcp port. So it can receive video stream from scrcpy's android app. 

This package can handle multiple scrcpy client(android) connections. 
And you need to add your own callbacks to handle frame images. You can also config the image size so this package will scale the image for you.

Open source software used(check lib folder):

1. ffmpeg
2. opencv
3. fmt

## How to use it?
```go get -u github.com/ColinZou/go_scrcpy_recv@0.1.0```

Check ```examples/basic.go``` for more details. 

## How to build and run?
1. run ```script\build.bat```

2. run build\basic.exe

3. run scripts\adb-prepare.ps1 in powershell

## Note for running the example

There's a modified scrcpy-server (andoird app) include inside scripts folder. This modified version supports pass device id as command line argument.
You may need to set environment variable ADB_CMD, point it to path of adb.exe. 


