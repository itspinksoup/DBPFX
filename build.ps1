# DBPFX - Windows build: native engine (MSVC), then the .NET solution.
#   .\build.ps1                  Release build + all tests
#   .\build.ps1 -Config Debug
#   .\build.ps1 -SkipTests
#   .\build.ps1 -Clean           start the native build from scratch
#
# Visual Studio is located with vswhere and its x64 compiler environment is loaded, so this works
# from any PowerShell window (no "Developer PowerShell" needed). Visual Studio's own CMake and Ninja
# are preferred over whatever is on PATH, so an older standalone CMake cannot get in the way.
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")] [string] $Config = "Release",
    [switch] $SkipTests,
    [switch] $Clean
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$nativeBuild = Join-Path $root "build\native"
$artifacts = Join-Path $root "artifacts\native"

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    $found = & $vswhere -latest -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($found) { return @($found)[0] }
    return $null
}

function Import-VsEnvironment([string] $vsPath) {
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $vsPath" }
    $lines = & cmd.exe /c "call `"$vcvars`" >nul 2>&1 && set"
    if ($LASTEXITCODE) { throw "vcvars64.bat failed" }
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
        }
    }
}

function Resolve-Tool([string] $bundled, [string] $name) {
    if ($bundled -and (Test-Path $bundled)) { return $bundled }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# ------------------------------------------------------------------ toolchain
$vs = Find-VisualStudio
if (-not $vs) {
    throw @"
No Visual Studio with the C++ compiler was found.
Install "Desktop development with C++" in the Visual Studio Installer (or the free
"Build Tools for Visual Studio" with that workload), then run this script again.
"@
}
Write-Host "Visual Studio: $vs" -ForegroundColor DarkGray
Import-VsEnvironment $vs

$vsCMakeDir = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake"
$cmake = Resolve-Tool (Join-Path $vsCMakeDir "CMake\bin\cmake.exe") "cmake"
$ctest = Resolve-Tool (Join-Path $vsCMakeDir "CMake\bin\ctest.exe") "ctest"
$ninja = Resolve-Tool (Join-Path $vsCMakeDir "Ninja\ninja.exe") "ninja"
if (-not $cmake) { throw "CMake not found. Add the 'C++ CMake tools for Windows' component in the Visual Studio Installer." }

if ($ninja) {
    $generator = "Ninja"
    $generatorArgs = @("-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=$ninja")
} else {
    $generator = "NMake Makefiles"
    $generatorArgs = @("-G", "NMake Makefiles")
}
Write-Host "CMake: $cmake ($generator)" -ForegroundColor DarkGray

# a cache made with another generator or a platform setting (e.g. by an earlier failed run) cannot be reused
$cache = Join-Path $nativeBuild "CMakeCache.txt"
$stale = (Test-Path $cache) -and (
    -not (Select-String -Path $cache -SimpleMatch "CMAKE_GENERATOR:INTERNAL=$generator" -Quiet) -or
    (Select-String -Path $cache -Pattern '^CMAKE_GENERATOR_PLATFORM:INTERNAL=.+' -Quiet))
if (($Clean -or $stale) -and (Test-Path $nativeBuild)) {
    Write-Host "Removing the previous native build folder" -ForegroundColor DarkGray
    Remove-Item -Recurse -Force $nativeBuild
}

# ------------------------------------------------------------------ version / release channel
# The pre-release tag picks the icon of every executable: canary, dev, beta or none (stable).
$cmakeLists = Get-Content (Join-Path $root "native\CMakeLists.txt") -Raw
$props = Get-Content (Join-Path $root "managed\Directory.Build.props") -Raw
if ($cmakeLists -notmatch 'project\(DBPFX VERSION ([0-9.]+)') { throw "No version in native\CMakeLists.txt" }
$nativeVersion = $Matches[1]
if ($cmakeLists -notmatch 'set\(DBPFX_VERSION_SUFFIX "([^"]*)"\)') { throw "No DBPFX_VERSION_SUFFIX in native\CMakeLists.txt" }
if ($Matches[1]) { $nativeVersion += "-" + $Matches[1] }
if ($props -notmatch '<Version>([^<]+)</Version>') { throw "No <Version> in managed\Directory.Build.props" }
$managedVersion = $Matches[1]
if ($nativeVersion -ne $managedVersion) {
    throw "Version mismatch: native $nativeVersion, .NET $managedVersion. Keep DBPFX_VERSION_SUFFIX and <Version> in sync."
}
$channel = switch -Regex ($nativeVersion) {
    '-canary' { "CANARY"; break }
    '-dev'    { "DEVELOPER"; break }
    '-beta'   { "BETA"; break }
    '-'       { throw "Unknown pre-release tag in $nativeVersion (use -canary, -dev, -beta or none)" }
    default   { "STABLE" }
}
Write-Host "DBPFX $nativeVersion ($channel channel)" -ForegroundColor DarkGray

# ------------------------------------------------------------------ native engine
Write-Host "== native engine ($Config)" -ForegroundColor Cyan
& $cmake -S (Join-Path $root "native") -B $nativeBuild @generatorArgs "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE) { throw "CMake configure failed" }
& $cmake --build $nativeBuild --parallel
if ($LASTEXITCODE) { throw "native build failed" }
if (-not $SkipTests) {
    & $ctest --test-dir $nativeBuild --output-on-failure
    if ($LASTEXITCODE) { throw "native tests failed" }
}
New-Item -ItemType Directory -Force $artifacts | Out-Null
Copy-Item (Join-Path $nativeBuild "DBPFX.Native.dll") $artifacts -Force
Copy-Item (Join-Path $nativeBuild "dbpfx-cli.exe") $artifacts -Force -ErrorAction SilentlyContinue
# channel icons and the stamper, used by the .NET build for its executables
Copy-Item (Join-Path $nativeBuild "DBPFX.Identity.dll") $artifacts -Force
Copy-Item (Join-Path $nativeBuild "dbpfx-iconstamp.exe") $artifacts -Force

# ------------------------------------------------------------------ .NET
Write-Host "== .NET (net10.0 + netstandard2.1)" -ForegroundColor Cyan
# vcvars64.bat sets Platform=x64, which MSBuild would treat as the solution platform ("Release|x64");
# the .NET projects are "Any CPU", so drop it for the managed build.
Remove-Item Env:Platform -ErrorAction SilentlyContinue
$dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
if (-not $dotnet) { throw "dotnet not found. Install the .NET 10 SDK." }
$sdks = & dotnet --list-sdks
if (-not ($sdks | Where-Object { $_ -match '^(1[0-9]|[2-9][0-9])\.' })) {
    throw "The .NET 10 SDK (or newer) is required. Installed SDKs:`n$($sdks -join "`n")"
}
& dotnet build (Join-Path $root "managed\DBPFX.slnx") -c $Config "-p:Platform=Any CPU"
if ($LASTEXITCODE) { throw "dotnet build failed" }
if (-not $SkipTests) {
    & dotnet test (Join-Path $root "managed\tests\DBPFX.Tests") -c $Config --no-build
    if ($LASTEXITCODE) { throw "managed tests failed" }
}
Write-Host "done. Native engine: $artifacts\DBPFX.Native.dll" -ForegroundColor Green
