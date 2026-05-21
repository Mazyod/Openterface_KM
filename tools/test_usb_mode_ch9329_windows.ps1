<#
.SYNOPSIS
Send Openterface USB mode config command 0x30 over the CH9329 CDC ACM path.

.DESCRIPTION
This talks to the upstream/host CDC ACM management port, VID_1A86 PID_FE0C.
It only works when the active USB mode exposes that CDC ACM interface, currently
runtime modes 1, 2, and 3. Mode 4 exposes CDC ECM instead, so use the HID mode
tool to leave Mode 4 before testing this path.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_usb_mode_ch9329_windows.ps1 -Operation Read

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_usb_mode_ch9329_windows.ps1 -Operation Write -Mode 3

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_usb_mode_ch9329_windows.ps1 -Operation Reboot
#>

param(
    [ValidateSet('Read', 'Write', 'Clear', 'Reboot')]
    [string]$Operation = 'Read',
    [ValidateRange(1, 4)]
    [int]$Mode = 1,
    [ValidateRange(0, 65535)]
    [int]$Flags = 0,
    [string]$Port,
    [ValidateRange(1200, 4000000)]
    [int]$BaudRate = 115200,
    [ValidateRange(100, 30000)]
    [int]$TimeoutMs = 3000,
    [ValidateRange(0, 5000)]
    [int]$SettleMs = 100,
    [switch]$NoResponse,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

$FrameHead1 = [byte]0x57
$FrameHead2 = [byte]0xAB
$DefaultAddr = [byte]0x00
$CmdUsbWorkMode = [byte]0x30

function Resolve-HostCdcAcmPort {
    param([string]$Override)

    if ($Override) {
        return $Override
    }

    $ports = @(Get-CimInstance Win32_SerialPort | Where-Object {
        $_.PNPDeviceID -match 'VID_1A86&PID_FE0C'
    } | Sort-Object DeviceID)

    if ($ports.Count -eq 0) {
        throw 'No Openterface host CDC ACM port found (expected VID_1A86 PID_FE0C). This CH9329 path only works in modes with host CDC ACM.'
    }
    if ($ports.Count -gt 1) {
        $names = ($ports | ForEach-Object { '{0} {1}' -f $_.DeviceID, $_.PNPDeviceID }) -join '; '
        throw "Multiple Openterface host CDC ACM ports found: $names. Pass -Port explicitly."
    }
    return $ports[0].DeviceID
}

function New-Ch9329Frame {
    param(
        [byte]$Addr,
        [byte]$Command,
        [byte[]]$Payload
    )

    if ($null -eq $Payload) {
        $Payload = [byte[]]@()
    }
    if ($Payload.Length -gt 255) {
        throw 'CH9329 payload is too large.'
    }

    $frame = New-Object byte[] (6 + $Payload.Length)
    $frame[0] = $FrameHead1
    $frame[1] = $FrameHead2
    $frame[2] = $Addr
    $frame[3] = $Command
    $frame[4] = [byte]$Payload.Length
    for ($index = 0; $index -lt $Payload.Length; $index++) {
        $frame[5 + $index] = $Payload[$index]
    }

    $sum = 0
    for ($index = 0; $index -lt ($frame.Length - 1); $index++) {
        $sum = ($sum + $frame[$index]) -band 0xFF
    }
    $frame[$frame.Length - 1] = [byte]$sum
    return $frame
}

function Read-ByteWithTimeout {
    param([System.IO.Ports.SerialPort]$Serial)
    try {
        return $Serial.ReadByte()
    }
    catch [System.TimeoutException] {
        return -1
    }
}

function Read-Ch9329Frame {
    param([System.IO.Ports.SerialPort]$Serial)

    $deadline = [DateTime]::UtcNow.AddMilliseconds($Serial.ReadTimeout)
    while ([DateTime]::UtcNow -lt $deadline) {
        $first = Read-ByteWithTimeout -Serial $Serial
        if ($first -lt 0) {
            continue
        }
        if ([byte]$first -ne $FrameHead1) {
            continue
        }

        $second = Read-ByteWithTimeout -Serial $Serial
        if ($second -lt 0) {
            continue
        }
        if ([byte]$second -ne $FrameHead2) {
            continue
        }

        $addr = Read-ByteWithTimeout -Serial $Serial
        $cmd = Read-ByteWithTimeout -Serial $Serial
        $len = Read-ByteWithTimeout -Serial $Serial
        if ($addr -lt 0 -or $cmd -lt 0 -or $len -lt 0) {
            continue
        }

        $payload = New-Object byte[] $len
        for ($index = 0; $index -lt $len; $index++) {
            $value = Read-ByteWithTimeout -Serial $Serial
            if ($value -lt 0) {
                throw 'Timed out reading CH9329 payload.'
            }
            $payload[$index] = [byte]$value
        }

        $checksum = Read-ByteWithTimeout -Serial $Serial
        if ($checksum -lt 0) {
            throw 'Timed out reading CH9329 checksum.'
        }

        $sum = ($FrameHead1 + $FrameHead2 + $addr + $cmd + $len) -band 0xFF
        foreach ($item in $payload) {
            $sum = ($sum + $item) -band 0xFF
        }
        if (($sum -band 0xFF) -ne ([byte]$checksum)) {
            throw ('CH9329 checksum mismatch: expected 0x{0:X2}, got 0x{1:X2}.' -f ($sum -band 0xFF), ([byte]$checksum))
        }

        return [pscustomobject]@{
            Addr = [byte]$addr
            Command = [byte]$cmd
            Length = [byte]$len
            Payload = [byte[]]$payload
        }
    }

    throw 'Timed out waiting for CH9329 response frame.'
}

function Get-OperationPayload {
    switch ($Operation) {
        'Read' { return [byte[]]@(0x00) }
        'Write' { return [byte[]]@(0x01, ($Mode -band 0xFF), ($Flags -band 0xFF), (($Flags -shr 8) -band 0xFF)) }
        'Clear' { return [byte[]]@(0x02) }
        'Reboot' { return [byte[]]@(0x03) }
        default { throw "Unsupported operation $Operation." }
    }
}

function Decode-ModePayload {
    param([byte[]]$Payload)

    $result = [ordered]@{
        last_status = $null
        compiled_mode = $null
        active_mode = $null
        stored_mode = $null
        config_status = $null
        flags = $null
        capability_mask = $null
    }

    if ($Payload.Length -eq 1) {
        $result.last_status = [int]$Payload[0]
        return $result
    }
    if ($Payload.Length -lt 12) {
        throw "Unexpected USB work mode response payload length $($Payload.Length)."
    }

    $result.last_status = [int]$Payload[0]
    $result.compiled_mode = [int]$Payload[1]
    $result.active_mode = [int]$Payload[2]
    $result.stored_mode = [int]$Payload[3]
    $result.config_status = [int]$Payload[4]
    $result.flags = [int]($Payload[5] -bor ($Payload[6] -shl 8))
    $result.capability_mask = [uint32]($Payload[7] -bor ($Payload[8] -shl 8) -bor ($Payload[9] -shl 16) -bor ($Payload[10] -shl 24))
    return $result
}

$resolvedPort = Resolve-HostCdcAcmPort -Override $Port
$serial = [System.IO.Ports.SerialPort]::new($resolvedPort, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.ReadTimeout = $TimeoutMs
$serial.WriteTimeout = $TimeoutMs
$serial.DtrEnable = $true
$serial.RtsEnable = $true

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.DiscardOutBuffer()

    $payload = Get-OperationPayload
    $frame = New-Ch9329Frame -Addr $DefaultAddr -Command $CmdUsbWorkMode -Payload $payload
    $serial.Write($frame, 0, $frame.Length)

    if ($NoResponse) {
        if ($SettleMs -gt 0) {
            Start-Sleep -Milliseconds $SettleMs
        }
        $output = [ordered]@{
            port = $resolvedPort
            operation = $Operation
            no_response = $true
            bytes_written = $frame.Length
        }
        if ($Json) {
            $output | ConvertTo-Json -Depth 4
        }
        else {
            Write-Host "Port: $resolvedPort"
            Write-Host "Operation: $Operation"
            Write-Host "NoResponse: True"
            Write-Host "BytesWritten: $($frame.Length)"
        }
        return
    }

    $response = Read-Ch9329Frame -Serial $serial
    if (($response.Command -band 0x3F) -ne $CmdUsbWorkMode) {
        throw ('Unexpected response command 0x{0:X2}.' -f $response.Command)
    }
    $decoded = Decode-ModePayload -Payload $response.Payload
    $output = [ordered]@{
        port = $resolvedPort
        operation = $Operation
        response_command = ('0x{0:X2}' -f $response.Command)
        last_status = $decoded.last_status
        compiled_mode = $decoded.compiled_mode
        active_mode = $decoded.active_mode
        stored_mode = $decoded.stored_mode
        config_status = $decoded.config_status
        flags = $decoded.flags
        capability_mask = if ($null -ne $decoded.capability_mask) { ('0x{0:X8}' -f $decoded.capability_mask) } else { $null }
    }

    if ($Json) {
        $output | ConvertTo-Json -Depth 4
    }
    else {
        Write-Host "Port: $resolvedPort"
        Write-Host "Operation: $Operation"
        foreach ($key in $output.Keys) {
            if ($key -in @('port', 'operation')) { continue }
            Write-Host ("{0}: {1}" -f $key, $output[$key])
        }
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}