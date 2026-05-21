$usbDevices = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match '^USB\\VID_1A86' }
foreach ($dev in $usbDevices) {
    $instanceId = $dev.InstanceId
    $props = Get-PnpDeviceProperty -InstanceId $instanceId | Where-Object { $_.KeyName -like '*BusReportedDeviceDesc*' }
    foreach ($p in $props) {
        Write-Host "$instanceId -> $($p.Data)"
    }
}
