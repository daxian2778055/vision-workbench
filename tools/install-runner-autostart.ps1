<#
    VisionFlowPlatform: make the self-hosted runner start automatically at logon.

    Why a logon task instead of a Windows service:
      The runner must execute as the same interactive account that owns the development
      environment - Qt is discovered through that user's CMake package registry and the
      build needs access to the drives holding Qt/HALCON/thirdparty. A service running as
      NETWORK SERVICE cannot see either, and a service under the user account would require
      storing that account's password. A logon task needs no password and keeps the
      runner in the right security context.

    Usage:
      powershell -ExecutionPolicy Bypass -File tools/install-runner-autostart.ps1
      powershell -ExecutionPolicy Bypass -File tools/install-runner-autostart.ps1 -Remove
      powershell -ExecutionPolicy Bypass -File tools/install-runner-autostart.ps1 -User Administrator

    The task runs tools/run-runner.ps1, which sanitizes PATH before starting the listener.
#>
[CmdletBinding()]
param(
    [string]$TaskName = 'VFP-ActionsRunner',
    [string]$User = $env:USERNAME,
    [string]$RunnerScript = '',
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'

if ($Remove) {
    $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($existing) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Host ("[ok]   removed scheduled task: " + $TaskName) -ForegroundColor Green
    } else {
        Write-Host ("[skip] no scheduled task named " + $TaskName)
    }
    exit 0
}

if (-not $RunnerScript) {
    $RunnerScript = Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\run-runner.ps1'
}
if (-not (Test-Path $RunnerScript)) {
    Write-Host ("[FAIL] runner script not found: " + $RunnerScript) -ForegroundColor Red
    exit 3
}

$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
    -Argument ('-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + $RunnerScript + '"')
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $User
$principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero)

Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Force `
    -Description 'Start the VisionFlowPlatform self-hosted GitHub Actions runner at logon (sanitizes PATH first).' | Out-Null

Write-Host ("[ok]   registered scheduled task: " + $TaskName) -ForegroundColor Green
Write-Host ("[ok]   runs at logon of " + $User + ", elevated, calling " + $RunnerScript)
Write-Host '[ok]   remove it with: -Remove'
