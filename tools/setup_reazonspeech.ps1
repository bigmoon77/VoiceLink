$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$runtimeRoot = Join-Path $projectRoot "third_party\sherpa-onnx"
$runtimeName = "sherpa-onnx-v2.0.1-win-x64-shared-MD-Release-no-tts"
$runtimeDirectory = Join-Path $runtimeRoot $runtimeName
$runtimeArchive = Join-Path $runtimeRoot "$runtimeName.tar.bz2"
$runtimeDll = Join-Path $runtimeDirectory "lib\sherpa-onnx-c-api.dll"

$modelDirectory = Join-Path $projectRoot "models\reazonspeech-k2-v2"

function Invoke-Download {
    param(
        [string]$Url,
        [string]$Destination,
        [long]$ExpectedSize
    )

    $directory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $directory | Out-Null

    if (Test-Path $Destination) {
        $existingSize = (Get-Item $Destination).Length
        if ($existingSize -eq $ExpectedSize) {
            return
        }
        Remove-Item -Force $Destination
    }

    Write-Host "Downloading $Destination"
    & curl.exe -L --fail --retry 3 $Url -o $Destination
    if ($LASTEXITCODE -ne 0) {
        throw "Download failed: $Url"
    }

    $actualSize = (Get-Item $Destination).Length
    if ($actualSize -ne $ExpectedSize) {
        throw "Unexpected file size: $Destination ($actualSize / $ExpectedSize)"
    }
}

if (!(Test-Path $runtimeDll)) {
    New-Item -ItemType Directory -Force -Path $runtimeRoot | Out-Null

    if (!(Test-Path $runtimeArchive)) {
        Write-Host "Downloading sherpa-onnx Windows x64 runtime"
        $runtimeUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.1/$runtimeName.tar.bz2"
        & curl.exe -L --fail --retry 3 $runtimeUrl -o $runtimeArchive
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to download sherpa-onnx runtime."
        }
    }

    Write-Host "Extracting sherpa-onnx runtime"
    & tar.exe -xjf $runtimeArchive -C $runtimeRoot
    if ($LASTEXITCODE -ne 0 -or !(Test-Path $runtimeDll)) {
        throw "Failed to extract sherpa-onnx runtime."
    }
}

$files = @(
    @{
        Name = "encoder.int8.onnx"
        Source = "encoder-epoch-99-avg-1.int8.onnx"
        Size = 154670139
    },
    @{
        Name = "decoder.int8.onnx"
        Source = "decoder-epoch-99-avg-1.int8.onnx"
        Size = 2959337
    },
    @{
        Name = "joiner.int8.onnx"
        Source = "joiner-epoch-99-avg-1.int8.onnx"
        Size = 2696970
    },
    @{
        Name = "tokens.txt"
        Source = "tokens.txt"
        Size = 45754
    }
)

foreach ($file in $files) {
    $url = "https://huggingface.co/reazon-research/reazonspeech-k2-v2/resolve/main/$($file.Source)"
    $destination = Join-Path $modelDirectory $file.Name
    Invoke-Download -Url $url -Destination $destination -ExpectedSize $file.Size
}

Write-Host ""
Write-Host "ReazonSpeech k2-v2 setup completed."
Write-Host "Runtime: $runtimeDirectory"
Write-Host "Model:   $modelDirectory"
Write-Host "The model is INT8 and runs on CPU."
