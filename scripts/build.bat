@ECHO OFF
REM SETUP GLOBAL VARS
set SCRIPT_DIR=%~dp0
set GO_SRC_FOLDER=%SCRIPT_DIR%..
set SRCDIR=%GO_SRC_FOLDER%
set CPP_SRC_FOLDER=%SCRIPT_DIR%..\cpp
set LIB_BIN_VS_ROOT_FOLDER=%SCRIPT_DIR%..\lib\scrcpy_recv
set LIB_BIN_TARGET_FOLDER=%SCRIPT_DIR%..\lib\scrcpy_recv_bin
set LIB_BIN_VS_FOLDER=%LIB_BIN_VS_ROOT_FOLDER%\build\x64-release
set CMAKE_WINDOWS_KITS_10_DIR="C:\Program Files (x86)\Windows Kits\10"


echo Looking for vswhere.exe...
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" set "vswhere=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
        echo ERROR: Failed to find vswhere.exe
        exit /b 1
)
echo Found %vswhere%
echo Looking for VC...
for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set vc_dir=%%i
)

if not exist "%vc_dir%\Common7\Tools\vsdevcmd.bat" (
        echo ERROR: Failed to find VC tools x86/x64
        exit /b 1
    )
echo Found %vc_dir%
call "%vc_dir%\Common7\Tools\vsdevcmd.bat" -arch=x86 -host_arch=x64

REM build dll
echo Building cpp &&^
cd %CPP_SRC_FOLDER% &&^
IF NOT EXIST %LIB_BIN_TARGET_FOLDER% (mkdir %LIB_BIN_TARGET_FOLDER%) else (echo %LIB_BIN_TARGET_FOLDER% existed ) &&^
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

