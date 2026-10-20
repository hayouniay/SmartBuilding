<#
SmartBuilding task runner for Windows 11 (MinGW GCC + Qt 6.8, no Visual Studio needed).

  .\scripts\sb.ps1 check                    verify compilers, CMake, Qt, Qt MQTT and the broker
  .\scripts\sb.ps1 install                  install missing tools (winget, aqtinstall, Qt MQTT from source)
  .\scripts\sb.ps1 build                    configure and build the apps and unit tests
  .\scripts\sb.ps1 build-tests              build only the Qt-free unit tests
  .\scripts\sb.ps1 test                     run the unit tests with ctest
  .\scripts\sb.ps1 launch <app>             client | server | simulator | all
  .\scripts\sb.ps1 build-test-launch <app>  build, run unit tests, then launch the app
  .\scripts\sb.ps1 clean
  .\scripts\sb.ps1 broker-start | broker-stop | broker-status

Optional overrides: SB_QT_PREFIX (default C:\Qt\6.8.0\mingw_64), SB_MINGW_BIN (default
C:\Qt\Tools\mingw1310_64\bin), SB_BROKER (localhost), SB_PORT (1883), SB_DB (smartbuilding.db).
#>
param(
    [Parameter(Position = 0)]
    [string]$Command = 'help',

    [Parameter(Position = 1)]
    [string]$App = 'client'
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$QtPrefix = if ($env:SB_QT_PREFIX) { $env:SB_QT_PREFIX } else { 'C:\Qt\6.8.0\mingw_64' }
$MingwBin = if ($env:SB_MINGW_BIN) { $env:SB_MINGW_BIN } else { 'C:\Qt\Tools\mingw1310_64\bin' }
$Broker   = if ($env:SB_BROKER)    { $env:SB_BROKER }    else { 'localhost' }
$Port     = if ($env:SB_PORT)      { [int]$env:SB_PORT } else { 1883 }
$Db       = if ($env:SB_DB)        { $env:SB_DB }        else { 'smartbuilding.db' }
$BuildDir = 'build-mingw'
$DepsDir = 'deps'
$DepsInstall = Join-Path (Get-Location).Path 'deps\install'
$TestsDir = 'build-tests-mingw'
$script:Failed = $false

# ---------- helpers ----------

function Write-Ok($Message)   { Write-Host "[ OK ] $Message" -ForegroundColor Green }
function Write-Warn($Message) { Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Miss($Message) { Write-Host "[MISS] $Message" -ForegroundColor Red; $script:Failed = $true }

# Puts the MinGW compiler, Qt runtime and CMake on PATH for this session.
function Use-Toolchain {
    $dirs = @($MingwBin, (Join-Path $QtPrefix 'bin'),
              (Join-Path $env:ProgramFiles 'mosquitto'),
              (Join-Path $env:ProgramFiles 'CMake\bin')) | Where-Object { Test-Path $_ }
    $env:Path = (@($dirs) + @($env:Path)) -join ';'
}

# Re-reads PATH from the registry, so tools installed by winget are visible without a new window.
function Update-SessionPath {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path', 'User')
}

# Runs a native command, streams its output, and throws if it fails.
# Native stderr would otherwise become an error under $ErrorActionPreference = 'Stop'.
function Invoke-Native([scriptblock]$Cmd, [string]$What) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Cmd 2>&1 | ForEach-Object { Write-Host $_ }
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $prev
    }
    if ($code -ne 0) { throw "$What failed (exit code $code)." }
}

# Returns the text output of a command, or an empty string if it can't run.
function Get-Output([scriptblock]$Cmd) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        (& $Cmd 2>&1 | Out-String).Trim()
    }
    catch {
        ''
    }
    finally {
        $ErrorActionPreference = $prev
    }
}

function Test-AtLeast([string]$Actual, [string]$Required) {
    try { return ([version]$Actual) -ge ([version]$Required) } catch { return $false }
}

# Finds a working Python 3. The Microsoft Store 'python' alias is skipped because it prints
# a message instead of a version; the 'py' launcher and the default install folder are tried too.
function Resolve-Python {
    $candidates = @(
        @{ Exe = 'py';     Pre = @('-3') },
        @{ Exe = 'python'; Pre = @() },
        @{ Exe = (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'); Pre = @() }
    )
    foreach ($c in $candidates) {
        $exists = (Get-Command $c.Exe -ErrorAction SilentlyContinue) -or (Test-Path $c.Exe)
        if (-not $exists) { continue }
        $out = Get-Output { & $c.Exe @($c.Pre) '--version' }
        if ($out -match '^Python 3') { return $c }
    }
    return $null
}

function Invoke-Python([string[]]$PyArgs, [string]$What) {
    $py = $script:Python
    if (-not $py) { throw 'Python not found. Run: install' }
    Invoke-Native { & $py.Exe @($py.Pre) @PyArgs } $What
}

# winget exit codes: 0 = installed now; -1978335189 = already installed, no newer version.
function Install-WingetPackage([string]$Id, [string]$Name) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        winget install -e --id $Id --accept-package-agreements --accept-source-agreements | Out-Host
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $prev
    }
    if ($code -eq 0 -or $code -eq -1978335189) { Write-Ok "$Name is installed" }
    else { throw "$Name install failed (winget exit code $code)." }
}

# Qt MQTT is built into deps\install (preferred) or may be installed inside the Qt folder.
function Test-QtMqtt {
    return (Test-Path (Join-Path $DepsInstall 'lib\cmake\Qt6Mqtt\Qt6MqttConfig.cmake')) -or
           (Test-Path (Join-Path $QtPrefix 'lib\cmake\Qt6Mqtt\Qt6MqttConfig.cmake'))
}

# Reads KEY=VALUE pairs from deps\qtmqtt.env (comment lines are ignored).
function Read-QtMqttPin {
    $pin = @{}
    foreach ($line in Get-Content (Join-Path $DepsDir 'qtmqtt.env')) {
        if ($line -match '^\s*([A-Z_]+)=(.+?)\s*$') { $pin[$Matches[1]] = $Matches[2] }
    }
    if (-not $pin['QTMQTT_REPO'] -or -not $pin['QTMQTT_TAG']) {
        throw 'deps\qtmqtt.env must define QTMQTT_REPO and QTMQTT_TAG.'
    }
    return $pin
}

function Assert-Qt {
    $qtConfig = Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake'
    if (-not (Test-Path $qtConfig) -or -not (Test-QtMqtt)) {
        throw "Qt 6.5+ with the MQTT module was not found at $QtPrefix. Run: .\scripts\sb.ps1 install"
    }
}

function Assert-Binary([string]$Name) {
    if (-not (Test-Path "$BuildDir\$Name.exe")) { throw "$Name.exe is not built. Run: build" }
}

# ---------- commands ----------

function Invoke-Check {
    $script:Failed = $false
    Write-Host '== SmartBuilding environment check (Windows) =='
    Use-Toolchain

    if (Get-Command g++ -ErrorAction SilentlyContinue) {
        $v = Get-Output { g++ -dumpfullversion }
        if (Test-AtLeast $v '13.0') { Write-Ok "g++ $v (MinGW)" }
        else { Write-Miss "g++ $v is too old (need 13+). Run: install" }
    }
    else { Write-Miss 'g++ not found. Run: install' }

    $cmakeText = Get-Output { cmake --version }
    if ($cmakeText) {
        $v = [regex]::Match($cmakeText, '\d+\.\d+(\.\d+)?').Value
        if (Test-AtLeast $v '3.21') { Write-Ok "cmake $v" }
        else { Write-Miss "cmake $v is too old (need 3.21+)" }
    }
    else { Write-Miss 'cmake not found. Run: install' }

    if (Get-Command ninja -ErrorAction SilentlyContinue) { Write-Ok 'ninja' }
    else { Write-Miss 'ninja not found. Run: install' }

    if (Get-Command git -ErrorAction SilentlyContinue) { Write-Ok 'git' }
    else { Write-Miss 'git not found (needed by install). Run: install' }

    $script:Python = Resolve-Python
    if ($script:Python) { Write-Ok "python 3 ($($script:Python.Exe) $($script:Python.Pre -join ' '))" }
    else { Write-Warn 'python 3 not found (needed only by install)' }

    $qtConfig = Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake'
    if (Test-Path $qtConfig) {
        $qmake = Join-Path $QtPrefix 'bin\qmake.exe'
        $v = if (Test-Path $qmake) { Get-Output { & $qmake -query QT_VERSION } } else { '' }
        if (Test-AtLeast $v '6.5') { Write-Ok "Qt $v at $QtPrefix" }
        else { Write-Miss "Qt version '$v' is too old or unknown (need 6.5+)" }
        if (Test-QtMqtt) { Write-Ok 'Qt MQTT module (deps\install)' }
        else { Write-Miss 'Qt MQTT module not found. Run: install' }
    }
    else { Write-Miss "Qt not found at $QtPrefix. Run: install" }

    $mosquittoExe = Join-Path $env:ProgramFiles 'mosquitto\mosquitto.exe'
    if ((Test-Path $mosquittoExe) -or (Get-Command mosquitto -ErrorAction SilentlyContinue)) { Write-Ok 'mosquitto broker' }
    else { Write-Warn 'mosquitto not installed (needed to run the apps). Run: install' }

    if (Test-NetConnection -ComputerName $Broker -Port $Port -InformationLevel Quiet -WarningAction SilentlyContinue) {
        Write-Ok "broker listening on ${Broker}:$Port"
    }
    else {
        Write-Warn "no broker on ${Broker}:$Port (run: broker-start)"
    }

    if ($script:Failed) {
        Write-Host "`nSome required items are missing. Run: .\scripts\sb.ps1 install" -ForegroundColor Red
        exit 1
    }
    Write-Host "`nEnvironment OK." -ForegroundColor Green
}

function Invoke-Install {
    Write-Host '== Installing dependencies (Windows) =='
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "winget not found. Install 'App Installer' from the Microsoft Store, then run install again."
    }

    $script:Python = Resolve-Python
    if (-not $script:Python) {
        Install-WingetPackage 'Python.Python.3.12' 'Python 3.12'
        Update-SessionPath
        Use-Toolchain
        $script:Python = Resolve-Python
    }
    if (-not $script:Python) {
        throw 'Python 3.12 is installed but cannot be found. Open a new PowerShell window and run install again.'
    }
    Write-Ok "Python found: $($script:Python.Exe) $($script:Python.Pre -join ' ')"

    if (-not (Get-Command git -ErrorAction SilentlyContinue))   { Install-WingetPackage 'Git.Git' 'Git' }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Install-WingetPackage 'Kitware.CMake' 'CMake' }
    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { Install-WingetPackage 'Ninja-build.Ninja' 'Ninja' }
    Update-SessionPath
    Use-Toolchain

    # QtPrefix is <root>\6.8.0\mingw_64, so the install root is two levels up.
    $qtRoot = Split-Path -Parent (Split-Path -Parent $QtPrefix)
    if (-not (Test-Path (Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake'))) {
        Write-Host 'Installing Qt 6.8 MinGW (several GB, this takes a while)...'
        Invoke-Python @('-m', 'pip', 'install', '--user', 'aqtinstall') 'aqtinstall install'
        Invoke-Python @('-m', 'aqt', 'install-qt', 'windows', 'desktop', '6.8.0', 'win64_mingw', '-O', $qtRoot) 'Qt install'
    }
    if (-not (Test-Path (Join-Path $MingwBin 'g++.exe'))) {
        Invoke-Python @('-m', 'aqt', 'install-tool', 'windows', 'desktop', 'tools_mingw1310', '-O', $qtRoot) 'MinGW tools install'
    }
    Use-Toolchain

    if (-not (Test-QtMqtt)) {
        Build-QtMqtt
    }

    Write-Host "`nInstall finished. Run: .\scripts\sb.ps1 check" -ForegroundColor Green
}

# Qt MQTT is built from source into deps\install, at the tag pinned in deps\qtmqtt.env.
function Build-QtMqtt {
    Use-Toolchain
    $pin   = Read-QtMqttPin
    $repo  = $pin['QTMQTT_REPO']
    $tag   = $pin['QTMQTT_TAG']
    $src   = Join-Path $DepsDir 'qtmqtt'
    $build = Join-Path $DepsDir 'build\qtmqtt'

    $qtVer = Get-Output { & (Join-Path $QtPrefix 'bin\qmake.exe') -query QT_VERSION }
    if ($qtVer -and $tag -ne "v$qtVer") {
        Write-Warn "qtmqtt $tag is pinned but Qt is $qtVer; adjust deps\qtmqtt.env if the build fails."
    }
    Write-Host "Building Qt MQTT $tag into $DepsInstall..."

    if (-not (Test-Path (Join-Path $src '.git'))) {
        New-Item -ItemType Directory -Force -Path $DepsDir | Out-Null
        Invoke-Native { git clone $repo $src } 'git clone qtmqtt'
    }
    Invoke-Native { git -C $src fetch --tags } 'git fetch qtmqtt'
    Invoke-Native { git -C $src checkout $tag } "git checkout $tag"

    Remove-Item -Recurse -Force $build -ErrorAction SilentlyContinue
    Invoke-Native {
        cmake -S $src -B $build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ `
              "-DCMAKE_PREFIX_PATH=$QtPrefix" "-DCMAKE_INSTALL_PREFIX=$DepsInstall" `
              -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
    } 'Qt MQTT configure'
    Invoke-Native { cmake --build $build } 'Qt MQTT build'
    Invoke-Native { cmake --install $build } 'Qt MQTT install'
    Sync-QtMqttDll
}

# windeployqt only looks for Qt modules inside Qt's own bin folder, so the Qt MQTT DLL
# built into deps\install\bin is copied there. This adds one file and leaves Qt's other files alone.
function Sync-QtMqttDll {
    $dlls = Get-ChildItem -Path (Join-Path $DepsInstall 'bin') -Filter 'Qt6Mqtt*.dll' -ErrorAction SilentlyContinue
    if (-not $dlls) { return }
    $qtBin = Join-Path $QtPrefix 'bin'
    foreach ($dll in $dlls) { Copy-Item $dll.FullName -Destination $qtBin -Force }
    Write-Ok "Qt MQTT DLL copied to $qtBin (needed by windeployqt)"
}

function Invoke-Build {
    Use-Toolchain
    Assert-Qt
    Sync-QtMqttDll
    Invoke-Native {
        cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc `
              -DCMAKE_CXX_COMPILER=g++ "-DCMAKE_PREFIX_PATH=$QtPrefix;$DepsInstall" -DSB_BUILD_APPS=ON -DSB_BUILD_TESTS=ON
    } 'CMake configure'
    Invoke-Native { cmake --build $BuildDir } 'Build'
}

function Invoke-BuildTests {
    Use-Toolchain
    Invoke-Native {
        cmake -S . -B $TestsDir -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc `
              -DCMAKE_CXX_COMPILER=g++ -DSB_BUILD_APPS=OFF
    } 'CMake configure (tests)'
    Invoke-Native { cmake --build $TestsDir } 'Build (tests)'
}

function Invoke-Test {
    Use-Toolchain
    if (Test-Path (Join-Path $BuildDir 'CTestTestfile.cmake')) { $dir = $BuildDir }
    elseif (Test-Path (Join-Path $TestsDir 'CTestTestfile.cmake')) { $dir = $TestsDir }
    else { throw 'Nothing built yet. Run: build (or build-tests).' }
    Invoke-Native { ctest --test-dir $dir --output-on-failure } 'Unit tests'
}

function Invoke-Launch([string]$Target) {
    Use-Toolchain
    $common = @('--broker', $Broker, '--port', "$Port")
    switch ($Target) {
        'client'    { Assert-Binary 'sb_client';    & "$BuildDir\sb_client.exe" @common }
        'simulator' { Assert-Binary 'sb_simulator'; & "$BuildDir\sb_simulator.exe" @common }
        'server'    { Assert-Binary 'sb_server';    & "$BuildDir\sb_server.exe" @common --database $Db }
        'all'       { Invoke-LaunchAll }
        default     { throw "Unknown app '$Target'. Use client, server, simulator or all." }
    }
}

# Server and simulator open in their own console windows; the client runs in this one.
function Invoke-LaunchAll {
    Assert-Binary 'sb_server'
    Assert-Binary 'sb_simulator'
    Assert-Binary 'sb_client'
    if (-not (Test-NetConnection -ComputerName $Broker -Port $Port -InformationLevel Quiet -WarningAction SilentlyContinue)) {
        Write-Warn "No broker on ${Broker}:$Port. The client will keep retrying until one starts."
    }

    $server = $null
    $simulator = $null
    try {
        $server = Start-Process -FilePath "$BuildDir\sb_server.exe" `
            -ArgumentList "--broker $Broker --port $Port --database $Db" -PassThru
        Start-Sleep -Seconds 1
        $simulator = Start-Process -FilePath "$BuildDir\sb_simulator.exe" `
            -ArgumentList "--broker $Broker --port $Port" -PassThru
        Start-Sleep -Seconds 1
        & "$BuildDir\sb_client.exe" --broker $Broker --port $Port
    }
    finally {
        foreach ($p in @($simulator, $server)) {
            if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
        }
        Write-Host 'Background services stopped.'
    }
}

function Invoke-Clean {
    foreach ($dir in @($BuildDir, $TestsDir)) {
        if (Test-Path $dir) {
            Remove-Item -Recurse -Force $dir
            Write-Host "Removed $dir"
        }
    }
}

function Invoke-Broker([string]$Action) {
    $svc = Get-Service -Name mosquitto -ErrorAction SilentlyContinue
    switch ($Action) {
        'start' {
            if ($svc) { Start-Service -Name mosquitto; Write-Ok 'mosquitto service started' }
            else { Write-Warn 'No mosquitto service. Start the broker manually: mosquitto -c "C:\Program Files\mosquitto\mosquitto.conf" -v' }
        }
        'stop' {
            if ($svc) { Stop-Service -Name mosquitto; Write-Ok 'mosquitto service stopped' }
            else { Write-Warn 'No mosquitto service found.' }
        }
        'status' {
            if ($svc) { $svc | Format-Table Name, Status | Out-String | Write-Host }
            else { Write-Warn 'No mosquitto service found.' }
            if (Test-NetConnection -ComputerName $Broker -Port $Port -InformationLevel Quiet -WarningAction SilentlyContinue) {
                Write-Ok "listening on ${Broker}:$Port"
            }
            else { Write-Warn "nothing listening on ${Broker}:$Port" }
        }
    }
}

function Show-Usage {
    @'
Usage: .\scripts\sb.ps1 <command> [app]

  check                    verify the environment
  install                  install missing dependencies
  build                    build apps and unit tests
  build-tests              build the unit tests only (no Qt)
  test                     run the unit tests
  launch <app>             client | server | simulator | all
  build-test-launch <app>  build, test, then launch
  clean                    remove build folders
  broker-start | broker-stop | broker-status
'@ | Write-Host
}

# ---------- dispatch ----------

try {
    switch ($Command) {
        'check'             { Invoke-Check }
        'install'           { Invoke-Install }
        'build'             { Invoke-Build }
        'build-tests'       { Invoke-BuildTests }
        'test'              { Invoke-Test }
        'launch'            { Invoke-Launch $App }
        'build-test-launch' { Invoke-Build; Invoke-Test; Invoke-Launch $App }
        'clean'             { Invoke-Clean }
        'broker-start'      { Invoke-Broker 'start' }
        'broker-stop'       { Invoke-Broker 'stop' }
        'broker-status'     { Invoke-Broker 'status' }
        default             { Show-Usage }
    }
}
catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
