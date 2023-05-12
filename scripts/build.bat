@ECHO OFF
REM SETUP GLOBAL VARS
set SCRIPT_DIR=%~dp0
set GO_SRC_FOLDER=%SCRIPT_DIR%..
set SRCDIR=%GO_SRC_FOLDER%
set CPP_SRC_FOLDER=%SCRIPT_DIR%..\cpp
set LIB_BIN_VS_ROOT_FOLDER=%SCRIPT_DIR%..\lib\scrcpy_recv
set LIB_BIN_TARGET_FOLDER=%SCRIPT_DIR%..\lib\scrcpy_recv_bin
set LIB_BIN_VS_FOLDER=%LIB_BIN_VS_ROOT_FOLDER%\build\x64-release

echo COPY BINARYIES FROM %LIB_BIN_VS_FOLDER% to %LIB_BIN_TARGET_FOLDER%
xcopy /Y %LIB_BIN_VS_FOLDER%\*.dll %LIB_BIN_TARGET_FOLDER%
xcopy /Y %LIB_BIN_VS_FOLDER%\*.lib %LIB_BIN_TARGET_FOLDER%

echo ************************
echo Building go example code
REM set CGO_CXXFLAGS='-std=c++17 -I%SRCDIR%'
REM set CGO_LDFLAGS='-L%SRCDIR%/lib/ffmpeg-n5.1.3-win64-gpl-shared-5.1/lib -L%SRCDIR%/lib/fmt-9.1.0/lib -L%SRCDIR%/lib/opencv/x64/vc16/lib -L%SRCDIR%/lib/scrcpy_recv_bin -lstdc++ -lwsock32 -lws2_32 -lavcodec -lavformat -lavutil -lswscale -lswresample -lfmt -lopencv_world470 -lscrcpy_recv'
go build -C %GO_SRC_FOLDER% -o build\basic.exe examples\basic.go &&^
XCOPY /Y %LIB_BIN_TARGET_FOLDER%\*.dll %GO_SRC_FOLDER%\build && echo Build OK, binary is %SCRIPT_DIR%\build\basic.exe
