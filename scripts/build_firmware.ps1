param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("motor", "sensor", "adc_test", "all")]
    [string]$Target,
    [switch]$Flash
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Toolchain = Join-Path $RepoRoot "03.software\02.source_code\cmake\arm-none-eabi-toolchain.cmake"

$ArmBin = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin"
$CMakeBin = "C:\Program Files\CMake\bin"
$NinjaDir = Split-Path -Parent (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source

$env:PATH = "$ArmBin;$CMakeBin;$NinjaDir;$env:PATH"

function Build-Firmware {
    param([string]$SourceDir)

    $BuildDir = Join-Path $SourceDir "build"
    $NinjaFile = Join-Path $BuildDir "build.ninja"

    if (-not (Test-Path $NinjaFile)) {
        Write-Host "Configuring $SourceDir ..."
        cmake --fresh -S $SourceDir -B $BuildDir -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$Toolchain"
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed: $SourceDir"
        }
    }

    Write-Host "Building $SourceDir ..."
    cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $SourceDir"
    }
}

function Flash-Firmware {
    param(
        [string]$SourceDir,
        [string]$HexName = "SKD26502_SMART_GREENHOUSE.hex"
    )

    $Hex = Join-Path $SourceDir "build\$HexName"
    if (-not (Test-Path $Hex)) {
        throw "HEX not found: $Hex"
    }

    Write-Host "Flashing $Hex ..."
    pyocd load -t py32f071xb --erase=sector $Hex
    if ($LASTEXITCODE -ne 0) {
        throw "pyocd load failed: $Hex"
    }

    pyocd reset -t py32f071xb
    if ($LASTEXITCODE -ne 0) {
        throw "pyocd reset failed"
    }

    Write-Host "Flash OK: $Hex"
}

$MotorDir = Join-Path $RepoRoot "03.software\02.source_code\04.controller_unit_motor_freertos"
$SensorDir = Join-Path $RepoRoot "03.software\02.source_code\04.controller_unit_sensor_freertos"
$AdcTestDir = Join-Path $RepoRoot "03.software\02.source_code\05.adc_motor_test"

if ($Target -eq "motor" -or $Target -eq "all") {
    Build-Firmware -SourceDir $MotorDir
}
if ($Target -eq "sensor" -or $Target -eq "all") {
    Build-Firmware -SourceDir $SensorDir
}
if ($Target -eq "adc_test" -or $Target -eq "all") {
    Build-Firmware -SourceDir $AdcTestDir
}

Write-Host "Build OK: $Target"

if ($Flash) {
    if ($Target -eq "motor" -or $Target -eq "all") {
        Flash-Firmware -SourceDir $MotorDir
    }
    if ($Target -eq "sensor" -or $Target -eq "all") {
        Flash-Firmware -SourceDir $SensorDir
    }
    if ($Target -eq "adc_test" -or $Target -eq "all") {
        Flash-Firmware -SourceDir $AdcTestDir -HexName "ADC_MOTOR_TEST.hex"
    }
}
