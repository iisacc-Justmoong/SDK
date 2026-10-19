$ErrorActionPreference = 'Stop'
$workspaceTools = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'Tools'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'MSVC C++ Build Tools installation not found.' }
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$cmakeBin = Get-ChildItem (Join-Path $workspaceTools 'CMake') -Directory | Sort-Object Name -Descending | Select-Object -First 1 | ForEach-Object { Join-Path $_.FullName 'bin' }
$pythonBin = Join-Path $workspaceTools 'Python314/tools'
$env:PATH = (@($cmakeBin, (Join-Path $workspaceTools 'Ninja'), $pythonBin, (Join-Path $pythonBin 'Scripts'), 'C:/Program Files/Git/cmd', 'C:/Program Files/Git/bin', 'C:/Program Files/nodejs') + @($env:PATH)) -join ';'
$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:CMAKE_GENERATOR = 'Ninja'
$env:CURL_SSL_BACKEND = 'openssl'
$env:CL = '/utf-8 /Zc:__cplusplus /bigobj /DNOMINMAX'
$env:VSLANG = '1033'
$localSdkRoot = Join-Path $env:USERPROFILE '.local/SDK'
$qtMsvcRoot = 'C:/Qt/6.8.3/msvc2022_64'
$vcpkgRoot = Join-Path $workspaceTools 'vcpkg'
$vcpkgPrefix = Join-Path $vcpkgRoot 'installed/x64-windows'
$sdkPrefixes = @(Get-ChildItem -LiteralPath $localSdkRoot -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'node_modules' } | ForEach-Object { $_.FullName })
$env:CMAKE_PREFIX_PATH = (@($qtMsvcRoot, $vcpkgPrefix) + $sdkPrefixes) -join ';'
$runtimePaths = @((Join-Path $qtMsvcRoot 'bin'), (Join-Path $vcpkgPrefix 'bin'), (Join-Path $workspaceTools 'Dokan/sdk/bin/x64'), (Join-Path $workspaceTools 'OpenSSL/bin'), (Join-Path $vcpkgPrefix 'tools/pkgconf'))
$runtimePaths += @($sdkPrefixes | ForEach-Object { Join-Path $_ 'bin' })
$env:PATH = (($runtimePaths | Where-Object { Test-Path -LiteralPath $_ }) + @($env:PATH)) -join ';'
$env:VCPKG_ROOT = $vcpkgRoot
$env:PKG_CONFIG_PATH = Join-Path $vcpkgPrefix 'lib/pkgconfig'
$env:IIFILEPROVIDER_PREFIX = Join-Path $localSdkRoot 'iiFileProvider'
$env:DOKAN_ROOT = Join-Path $workspaceTools 'Dokan/sdk'
$env:OPENSSL_CONF = Join-Path $workspaceTools 'OpenSSL/openssl.cnf'
$officeBin = Join-Path $workspaceTools 'LibreOffice/SourceDir/LibreOffice/program'
if (Test-Path -LiteralPath $officeBin) { $env:PATH = $env:PATH + ';' + $officeBin }
$ffmpegBin = Join-Path $workspaceTools 'FFmpeg/ffmpeg-9.0.2-essentials_build/bin'
$env:LC_ALL = 'C'
$env:CMAKE_PREFIX_PATH = $env:CMAKE_PREFIX_PATH.Replace('\','/')
$env:IIFILEPROVIDER_PREFIX = $env:IIFILEPROVIDER_PREFIX.Replace('\','/')
$env:DOKAN_ROOT = $env:DOKAN_ROOT.Replace('\','/')
$env:VCPKG_ROOT = $env:VCPKG_ROOT.Replace('\','/')
$env:PKG_CONFIG_PATH = $env:PKG_CONFIG_PATH.Replace('\','/')
$ffmpegSdkRoot = Join-Path $workspaceTools 'FFmpeg/SDK9/ffmpeg-n9.0-latest-win64-lgpl-shared-9.0'
if (Test-Path -LiteralPath $ffmpegSdkRoot) {
    $env:FFMPEG_SDK_ROOT = $ffmpegSdkRoot.Replace('\','/')
    $env:PKG_CONFIG_PATH = $env:FFMPEG_SDK_ROOT + '/lib/pkgconfig;' + $env:PKG_CONFIG_PATH
    $env:PATH = $env:FFMPEG_SDK_ROOT + '/bin;' + $env:PATH
}
if (Test-Path -LiteralPath $ffmpegBin) { $env:PATH = $ffmpegBin + ';' + $env:PATH }
Write-Host 'Windows x64 MSVC development environment ready.'
