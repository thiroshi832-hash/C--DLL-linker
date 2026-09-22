# ===========================================================================
# build.ps1 — build every piece and assemble a runnable ./dist folder.
#
# Target runtime: Windows 7 SP1 -> .NET Framework 4.8 (newest CLR that runs
# there). Build on a normal Win 10/11 dev box; run the ./dist output on Win 7.
#
# Prerequisites on PATH (build machine):
#   * .NET SDK (any recent 6/7/8) WITH the .NET Framework 4.8 targeting pack,
#     or the .NET Framework 4.8 Developer Pack + MSBuild (dotnet build net48)
#                                                 -> https://dotnet.microsoft.com
#   * CMake 3.16+             (cmake)
#   * MinGW-w64 gcc           (gcc, mingw32-make) — the same kit your Qt uses
#   * Qt 5/6 for MinGW        (Qt6::Core / Qt5::Core discoverable by CMake)
#
# On the Windows 7 SP1 target: the .NET Framework 4.8 runtime, plus the Qt and
# MinGW runtime DLLs deployed next to the exe (see README).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File build.ps1
#   powershell -File build.ps1 -QtPrefix "C:\Qt\6.7.0\mingw_64"
# ===========================================================================
param(
    [string]$QtPrefix = "",             # e.g. C:\Qt\6.7.0\mingw_64
    [string]$Config   = "Release"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

Write-Host "==> [1/4] Building sample ThirdParty.dll (stand-in for your fixed DLL)" -ForegroundColor Cyan
dotnet build "$root\sample-thirdparty-dll\ThirdParty.csproj" -c $Config -o "$dist"

Write-Host "==> [2/4] Building ManagedBridge.dll (the C# dispatcher)" -ForegroundColor Cyan
dotnet build "$root\managed-bridge\ManagedBridge.csproj" -c $Config -o "$dist"

Write-Host "==> [3/4] Building qtclrbridge.dll (native C bridge, MinGW)" -ForegroundColor Cyan
$nbBuild = Join-Path $root "native-bridge\build"
New-Item -ItemType Directory -Force -Path $nbBuild | Out-Null
cmake -S "$root\native-bridge" -B $nbBuild -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=$Config
cmake --build $nbBuild
Copy-Item (Join-Path $nbBuild "qtclrbridge.dll") $dist -Force

Write-Host "==> [4/4] Building the Qt demo (MinGW)" -ForegroundColor Cyan
$qtBuild = Join-Path $root "qt-example\build"
New-Item -ItemType Directory -Force -Path $qtBuild | Out-Null
$cmakeArgs = @("-S","$root\qt-example","-B",$qtBuild,"-G","MinGW Makefiles","-DCMAKE_BUILD_TYPE=$Config")
if ($QtPrefix -ne "") { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtPrefix" }
cmake @cmakeArgs
cmake --build $qtBuild
Copy-Item (Join-Path $qtBuild "qt_clr_demo.exe") $dist -Force

Write-Host ""
Write-Host "Done. Artifacts in: $dist" -ForegroundColor Green
Write-Host "Run the demo (from a shell where Qt's bin is on PATH for the Qt DLLs):" -ForegroundColor Green
Write-Host "    $dist\qt_clr_demo.exe"
