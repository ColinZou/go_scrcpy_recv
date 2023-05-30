# go-scrcpy-video-receiver
A simple golang lib for receiving and decoding scrcpy frames in png format for Windows only.

It is designed to listen to a tcp port. So it can receive video stream from scrcpy's android app. 

This package can handle multiple scrcpy client(android) connections. 
And you need to add your own callbacks to handle frame images. You can also config the image size so this package will scale the image for you.

Open source software used(check lib folder):

1. ffmpeg
2. opencv

## How to use it?
```go get -u github.com/ColinZou/go_scrcpy_recv@0.2.2```

Check ```examples/basic.go``` for more details. 

## How to build and run?

1. install ms build tool

You also need to have Windows Kits and Visual Studio(Community is ok) installed. Just go to [download visualstudio installer](https://visualstudio.microsoft.com/downloads/). And make sure "Desktop development with C++" is checked before you hit install.

2. install cmake

You need to download from [here](https://cmake.org). Making sure cmake.exe is in your path before you move on.

3. install dependencies with vcpkg

```bash
vcpkg install --recurse --triplet x64-windows-static ffmpeg[avcodec] ffmpeg[x264] ffmpeg[swscale] ffmpeg[avresample] opencv4[png]
```

DO NOT FORGET TO SET VCPKG_ROOT to vcpkg folder.

4. run ```script\build.bat```

5. run build\basic.exe

6. run scripts\adb-prepare.ps1 in powershell

## Note for running the example

There's a modified scrcpy-server (andoird app "scrcpy-server-debug") include inside scripts folder. This modified version supports pass device id as command line argument.
You may need to set environment variable ADB_CMD, point it to path of adb.exe. 

The source code of scrcpy-server-debug can be found [here](github.com/ColinZou/scrcpy).

You need to set environment variable SCRCPY_SAVE_FRAMES=y if you want to save frame images into "images" folder. The images folder will be created in PWD(current directory).



