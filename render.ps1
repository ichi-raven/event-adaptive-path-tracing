[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$InputScene = (Join-Path $PSScriptRoot "scenes/cornell_box/scene.json"),

    [Parameter(Position = 1)]
    [string]$OutputDirectory,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RendererArguments
)

$ErrorActionPreference = "Stop"

$RootDirectory = $PSScriptRoot
$BinaryDirectory = Join-Path $RootDirectory "build/bin"
$MainExecutable = Join-Path $BinaryDirectory "eapt.exe"

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $SceneDirectory = Split-Path -Parent $InputScene
    $SceneName = Split-Path -Leaf $SceneDirectory
    $SceneName = [System.IO.Path]::GetFileNameWithoutExtension($SceneName)
    $OutputDirectory = Join-Path $RootDirectory "outputs/$SceneName"
}

if (-not [System.IO.Path]::IsPathRooted($InputScene)) {
    $InputScene = Join-Path $RootDirectory $InputScene
}
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $RootDirectory $OutputDirectory
}

$InputScene = [System.IO.Path]::GetFullPath($InputScene)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

if (-not (Test-Path -LiteralPath $MainExecutable -PathType Leaf)) {
    throw "Renderer executable not found: $MainExecutable`nBuild it first with: cmake --build build"
}
if (-not (Test-Path -LiteralPath $InputScene -PathType Leaf)) {
    throw "Input scene not found: $InputScene"
}

$ExitCode = 0
Push-Location $BinaryDirectory
try {
    & $MainExecutable `
        -i $InputScene `
        -o $OutputDirectory `
        --verbose `
        @RendererArguments
    $ExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

exit $ExitCode
