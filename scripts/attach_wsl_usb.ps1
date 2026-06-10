# SPDX-License-Identifier: MIT
# Copyright (c) 2026 RenoSeven

param(
    [string]$VidPid = '303a:1001',
    [string]$AutoAttach = '1',
    [switch]$Quiet
)

$ErrorActionPreference = 'Continue'
$script:Quiet = [bool]$Quiet

# --- Constants ---

$AttachRetryAttempts = 5
$AttachRetryDelaySec = 1
$AttachSettleDelaySec = 0.5

$UsbipdCandidates = @(
    "$env:ProgramFiles\usbipd-win\usbipd.exe",
    "${env:ProgramFiles(x86)}\usbipd-win\usbipd.exe"
)

# --- Logging ---

function Write-Err {
    param([string]$Message)
    if (-not $script:Quiet) {
        [Console]::Error.WriteLine($Message)
    }
}

function Write-Info {
    param([string]$Message)
    if ($script:Quiet) {
        return
    }
    [Console]::Out.WriteLine($Message)
}

# --- usbipd ---

function Get-UsbipdPath {
    $cmd = Get-Command usbipd -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    foreach ($path in $UsbipdCandidates) {
        if (Test-Path $path) {
            return $path
        }
    }
    return $null
}

function Invoke-UsbipdAttach {
    param(
        [string]$Usbipd,
        [string]$VidPid,
        [string]$BusId
    )
    if ($BusId) {
        & $Usbipd attach --wsl --busid $BusId *> $null
        if ($LASTEXITCODE -eq 0) {
            return
        }
    }
    & $Usbipd attach --wsl --hardware-id $VidPid *> $null
}

function Wait-AndAttach {
    param(
        [string]$Usbipd,
        [string]$VidPid,
        [string]$BusId
    )

    Start-Sleep -Seconds $AttachSettleDelaySec

    for ($attempt = 1; $attempt -le $AttachRetryAttempts; $attempt++) {
        if (Test-DeviceAttached -Usbipd $Usbipd -VidPid $VidPid) {
            return $true
        }

        $label = if ($attempt -eq 1) { 'Attaching' } else { 'Retrying' }
        Write-Info "$label ($attempt/$AttachRetryAttempts) ..."

        $useBusId = $BusId -and ($attempt -eq $AttachRetryAttempts)
        Invoke-UsbipdAttach -Usbipd $Usbipd -VidPid $VidPid -BusId $(if ($useBusId) { $BusId } else { '' })
        if ($LASTEXITCODE -eq 0 -and (Test-DeviceAttached -Usbipd $Usbipd -VidPid $VidPid)) {
            return $true
        }

        if ($attempt -lt $AttachRetryAttempts) {
            Start-Sleep -Seconds $AttachRetryDelaySec
        }
    }

    return $false
}

# --- Device ---

function Get-Device {
    param(
        [string]$Usbipd,
        [string]$VidPid
    )

    $line = @(& $Usbipd list) | Where-Object { $_ -like "*$VidPid*" } | Select-Object -First 1
    if (-not $line) {
        return $null
    }

    $busId = $null
    if ($line -match '(?<busid>\d+-\d+)') {
        $busId = $Matches.busid
    }

    $state = 'Unknown'
    if ($line -match 'Attached') {
        $state = 'Attached'
    } elseif ($line -match 'Not shared') {
        $state = 'Not shared'
    } elseif ($line -match 'Shared') {
        $state = 'Shared'
    }

    return [pscustomobject]@{
        BusId = $busId
        State = $state
    }
}

function Test-DeviceAttached {
    param(
        [string]$Usbipd,
        [string]$VidPid
    )
    $device = Get-Device -Usbipd $Usbipd -VidPid $VidPid
    return $device -and $device.State -eq 'Attached'
}

function Write-BindInstruction {
    param([string]$BusId)
    Write-Err 'Error: Device is not shared with WSL yet.'
    Write-Err '  Run once in Administrator PowerShell:'
    Write-Err "    usbipd bind --busid $BusId"
    Write-Err '  Then retry attaching the USB device from WSL.'
}

# --- Auto-attach ---

function Get-AutoAttachMarkerPath {
    param([string]$VidPid)
    $safe = $VidPid -replace ':', '-'
    return Join-Path $env:TEMP "serial-bridge-usbipd-auto-attach-$safe.pid"
}

function Test-AutoAttachDaemonRunning {
    param([string]$VidPid)

    $marker = Get-AutoAttachMarkerPath -VidPid $VidPid
    if (Test-Path $marker) {
        $markerPid = Get-Content $marker -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($markerPid -match '^\d+$') {
            $markerProc = Get-Process -Id ([int]$markerPid) -ErrorAction SilentlyContinue
            if ($markerProc -and $markerProc.ProcessName -eq 'usbipd') {
                return $true
            }
        }
        Remove-Item $marker -Force -ErrorAction SilentlyContinue
    }

    $processes = @(Get-CimInstance Win32_Process -Filter "Name = 'usbipd.exe'" -ErrorAction SilentlyContinue)
    foreach ($proc in $processes) {
        $cmd = $proc.CommandLine
        if ($cmd -and $cmd -like '*--auto-attach*' -and $cmd -like "*$VidPid*") {
            return $true
        }
    }

    return $false
}

function Start-AutoAttachDaemon {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [string]$Usbipd,
        [string]$VidPid
    )

    if (Test-AutoAttachDaemonRunning -VidPid $VidPid) {
        return
    }

    $target = "usbipd auto-attach ($VidPid)"
    if (-not $PSCmdlet.ShouldProcess($target, 'Start')) {
        return
    }

    Write-Info 'Starting auto-attach monitor ...'
    $daemon = Start-Process -FilePath $Usbipd -ArgumentList @(
        'attach', '--wsl', '--hardware-id', $VidPid, '--auto-attach'
    ) -WindowStyle Minimized -PassThru
    if ($daemon) {
        $marker = Get-AutoAttachMarkerPath -VidPid $VidPid
        if ($PSCmdlet.ShouldProcess($marker, 'Write marker')) {
            Set-Content -Path $marker -Value $daemon.Id -NoNewline
        }
    }
}

function Enable-AutoAttachIfRequested {
    param(
        [string]$Usbipd,
        [string]$VidPid,
        [string]$AutoAttach
    )
    if ($AutoAttach -ne '0') {
        Start-AutoAttachDaemon -Usbipd $Usbipd -VidPid $VidPid
    }
}

# --- Attach flow ---
#
# 1. Find usbipd
# 2. Look up device by hardware ID
# 3. Already attached  -> enable auto-attach, exit
# 4. Not shared        -> print bind instructions, exit
# 5. Attach via usbipd -> verify, enable auto-attach

$usbipd = Get-UsbipdPath
if (-not $usbipd) {
    Write-Err 'Error: Could not find usbipd on Windows.'
    Write-Err '  Install: winget install dorssel.usbipd-win'
    exit 1
}

$device = Get-Device -Usbipd $usbipd -VidPid $VidPid
if (-not $device) {
    Write-Err "Error: No device matching $VidPid."
    Write-Err '  Check that the board is connected and powered on.'
    exit 1
}

Write-Info "Device $($device.BusId) ($VidPid), state: $($device.State)"

if ($device.State -eq 'Attached') {
    Write-Info 'Already attached to WSL.'
    Enable-AutoAttachIfRequested -Usbipd $usbipd -VidPid $VidPid -AutoAttach $AutoAttach
    exit 0
}

if ($device.State -eq 'Not shared') {
    if (-not $device.BusId) {
        Write-Err 'Error: Could not parse BUSID from usbipd list output.'
        exit 1
    }
    Write-BindInstruction -BusId $device.BusId
    exit 2
}

if (-not (Wait-AndAttach -Usbipd $usbipd -VidPid $VidPid -BusId $device.BusId)) {
    Write-Err 'Error: The usbipd attach command failed.'
    Write-Err '  Keep WSL terminal open, wait a moment after replugging, and retry.'
    exit 1
}

Enable-AutoAttachIfRequested -Usbipd $usbipd -VidPid $VidPid -AutoAttach $AutoAttach
exit 0
