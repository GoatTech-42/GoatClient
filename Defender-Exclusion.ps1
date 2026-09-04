# GoatClient - Defender exclusion helper
# Run as Administrator. Adds the GoatClient folder to Windows Defender exclusions
# so the DLL-injecting launcher is not blocked by real-time protection.
# Usage: right-click -> Run with PowerShell (as admin), or from an elevated shell:
#   powershell -ExecutionPolicy Bypass -File .\Defender-Exclusion.ps1

$ErrorActionPreference = 'Stop'

# Self-elevate if not already admin.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Requesting administrator elevation..."
    $args = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    Start-Process powershell.exe -Verb RunAs -ArgumentList $args
    exit
}

$folder = Split-Path -Parent $PSCommandPath
Write-Host "Adding Defender exclusion for: $folder"

try {
    Add-MpPreference -ExclusionPath $folder -ErrorAction Stop
    Write-Host "SUCCESS: folder excluded from Windows Defender."
}
catch {
    Write-Warning "Could not add exclusion: $($_.Exception.Message)"
    Write-Host "You may need to add it manually:"
    Write-Host "  Windows Security -> Virus & threat protection -> Manage settings"
    Write-Host "  -> Exclusions -> Add or remove exclusions -> Add an exclusion -> Folder"
    Write-Host "  -> select: $folder"
}

# Also exclude the two binaries explicitly (belt and suspenders).
foreach ($name in @('GoatClient.exe', 'GoatClientNative.dll')) {
    $path = Join-Path $folder $name
    if (Test-Path -LiteralPath $path) {
        try { Add-MpPreference -ExclusionPath $path -ErrorAction SilentlyContinue; Write-Host "Excluded: $path" }
        catch {}
    }
}

Write-Host ""
Write-Host "Done. GoatClient will no longer be blocked by Defender."
Write-Host "Press any key to exit..."
$null = Read-Host
