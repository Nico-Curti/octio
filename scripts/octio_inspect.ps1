param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$File,
  [switch]$Json
)

$ErrorActionPreference = "Stop"
$ArgsList = @("inspect", $File)
if ($Json) {
  $ArgsList += "--json"
}

if (Get-Command octio -ErrorAction SilentlyContinue) {
  & octio @ArgsList
} else {
  & python -m octio.cli @ArgsList
}
exit $LASTEXITCODE
