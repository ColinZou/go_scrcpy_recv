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

IF /i "%1" == "debug" (
    set SCRCPY_DEBUG_ENABLED=1
)

IF /i "%1" == "trace" (
    set SCRCPY_DEBUG_ENABLED=2
)

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
set BUILD_FOLDER=build
set BUILD_CFG=Release
set ALL_TESTS=test_utils test_frame_img_callback test_scrcpy_ctrl_handler test_scrcpy_support
set ALL_TESTS_CPY=%ALL_TESTS%
FOR %%i in (%ALL_TESTS_CPY%) DO IF /I "%%i" == "%2" SET ALL_TESTS=%%i

echo Building cpp &&^
cd %CPP_SRC_FOLDER% &&^
IF NOT EXIST %LIB_BIN_TARGET_FOLDER% (mkdir %LIB_BIN_TARGET_FOLDER%) else (echo %LIB_BIN_TARGET_FOLDER% existed ) &&^
IF NOT EXIST "%BUILD_FOLDER%" (mkdir "%BUILD_FOLDER%" ) else (echo relase folder is ready && DEL /S /F /Q "%BUILD_FOLDER%") &&^
echo clean built binary first && del /S /F /Q %LIB_BIN_TARGET_FOLDER% &&^
cd %BUILD_FOLDER% && cmake -DCMAKE_BUILD_TYPE=%BUILD_CFG% .. &&^
IF /i "%1" == "runtest" (
echo "Will run test(s) : %ALL_TESTS%" &&^
set SCRCPY_DEBUG_ENABLED='0' &&^
echo Run tests && cmake --build . --target %ALL_TESTS% --config %BUILD_CFG% &&^
ctest -C %BUILD_CFG% -VV --output-on-failure 
) ELSE (
echo Run relase build &&^
cmake --build . --target scrcpy_recv scrcpy_demo_app --config %BUILD_CFG% &&^
echo CPP build succeed &&^
echo COPY BINARYIES FROM %CPP_SRC_FOLDER%\%BUILD_FOLDER%\src\%BUILD_CFG% to %LIB_BIN_TARGET_FOLDER% &&^
xcopy /Y src\%BUILD_CFG%\*.dll %LIB_BIN_TARGET_FOLDER% &&^
xcopy /Y src\%BUILD_CFG%\scrcpy_recv.lib %LIB_BIN_TARGET_FOLDER% &&^
echo ************************ &&^
echo Building go example code &&^
go build -C %GO_SRC_FOLDER% -o build\basic.exe examples\basic.go &&^
XCOPY /Y %LIB_BIN_TARGET_FOLDER%\*.dll %GO_SRC_FOLDER%\build && echo Build OK, binary is "%GO_SRC_FOLDER%\build\basic.exe")

