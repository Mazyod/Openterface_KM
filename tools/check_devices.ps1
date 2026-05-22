$OpenOCD = "C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\OpenOCD\OpenOCD\bin\openocd.exe"
$ScriptDir = "C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\OpenOCD\OpenOCD\scripts"
$Firmware = "C:\Users\admin\projects\Openterface_KM\build\openterface-km-firmware.bin"

Write-Host "=== Check for WCH-LinkE programmer ==="
$wchlink = Get-PnpDevice | Where-Object { $_.PNPDeviceID -like "*VID_1A86*" -or $_.PNPDeviceID -like "*WCH*" }
if ($wchlink) {
    $wchlink | Select-Object FriendlyName, Status, PNPDeviceID | Format-List
} else {
    Write-Host "No WCH device found"
}

Write-Host "`n=== All USB devices ==="
Get-PnpDevice -Class USB | Where-Object { $_.Status -eq "OK" } | Select-Object FriendlyName, Status | Format-Table -AutoSize

Write-Host "`n=== Ports ==="
Get-PnpDevice -Class Ports | Where-Object { $_.Status -eq "OK" } | Select-Object FriendlyName, Status | Format-Table -AutoSize
