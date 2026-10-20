<#
.SYNOPSIS
    Build, test and run SmartBuilding on Windows 11 (Visual Studio 2022, MSVC).

.EXAMPLES
    .\scripts\windows.ps1 configure            # needs $env:QT_DIR
    .\scripts\windows.ps1 build -Config Release
    .\scripts\windows.ps1 test
    .\scripts\windows.ps1 run-all              # server + simulator + client
    .\scripts\windows.ps1 configure -NoQt      # unit tests only, no Qt needed
    .\scripts\windows.ps1 clean
#>
param(
    [Parameter(Position = 0)]
    [ValidateSet('configure', 'build', 'test', 'run', 'run-all', 'clean')]
    [string]$Action = 'build',

    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [string]$Broker = 'localhost',
    [int]$Port = 1883,

    [switch]$NoQt
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$buildDir = if ($NoQt) { 'build\win-tests' } else { 'build\win' }

function Assert-Qt {
    # Check QT_DIR before calling Join-Path: an empty value makes Join-Path throw a confusing error.
    if (-not $env:QT_DIR) {
        throw 'QT_DIR is not set in this terminal. Run: $env:QT_DIR = "C:\Qt\6.8.0\msvc2022_64" (or setx QT_DIR ... and open a new terminal).'
    }
    $cfg = Join-Path $env:QT_DIR 'lib\cmake\Qt6\Qt6Config.cmake'
    if (-not (Test-Path $cfg)) {
        throw "Qt6Config.cmake not found under QT_DIR ($cfg). Check that QT_DIR points at your Qt MSVC install, e.g. C:\Qt\6.8.0\msvc2022_64."
    }
    $mqtt = Join-Path $env:QT_DIR 'lib\cmake\Qt6Mqtt\Qt6MqttConfig.cmake'
    if (-not (Test-Path $mqtt)) {
        throw "Qt MQTT module not found (expected $mqtt). Install it with the Qt Maintenance Tool or build it from source (see README.md)."
    }
}

function Invoke-Native([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE" }
}

switch ($Action) {
    'configure' {
        if (-not $NoQt) { Assert-Qt }
        $cmakeArgs = @('-S', '.', '-B', $buildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64')
        if ($NoQt) {
            $cmakeArgs += '-DSB_BUILD_APPS=OFF'
        } else {
            $cmakeArgs += "-DCMAKE_PREFIX_PATH=$env:QT_DIR"
        }
        Invoke-Native { cmake @cmakeArgs }
    }

    'build' {
        if (-not (Test-Path $buildDir)) {
            & $PSCommandPath configure -NoQt:$NoQt
        }
        Invoke-Native { cmake --build $buildDir --config $Config --parallel }
    }

    'test' {
        Invoke-Native { ctest --test-dir $buildDir -C $Config --output-on-failure }
    }

    'run' {
        $exe = Join-Path $buildDir "$Config\sb_client.exe"
        Invoke-Native { & $exe --broker $Broker --port $Port }
    }

    'run-all' {
        if (-not (Test-NetConnection -ComputerName $Broker -Port $Port -InformationLevel Quiet)) {
            throw "No MQTT broker listening on ${Broker}:$Port. Start Mosquitto first (see README.md, 'Windows')."
        }
        $server = $null
        $simulator = $null
        try {
            # Each background program opens its own console window so its log stays visible.
            $server = Start-Process -FilePath (Join-Path $buildDir "$Config\sb_server.exe") `
                -ArgumentList "--broker $Broker --port $Port --database smartbuilding.db" -PassThru
            Start-Sleep -Seconds 1
            $simulator = Start-Process -FilePath (Join-Path $buildDir "$Config\sb_simulator.exe") `
                -ArgumentList "--broker $Broker --port $Port" -PassThru
            Start-Sleep -Seconds 1
            & (Join-Path $buildDir "$Config\sb_client.exe") --broker $Broker --port $Port
        }
        finally {
            foreach ($p in @($simulator, $server)) {
                if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
            }
            Write-Host 'Background services stopped.'
        }
    }

    'clean' {
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue build
        Write-Host 'Removed build\'
    }
}
