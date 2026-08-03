<#
.SYNOPSIS
    Provisions dependencies and builds the trailer engine on Windows.

.DESCRIPTION
    Locates an MSYS2 installation, installs the ucrt64 toolchain and library
    packages the engine needs, syncs the OpenOCD submodules, writes a
    CMakeUserPresets.json pinned to the detected toolchain, then configures and
    builds. Re-running is safe; every step is a no-op when already satisfied.

    Nothing is written to the persistent PATH.

.PARAMETER Msys2Root
    MSYS2 installation directory. Autodetected when omitted.

.PARAMETER BuildType
    CMake build type. Defaults to Release.

.PARAMETER Clean
    Delete the build directory before configuring.

.PARAMETER Package
    Run cpack after building to produce the installer and portable archive.

.PARAMETER SkipBuild
    Provision dependencies and write the preset, but stop before configuring.

.EXAMPLE
    .\scripts\bootstrap.ps1

.EXAMPLE
    .\scripts\bootstrap.ps1 -Clean -Package
#>
[CmdletBinding()]
param(
    [string]$Msys2Root,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$BuildType = 'Release',
    [switch]$Clean,
    [switch]$Package,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$EngineRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $EngineRoot 'build'

$Packages = @(
    'mingw-w64-ucrt-x86_64-clang'
    'mingw-w64-ucrt-x86_64-lldb'
    'mingw-w64-ucrt-x86_64-llvm'
    'mingw-w64-ucrt-x86_64-cmake'
    'mingw-w64-ucrt-x86_64-ninja'
    'mingw-w64-ucrt-x86_64-pkgconf'
    'mingw-w64-ucrt-x86_64-xerces-c'
    'mingw-w64-ucrt-x86_64-libusb'
    'mingw-w64-ucrt-x86_64-hidapi'
    'mingw-w64-ucrt-x86_64-libftdi'
    'mingw-w64-ucrt-x86_64-nsis'
)

function Write-Step {
    param([string]$Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Find-Msys2Root {
    param([string]$Explicit)

    $candidates = New-Object System.Collections.Generic.List[string]
    if ($Explicit) { $candidates.Add($Explicit) }
    if ($env:MSYS2_ROOT) { $candidates.Add($env:MSYS2_ROOT) }
    $candidates.Add('C:\msys64')
    $candidates.Add('C:\msys2')

    $uninstallKeys = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    foreach ($key in $uninstallKeys) {
        try {
            Get-ItemProperty $key -ErrorAction Stop |
                Where-Object { $_.DisplayName -like 'MSYS2*' -and $_.InstallLocation } |
                ForEach-Object { $candidates.Add($_.InstallLocation) }
        } catch {}
    }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path (Join-Path $candidate 'usr\bin\pacman.exe'))) {
            return (Resolve-Path $candidate).Path.TrimEnd('\')
        }
    }
    return $null
}

function Invoke-Native {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$FailureMessage
    )
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FailureMessage (exit code $LASTEXITCODE)"
    }
}

Write-Step 'Locating MSYS2'
$msys2 = Find-Msys2Root -Explicit $Msys2Root
if (-not $msys2) {
    throw @'
MSYS2 was not found.

Install it, then re-run this script:

    winget install --id MSYS2.MSYS2

Or pass an existing installation explicitly:

    .\scripts\bootstrap.ps1 -Msys2Root D:\msys64
'@
}
$ucrt64 = Join-Path $msys2 'ucrt64'
$ucrt64Bin = Join-Path $ucrt64 'bin'
$pacman = Join-Path $msys2 'usr\bin\pacman.exe'
Write-Host "    MSYS2:  $msys2"
Write-Host "    ucrt64: $ucrt64"

Write-Step 'Installing ucrt64 packages'
& $pacman -S --needed --noconfirm @Packages
if ($LASTEXITCODE -ne 0) {
    throw @"
pacman failed (exit code $LASTEXITCODE).

If it reported a package as not found, the package database is stale.
Refresh it in an MSYS2 shell and re-run this script:

    pacman -Syu
"@
}

Write-Step 'Syncing submodules'
Invoke-Native -Executable 'git' `
    -Arguments @('-C', $EngineRoot, 'submodule', 'update', '--init', '--recursive') `
    -FailureMessage 'git submodule update failed'

Write-Step 'Writing CMakeUserPresets.json'
$toCMakePath = { param($p) $p -replace '\\', '/' }
$presets = [ordered]@{
    version              = 6
    cmakeMinimumRequired = [ordered]@{ major = 3; minor = 21; patch = 0 }
    configurePresets     = @(
        [ordered]@{
            name           = 'windows-local'
            inherits       = 'windows-msys2-ucrt64'
            displayName    = 'Windows, MSYS2 ucrt64 (bootstrapped)'
            cacheVariables = [ordered]@{
                CMAKE_C_COMPILER   = (& $toCMakePath (Join-Path $ucrt64Bin 'clang.exe'))
                CMAKE_CXX_COMPILER = (& $toCMakePath (Join-Path $ucrt64Bin 'clang++.exe'))
                CMAKE_MAKE_PROGRAM = (& $toCMakePath (Join-Path $ucrt64Bin 'ninja.exe'))
                CMAKE_PREFIX_PATH  = (& $toCMakePath $ucrt64)
            }
        }
    )
}
$presetPath = Join-Path $EngineRoot 'CMakeUserPresets.json'
$presets | ConvertTo-Json -Depth 8 | Set-Content -Path $presetPath -Encoding utf8
Write-Host "    $presetPath"

if ($SkipBuild) {
    Write-Step 'Dependencies ready'
    Write-Host @"
Skipping the build. To configure and build:

    & '$ucrt64Bin\cmake.exe' --preset windows-local -DCMAKE_BUILD_TYPE=$BuildType
    & '$ucrt64Bin\cmake.exe' --build build
"@
    return
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step 'Removing build directory'
    Remove-Item -Recurse -Force $BuildDir
}

$env:PATH = "$ucrt64Bin;$env:PATH"
$cmake = Join-Path $ucrt64Bin 'cmake.exe'
$cpack = Join-Path $ucrt64Bin 'cpack.exe'

Write-Step "Configuring ($BuildType)"
Invoke-Native -Executable $cmake `
    -Arguments @('--preset', 'windows-local', "-DCMAKE_BUILD_TYPE=$BuildType") `
    -FailureMessage 'CMake configure failed'

Write-Step 'Building'
Invoke-Native -Executable $cmake `
    -Arguments @('--build', $BuildDir) `
    -FailureMessage 'Build failed'

if ($Package) {
    Write-Step 'Packaging'
    Push-Location $BuildDir
    try {
        Invoke-Native -Executable $cpack `
            -Arguments @('-C', $BuildType) `
            -FailureMessage 'cpack failed'
    } finally {
        Pop-Location
    }
}

Write-Step 'Done'
Write-Host "    Engine:  $(Join-Path $BuildDir 'trailer-dap.exe')"
if ($Package) {
    Get-ChildItem $BuildDir -Filter 'Trailer-*' -File |
        ForEach-Object { Write-Host "    Package: $($_.FullName)" }
}
