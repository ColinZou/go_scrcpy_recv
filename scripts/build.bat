@ECHO OFF
REM SETUP GLOBAL VARS
set SCRIPT_DIR=%~dp0
set GO_SRC_FOLDER=%SCRIPT_DIR%..
set SRCDIR=%GO_SRC_FOLDER%
set CPP_SRC_FOLDER=%SCRIPT_DIR%..\cpp
set LIB_BIN_VS_ROOT_FOLDER=%SCRIPT_DIR%..\lib\scrcpy_recv
set LIB_BIN_TARGET_FOLDER=%SCRIPT_DIR%..\lib\scrcpy_recv_bin
set LIB_BIN_VS_FOLDER=%LIB_BIN_VS_ROOT_FOLDER%\build\x64-release

REM build dll
echo Building cpp &&^
cd %CPP_SRC_FOLDER% &&^
IF NOT EXIST release (mkdir release) else (echo relase folder is ready && DEL /S /F /Q release) &&^
echo clean built binary first && del /S /F /Q %LIB_BIN_TARGET_FOLDER% &&^
cd release && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . --target scrcpy_recv scrcpy_desktop --config Release &&^
echo CPP build succeed &&^
echo COPY BINARYIES FROM %CPP_SRC_FOLDER%\release\Release to %LIB_BIN_TARGET_FOLDER% &&^
xcopy /Y Release\*.dll %LIB_BIN_TARGET_FOLDER% &&^
xcopy /Y Release\*.lib %LIB_BIN_TARGET_FOLDER% &&^
echo ************************ &&^
echo Building go example code &&^
go build -C %GO_SRC_FOLDER% -o build\basic.exe examples\basic.go &&^
XCOPY /Y %LIB_BIN_TARGET_FOLDER%\*.dll %GO_SRC_FOLDER%\build && echo Build OK, binary is %GO_SRC_FOLDER%\build\basic.exe
