$adbCmd = $env:ADB_CMD ?? "adb"
$reversePortNumber = $env:ADB_REVERSE_PORT ?? "27183"
$scrcpyServerPath = $env:SCRCPY_SERVER_PACKAGE ?? "${PSScriptRoot}\scrcpy-server-debug"
function IsReversePortMapped {
    param (
        $adbExe,
        $Port
    )
    $mappedPort = & $adbCmd reverse --list | findstr 27183
    return ($mappedPort ?? "").contains($reversePortNumber)
}
$filePathList = $adbCmd,$scrcpyServerPath
Foreach ($filePath in $filePathList) {
    if (-not (Test-Path -Path $filePath -PathType leaf)){
        echo "$filePath was not existed."
        exit 1
    }
}
echo "Will use adb '${adbCmd}' port '${reversePortNumber}' scrcpy-server '${scrcpyServerPath}'"
$isPortMapped=& IsReversePortMapped $adbCmd $reversePortNumber
if ($isPortMapped) {
    echo "The reverse port is working"
} else {
    echo "port $reversePortNumber was not mapped"
    & $adbCmd reverse "localabstract:scrcpy" "tcp:$reversePortNumber"
    $isPortMapped = & IsReversePortMapped $adbCmd $reversePortNumber
    if (!$isPortMapped) {
        echo "The reverse port is not mapped"
        exit 1
    }
}
echo "$adbCmd push $scrcpyServerPath  /sdcard/temp/scrcpy-server"
& $adbCmd  push $scrcpyServerPath  /sdcard/temp/scrcpy-server
if ($LASTEXITCODE -gt 0) {
    echo "Failed to copy scrcpy-server to proper location."
} else {
    & $adbCmd shell CLASSPATH=/sdcard/temp/scrcpy-server app_process / com.genymobile.scrcpy.Server 1.25 stay_awake=true max_fps=2 device_name=session001
}

