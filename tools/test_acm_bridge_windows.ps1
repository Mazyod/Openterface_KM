<#
.SYNOPSIS
Test the Mode 3 CDC ACM bridge with both USB ends connected to one Windows PC.

.DESCRIPTION
The script opens the host-side CDC ACM port (VID_1A86 PID_FE0C) and the
target-side CDC ACM port (VID_1A86 PID_FE00), then sends deterministic binary
payloads in both directions at one or more baud rates.

Expected Mode 3 wiring:
- Host CDC ACM FE0C -> target CDC ACM FE00
- Target CDC ACM FE00 -> host CDC ACM FE0C

The current Mode 3 firmware may enumerate before the bridge data path exists.
In that case this script should find both COM ports but fail the transfer test
with a timeout or payload mismatch.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_acm_bridge_windows.ps1

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_acm_bridge_windows.ps1 -BaudRates 9600,115200,921600 -Iterations 5

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_acm_bridge_windows.ps1 -HostPort COM14 -TargetPort COM5
#>

param(
    [string]$HostPort,
    [string]$TargetPort,
    [string[]]$BaudRates = @('9600', '57600', '115200', '460800', '921600'),
    [string[]]$PayloadSizes = @('16', '64', '255', '1024'),
    [ValidateRange(1, 100)]
    [int]$Iterations = 3,
    [ValidateRange(100, 60000)]
    [int]$TimeoutMs = 3000,
    [ValidateRange(0, 5000)]
    [int]$SettleMs = 150,
    [switch]$ListPortsOnly
)

$ErrorActionPreference = 'Stop'

function Convert-IntList {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Values,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [int]$Min,
        [Parameter(Mandatory = $true)]
        [int]$Max
    )

    $items = @()
    foreach ($value in $Values) {
        foreach ($part in ($value -split ',')) {
            $text = $part.Trim()
            if ($text.Length -eq 0) {
                continue
            }

            $number = 0
            if (-not [int]::TryParse($text, [ref]$number)) {
                throw "Invalid $Name value '$text'."
            }
            if ($number -lt $Min -or $number -gt $Max) {
                throw "$Name value $number is outside the allowed range $Min..$Max."
            }
            $items += $number
        }
    }

    if ($items.Count -eq 0) {
        throw "No $Name values were provided."
    }

    return $items
}

function Get-BridgeSerialPorts {
    $ports = @(Get-CimInstance Win32_SerialPort | Where-Object {
        $_.PNPDeviceID -match 'VID_1A86&PID_FE00' -or $_.PNPDeviceID -match 'VID_1A86&PID_FE0C'
    } | Select-Object DeviceID, Description, PNPDeviceID, @{
        Name = 'Role'; Expression = {
            if ($_.PNPDeviceID -match 'VID_1A86&PID_FE0C') { 'Host' }
            elseif ($_.PNPDeviceID -match 'VID_1A86&PID_FE00') { 'Target' }
            else { 'Unknown' }
        }
    })

    return $ports
}

function Resolve-BridgePort {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Ports,
        [Parameter(Mandatory = $true)]
        [ValidateSet('Host', 'Target')]
        [string]$Role,
        [string]$Override
    )

    if ($Override) {
        return $Override
    }

    $matches = @($Ports | Where-Object { $_.Role -eq $Role })
    if ($matches.Count -eq 0) {
        throw "No $Role CDC ACM port found. Expected VID_1A86 PID_$($(if ($Role -eq 'Host') { 'FE0C' } else { 'FE00' }))."
    }

    if ($matches.Count -gt 1) {
        $names = ($matches | ForEach-Object { $_.DeviceID }) -join ', '
        throw "Multiple $Role CDC ACM ports found: $names. Pass -$($Role)Port explicitly."
    }

    return $matches[0].DeviceID
}

function New-TestPayload {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Length,
        [Parameter(Mandatory = $true)]
        [int]$BaudRate,
        [Parameter(Mandatory = $true)]
        [int]$Iteration,
        [Parameter(Mandatory = $true)]
        [ValidateSet('HostToTarget', 'TargetToHost')]
        [string]$Direction
    )

    $payload = New-Object byte[] $Length
    $seed = ($BaudRate -bxor ($Iteration * 257) -bxor $Length -bxor $Direction.Length) -band 0x7FFFFFFF
    for ($index = 0; $index -lt $payload.Length; $index++) {
        $seed = (($seed * 1103515245) + 12345) -band 0x7FFFFFFF
        $payload[$index] = [byte](($seed -shr 8) -band 0xFF)
    }

    $prefixText = 'KM3:{0}:{1}:{2}:{3}:' -f $Direction, $BaudRate, $Iteration, $Length
    $prefix = [System.Text.Encoding]::ASCII.GetBytes($prefixText)
    $copyLength = [Math]::Min($prefix.Length, $payload.Length)
    [Array]::Copy($prefix, 0, $payload, 0, $copyLength)
    return $payload
}

function New-OpenSerialPort {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PortName,
        [Parameter(Mandatory = $true)]
        [int]$BaudRate,
        [Parameter(Mandatory = $true)]
        [int]$Timeout
    )

    $serial = [System.IO.Ports.SerialPort]::new($PortName, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.Handshake = [System.IO.Ports.Handshake]::None
    $serial.ReadTimeout = 100
    $serial.WriteTimeout = $Timeout
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.DiscardOutBuffer()
    return $serial
}

function Read-Exact {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Ports.SerialPort]$Port,
        [Parameter(Mandatory = $true)]
        [int]$Length,
        [Parameter(Mandatory = $true)]
        [int]$Timeout
    )

    $buffer = New-Object byte[] $Length
    $offset = 0
    $timer = [System.Diagnostics.Stopwatch]::StartNew()

    while ($offset -lt $Length -and $timer.ElapsedMilliseconds -lt $Timeout) {
        $available = $Port.BytesToRead
        if ($available -le 0) {
            Start-Sleep -Milliseconds 10
            continue
        }

        $count = [Math]::Min($available, $Length - $offset)
        $read = $Port.Read($buffer, $offset, $count)
        $offset += $read
    }

    if ($offset -ne $Length) {
        throw "Timed out reading $Length bytes from $($Port.PortName); received $offset bytes."
    }

    return $buffer
}

function Assert-BytesEqual {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Expected,
        [Parameter(Mandatory = $true)]
        [byte[]]$Actual
    )

    if ($Expected.Length -ne $Actual.Length) {
        throw "Length mismatch: expected $($Expected.Length), got $($Actual.Length)."
    }

    for ($index = 0; $index -lt $Expected.Length; $index++) {
        if ($Expected[$index] -ne $Actual[$index]) {
            $start = [Math]::Max(0, $index - 8)
            $count = [Math]::Min(16, $Expected.Length - $start)
            $expectedHex = [BitConverter]::ToString($Expected, $start, $count)
            $actualHex = [BitConverter]::ToString($Actual, $start, $count)
            throw "Payload mismatch at byte $index. Expected window: $expectedHex; actual window: $actualHex."
        }
    }
}

function Invoke-DirectionTest {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Ports.SerialPort]$TxPort,
        [Parameter(Mandatory = $true)]
        [System.IO.Ports.SerialPort]$RxPort,
        [Parameter(Mandatory = $true)]
        [ValidateSet('HostToTarget', 'TargetToHost')]
        [string]$Direction,
        [Parameter(Mandatory = $true)]
        [int]$BaudRate,
        [Parameter(Mandatory = $true)]
        [int]$Iteration,
        [Parameter(Mandatory = $true)]
        [int]$PayloadSize,
        [Parameter(Mandatory = $true)]
        [int]$Timeout
    )

    $TxPort.DiscardInBuffer()
    $TxPort.DiscardOutBuffer()
    $RxPort.DiscardInBuffer()
    $RxPort.DiscardOutBuffer()

    $payload = New-TestPayload -Length $PayloadSize -BaudRate $BaudRate -Iteration $Iteration -Direction $Direction
    $TxPort.Write($payload, 0, $payload.Length)
    $received = Read-Exact -Port $RxPort -Length $payload.Length -Timeout $Timeout
    Assert-BytesEqual -Expected $payload -Actual $received
}

function Invoke-TransferCase {
    param(
        [Parameter(Mandatory = $true)]
        [string]$HostPortName,
        [Parameter(Mandatory = $true)]
        [string]$TargetPortName,
        [Parameter(Mandatory = $true)]
        [ValidateSet('HostToTarget', 'TargetToHost')]
        [string]$Direction,
        [Parameter(Mandatory = $true)]
        [int]$BaudRate,
        [Parameter(Mandatory = $true)]
        [int]$Iteration,
        [Parameter(Mandatory = $true)]
        [int]$PayloadSize,
        [Parameter(Mandatory = $true)]
        [int]$Timeout
    )

    $hostSerial = $null
    $targetSerial = $null

    try {
        if ($Direction -eq 'HostToTarget') {
            $hostSerial = New-OpenSerialPort -PortName $HostPortName -BaudRate $BaudRate -Timeout $Timeout
            $targetSerial = New-OpenSerialPort -PortName $TargetPortName -BaudRate $BaudRate -Timeout $Timeout
        }
        else {
            $targetSerial = New-OpenSerialPort -PortName $TargetPortName -BaudRate $BaudRate -Timeout $Timeout
            $hostSerial = New-OpenSerialPort -PortName $HostPortName -BaudRate $BaudRate -Timeout $Timeout
        }

        if ($SettleMs -gt 0) {
            Start-Sleep -Milliseconds $SettleMs
        }

        if ($Direction -eq 'HostToTarget') {
            Invoke-DirectionTest -TxPort $hostSerial -RxPort $targetSerial -Direction $Direction -BaudRate $BaudRate -Iteration $Iteration -PayloadSize $PayloadSize -Timeout $Timeout
        }
        else {
            Invoke-DirectionTest -TxPort $targetSerial -RxPort $hostSerial -Direction $Direction -BaudRate $BaudRate -Iteration $Iteration -PayloadSize $PayloadSize -Timeout $Timeout
        }
    }
    finally {
        if ($hostSerial -and $hostSerial.IsOpen) {
            $hostSerial.Close()
        }
        if ($targetSerial -and $targetSerial.IsOpen) {
            $targetSerial.Close()
        }
    }
}

Write-Host '== CDC ACM Bridge Serial Test =='
$ports = @(Get-BridgeSerialPorts)

if ($ports.Count -eq 0) {
    throw 'No Openterface CDC ACM serial ports found for VID_1A86 PID_FE00 or PID_FE0C.'
}

Write-Host "Detected CDC ACM ports:"
$ports | Sort-Object Role, DeviceID | Format-Table -AutoSize Role, DeviceID, Description, PNPDeviceID | Out-String | Write-Host

if ($ListPortsOnly) {
    exit 0
}

$resolvedHostPort = Resolve-BridgePort -Ports $ports -Role Host -Override $HostPort
$resolvedTargetPort = Resolve-BridgePort -Ports $ports -Role Target -Override $TargetPort
$resolvedBaudRates = @(Convert-IntList -Values $BaudRates -Name 'BaudRates' -Min 1 -Max 4000000)
$resolvedPayloadSizes = @(Convert-IntList -Values $PayloadSizes -Name 'PayloadSizes' -Min 1 -Max 65535)

Write-Host "Host port   : $resolvedHostPort (VID_1A86 PID_FE0C)"
Write-Host "Target port : $resolvedTargetPort (VID_1A86 PID_FE00)"
Write-Host "Baud rates  : $($resolvedBaudRates -join ', ')"
Write-Host "Payloads    : $($resolvedPayloadSizes -join ', ') bytes"
Write-Host "Iterations  : $Iterations"

$failures = @()
$passes = 0

foreach ($baudRate in $resolvedBaudRates) {
    Write-Host "`n== Baud $baudRate =="

    foreach ($direction in @('HostToTarget', 'TargetToHost')) {
        $hostSerial = $null
        $targetSerial = $null

        try {
            if ($direction -eq 'HostToTarget') {
                $hostSerial = New-OpenSerialPort -PortName $resolvedHostPort -BaudRate $baudRate -Timeout $TimeoutMs
                $targetSerial = New-OpenSerialPort -PortName $resolvedTargetPort -BaudRate $baudRate -Timeout $TimeoutMs
            }
            else {
                $targetSerial = New-OpenSerialPort -PortName $resolvedTargetPort -BaudRate $baudRate -Timeout $TimeoutMs
                $hostSerial = New-OpenSerialPort -PortName $resolvedHostPort -BaudRate $baudRate -Timeout $TimeoutMs
            }

            if ($SettleMs -gt 0) {
                Start-Sleep -Milliseconds $SettleMs
            }

            for ($iteration = 1; $iteration -le $Iterations; $iteration++) {
                foreach ($payloadSize in $resolvedPayloadSizes) {
                    try {
                        if ($direction -eq 'HostToTarget') {
                            Invoke-DirectionTest -TxPort $hostSerial -RxPort $targetSerial -Direction $direction -BaudRate $baudRate -Iteration $iteration -PayloadSize $payloadSize -Timeout $TimeoutMs
                        }
                        else {
                            Invoke-DirectionTest -TxPort $targetSerial -RxPort $hostSerial -Direction $direction -BaudRate $baudRate -Iteration $iteration -PayloadSize $payloadSize -Timeout $TimeoutMs
                        }
                        Write-Host "PASS $direction baud=$baudRate iter=$iteration bytes=$payloadSize"
                        $passes++
                    }
                    catch {
                        $message = 'FAIL {0} baud={1} iter={2} bytes={3}: {4}' -f $direction, $baudRate, $iteration, $payloadSize, $_.Exception.Message
                        Write-Host $message -ForegroundColor Red
                        $failures += $message
                    }
                }
            }
        }
        catch {
            for ($iteration = 1; $iteration -le $Iterations; $iteration++) {
                foreach ($payloadSize in $resolvedPayloadSizes) {
                    $message = 'FAIL {0} baud={1} iter={2} bytes={3}: {4}' -f $direction, $baudRate, $iteration, $payloadSize, $_.Exception.Message
                    Write-Host $message -ForegroundColor Red
                    $failures += $message
                }
            }
        }
        finally {
            if ($hostSerial -and $hostSerial.IsOpen) {
                $hostSerial.Close()
            }
            if ($targetSerial -and $targetSerial.IsOpen) {
                $targetSerial.Close()
            }
        }
    }
}

<#
Legacy per-transfer runner kept for reference during bring-up:
foreach ($baudRate in $resolvedBaudRates) {
    for ($iteration = 1; $iteration -le $Iterations; $iteration++) {
        foreach ($payloadSize in $resolvedPayloadSizes) {
            foreach ($direction in @('HostToTarget', 'TargetToHost')) {
                try {
                    Invoke-TransferCase -HostPortName $resolvedHostPort -TargetPortName $resolvedTargetPort -Direction $direction -BaudRate $baudRate -Iteration $iteration -PayloadSize $payloadSize -Timeout $TimeoutMs
                    Write-Host "PASS $direction baud=$baudRate iter=$iteration bytes=$payloadSize"
                    $passes++
                }
                catch {
                    $message = 'FAIL {0} baud={1} iter={2} bytes={3}: {4}' -f $direction, $baudRate, $iteration, $payloadSize, $_.Exception.Message
                    Write-Host $message -ForegroundColor Red
                    $failures += $message
                }
            }
        }
    }
}
#>

Write-Host "`n== Result =="
Write-Host "Passed transfers: $passes"

if ($failures.Count -gt 0) {
    Write-Host "Failures:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host "- $failure"
    }
    exit 1
}

Write-Host 'All serial bridge transfers passed.' -ForegroundColor Green