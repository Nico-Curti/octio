param(
  [string]$BuildDir = "",
  [string]$PrefixPath = "",
  [string]$Config = "Release",
  [switch]$NoTests,
  [string]$VcpkgRoot = ""
)

$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
  $BuildDir = Join-Path $RootDir "build/local"
}
if ([string]::IsNullOrWhiteSpace($PrefixPath)) {
  $PrefixPath = Join-Path $BuildDir "install"
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot) -and $env:VCPKG_ROOT) {
  $VcpkgRoot = $env:VCPKG_ROOT
}

$CMakeArgs = @(
  "-S", $RootDir,
  "-B", $BuildDir,
  "-DCMAKE_BUILD_TYPE=$Config",
  "-DCMAKE_INSTALL_PREFIX=$PrefixPath",
  "-DOCTIO_BUILD_TESTS=ON",
  "-DOCTIO_BUILD_TOOLS=ON"
)

if (-not [string]::IsNullOrWhiteSpace($VcpkgRoot)) {
  $Toolchain = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"
  $CMakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$Toolchain"
  $CMakeArgs += "-DVCPKG_MANIFEST_MODE=ON"
}

& cmake @CMakeArgs
& cmake --build $BuildDir --config $Config --parallel

if (-not $NoTests) {
  & ctest --test-dir $BuildDir -C $Config --output-on-failure
}

& cmake --install $BuildDir --config $Config --prefix $PrefixPath
Write-Host "octio installed locally to: $PrefixPath"
