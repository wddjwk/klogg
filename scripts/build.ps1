param(
    [ValidateSet('prepare', 'build', 'release', 'clean', 'clangd')]
    [string]$Action = 'build',

    [string]$MsvcSetup = 'D:\env\MSVC\msvc\setup_x64.bat',
    [string]$QtRoot = 'D:\env\Qt\6.7.3\msvc2019_64',
    [string]$BuildType = 'Release'
)

$ErrorActionPreference = 'Stop'

# ============================================================================
# Paths
# ============================================================================

$ProjectRoot = (Get-Item $PSScriptRoot).Parent.FullName
$BuildRoot = Join-Path $ProjectRoot 'build'
$DepsDir = Join-Path $BuildRoot '_deps'
$KloggVersion = '24.11.0'
$Arch = 'x64'
$QtMajor = 'Qt6'

# ============================================================================
# Dependency versions and URLs
# ============================================================================

$BoostVersion = '1.86.0'
$BoostUrl = 'https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.bz2'
$BoostDir = "boost_$($BoostVersion -replace '\.', '_')"

$RagelVersion = '6.10'
$RagelUrl = 'https://github.com/PolarGoose/Ragel-for-Windows/releases/download/ragel-6.10/Ragel.zip'

$OpenSslUrl = 'https://www.firedaemon.com/download-firedaemon-openssl-1.1.1-zip'


# ============================================================================
# Helpers
# ============================================================================

function Write-Step {
    param([string]$Message)
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

function Write-Ok {
    param([string]$Message)
    Write-Host "  OK: $Message" -ForegroundColor Green
}

# ############################################################################
# >>>  MSYS2/MINGW POLLUTION CONTROL - BEGIN  <<<
# >>>  All MSYS2-related filtering is in this section.                     <<<
# >>>  To disable: set $script:Msys2FilterEnabled = $false below.          <<<
# ############################################################################

$script:Msys2FilterEnabled = $true
$script:Msys2Keywords = @('msys', 'mingw', 'cygwin')
$script:Msys2KnownPaths = @(
    'D:\env\msys2\mingw64\include',
    'D:\env\msys2\mingw64\lib',
    'D:\env\msys2\usr\include',
    'D:\env\msys2\usr\lib'
)

function Clear-Msys2Pollution {
    if (-not $script:Msys2FilterEnabled) { return }

    # 1. Clear all GCC-style env vars before MSVC setup
    foreach ($var in @('INCLUDE', 'LIB', 'LIBPATH',
                       'CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH',
                       'LIBRARY_PATH', 'CMAKE_INCLUDE_PATH', 'CMAKE_LIBRARY_PATH')) {
        [System.Environment]::SetEnvironmentVariable($var, $null, 'Process')
    }
}

function Filter-Msys2FromEnv {
    if (-not $script:Msys2FilterEnabled) { return }

    # 2. Filter MSYS2 entries from INCLUDE, LIB, LIBPATH after MSVC setup
    foreach ($varName in @('INCLUDE', 'LIB', 'LIBPATH')) {
        $val = [System.Environment]::GetEnvironmentVariable($varName, 'Process')
        if ($val) {
            $filtered = ($val -split ';' | Where-Object {
                $entry = $_
                $entry -and -not ($script:Msys2Keywords | Where-Object { $entry -like "*$_*" })
            }) -join ';'
            [System.Environment]::SetEnvironmentVariable($varName, $filtered, 'Process')
        }
    }
}

function Get-Msys2PathFilter {
    # Returns a scriptblock that filters MSYS2 entries from a list of paths
    if (-not $script:Msys2FilterEnabled) { return { $true } }
    return {
        param($entry)
        -not ($script:Msys2Keywords | Where-Object { $entry -like "*$_*" })
    }
}

function Get-CMakeMsys2Flags {
    # Returns cmake flags to block MSYS2 include paths at the compiler level
    if (-not $script:Msys2FilterEnabled) { return @() }

    $flags = @(
        # Ultimate fix: override implicit include dirs so compiler never searches MSYS2 headers
        '-DCMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES='
        '-DCMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES='
        # Also block via CMAKE_IGNORE_PATH for find_package etc.
        '-DCMAKE_NO_SYSTEM_FROM_IMPORTED=ON'
    )

    # Collect known paths that exist
    $existingPaths = $script:Msys2KnownPaths |
        Where-Object { Test-Path $_ } |
        ForEach-Object { $_ -replace '\\', '/' }

    # Also collect from env vars
    foreach ($var in @('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'INCLUDE')) {
        $val = [System.Environment]::GetEnvironmentVariable($var, 'Process')
        if ($val) {
            foreach ($p in ($val -split ';')) {
                if ($p -and ($script:Msys2Keywords | Where-Object { $p -like "*$_*" })) {
                    $existingPaths += ($p -replace '\\', '/')
                }
            }
        }
    }

    $existingPaths = $existingPaths | Select-Object -Unique
    if ($existingPaths) {
        $flags += "-DCMAKE_IGNORE_PATH=$($existingPaths -join ';')"
        Write-Host "  MSYS2 paths excluded from CMake:" -ForegroundColor Yellow
        foreach ($p in $existingPaths) { Write-Host "    $p" -ForegroundColor DarkYellow }
    }

    return $flags
}

# ############################################################################
# >>>  MSYS2/MINGW POLLUTION CONTROL - END  <<<
# ############################################################################

function Setup-Msvc {
    if ($env:KLOGG_MSVC_LOADED -eq '1') { return }

    Write-Step "Loading MSVC environment"
    if (-not (Test-Path $MsvcSetup)) {
        throw "MSVC setup script not found: $MsvcSetup"
    }

    # Clear MSYS2 pollution before loading MSVC
    Clear-Msys2Pollution

    $originalPath = $env:PATH
    $envOutput = cmd.exe /c "`"$MsvcSetup`" && set"
    foreach ($line in $envOutput) {
        if ($line -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($matches[1].Trim(), $matches[2].Trim(), 'Process')
        }
    }

    # Filter MSYS2 from INCLUDE/LIB/LIBPATH
    Filter-Msys2FromEnv

    # Merge PATH: keep MSVC + original (minus MSYS2 entries)
    $pathFilter = Get-Msys2PathFilter
    $msvcPath = $env:PATH
    $originalEntries = ($originalPath -split ';') | Where-Object { $_ -and (& $pathFilter $_) }
    $msvcEntries = ($msvcPath -split ';') | Where-Object { $_ }
    $merged = ($msvcEntries + ($originalEntries | Where-Object { $msvcEntries -notcontains $_ })) -join ';'
    $env:PATH = $merged

    $cl = Get-Command cl -ErrorAction SilentlyContinue
    if (-not $cl) { throw "MSVC load failed: cl.exe not found" }
    Write-Ok "cl.exe: $($cl.Source)"

    $scoopShims = Join-Path $env:USERPROFILE 'scoop\shims'
    if ((Test-Path $scoopShims) -and ($env:PATH -notlike "*$scoopShims*")) {
        $env:PATH = "$scoopShims;$env:PATH"
    }

    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmake) { Write-Ok "cmake: $($cmake.Source)" } else { Write-Warning "cmake not in PATH" }
    $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if ($ninja) { Write-Ok "ninja: $($ninja.Source)" } else { Write-Warning "ninja not in PATH" }

    $env:KLOGG_MSVC_LOADED = '1'
}

function Download-File {
    param([string]$Url, [string]$OutputPath, [string]$Description)

    if (Test-Path $OutputPath) { Write-Ok "$Description (cached)"; return }

    $dir = Split-Path $OutputPath -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }

    Write-Host "  Downloading $Description..."
    $ProgressPreference = 'SilentlyContinue'
    try { Invoke-WebRequest -Uri $Url -OutFile $OutputPath -UseBasicParsing }
    catch { & curl.exe -L -A 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)' -o $OutputPath $Url }
    $ProgressPreference = 'Continue'

    $size = (Get-Item $OutputPath).Length
    if ($size -lt 10KB) {
        $head = Get-Content $OutputPath -TotalCount 3 -ErrorAction SilentlyContinue
        if ($head -match '<!DOCTYPE|<html|Not Found') {
            Remove-Item $OutputPath -Force
            throw "Download failed (HTML error page): $Description"
        }
    }
    Write-Ok "$Description ($([math]::Round($size/1MB, 1)) MB)"
}

# ============================================================================
# Prepare
# ============================================================================

function Invoke-Prepare {
    Write-Step "Preparing build dependencies"
    Write-Host "Project: $ProjectRoot"
    Write-Host "Build:   $BuildRoot"
    Write-Host "Deps:    $DepsDir"

    if (-not (Test-Path $QtRoot)) { throw "Qt not found: $QtRoot" }
    Write-Ok "Qt: $QtRoot"
    Setup-Msvc

    if (-not (Test-Path $DepsDir)) { New-Item -ItemType Directory -Path $DepsDir -Force | Out-Null }

    # Boost
    $boostRoot = Join-Path $DepsDir $BoostDir
    if (Test-Path (Join-Path $boostRoot 'boost')) {
        Write-Ok "Boost $BoostVersion (cached)"
    } else {
        $archive = Join-Path $DepsDir 'boost.tar.bz2'
        Download-File -Url $BoostUrl -OutputPath $archive -Description "Boost $BoostVersion"
        Write-Host "  Extracting Boost..."
        & 'C:\Windows\System32\tar.exe' -xjf $archive -C $DepsDir 2>&1 | Out-Null
        $inner = Join-Path $DepsDir $BoostDir
        if (Test-Path (Join-Path $inner $BoostDir)) {
            Get-ChildItem (Join-Path $inner $BoostDir) | Move-Item -Destination $inner -Force
            Remove-Item (Join-Path $inner $BoostDir) -Recurse -Force -ErrorAction SilentlyContinue
        }
        Remove-Item $archive -Force -ErrorAction SilentlyContinue
        Write-Ok "Boost $BoostVersion"
    }
    $env:BOOST_ROOT = $boostRoot

    # Ragel
    $ragelExe = Join-Path $DepsDir 'ragel\ragel.exe'
    if (Test-Path $ragelExe) {
        Write-Ok "Ragel $RagelVersion (cached)"
    } else {
        $archive = Join-Path $DepsDir 'ragel.zip'
        Download-File -Url $RagelUrl -OutputPath $archive -Description "Ragel $RagelVersion"
        $ragelDir = Join-Path $DepsDir 'ragel'
        if (-not (Test-Path $ragelDir)) { New-Item -ItemType Directory -Path $ragelDir -Force | Out-Null }
        Expand-Archive -Path $archive -DestinationPath $ragelDir -Force
        if ((Test-Path (Join-Path $ragelDir 'Ragel.exe')) -and -not (Test-Path $ragelExe)) {
            Rename-Item (Join-Path $ragelDir 'Ragel.exe') 'ragel.exe'
        }
        Remove-Item $archive -Force -ErrorAction SilentlyContinue
        Write-Ok "Ragel $RagelVersion"
    }
    $env:PATH = "$(Split-Path $ragelExe);$env:PATH"

    # OpenSSL
    $sslBinDir = Join-Path $DepsDir 'openssl\x64\bin'
    if (Test-Path $sslBinDir) {
        Write-Ok "OpenSSL (cached)"
    } else {
        $archive = Join-Path $DepsDir 'openssl.zip'
        Download-File -Url $OpenSslUrl -OutputPath $archive -Description 'OpenSSL 1.1.1'
        $sslDir = Join-Path $DepsDir 'openssl'
        Expand-Archive -Path $archive -DestinationPath $sslDir -Force
        $nested = Join-Path $sslDir 'openssl-1.1'
        if (Test-Path $nested) {
            Get-ChildItem $nested | Move-Item -Destination $sslDir -Force
            Remove-Item $nested -Recurse -Force -ErrorAction SilentlyContinue
        }
        Remove-Item $archive -Force -ErrorAction SilentlyContinue
        Write-Ok "OpenSSL 1.1.1"
    }
    $env:SSL_DIR = $sslBinDir
    $env:SSL_ARCH = '-x64'

    # NSIS FileAssociation.nsh (copy from project's own packaging)
    $nsisIncludeDir = $null
    foreach ($candidate in @(
        (Join-Path $env:USERPROFILE 'scoop\apps\nsis\current\Include'),
        'C:\Program Files (x86)\NSIS\Include',
        'C:\Program Files\NSIS\Include',
        'D:\env\tools\nsis\Include'
    )) {
        if (Test-Path $candidate) { $nsisIncludeDir = $candidate; break }
    }
    if ($nsisIncludeDir) {
        $target = Join-Path $nsisIncludeDir 'FileAssociation.nsh'
        $source = Join-Path $ProjectRoot 'packaging\windows\FileAssociation.nsh'
        if (-not (Test-Path $target) -and (Test-Path $source)) {
            Copy-Item $source $target -Force
            Write-Ok "FileAssociation.nsh (copied from packaging\windows)"
        } elseif (Test-Path $target) {
            Write-Ok "FileAssociation.nsh (cached)"
        } else {
            Write-Warning "FileAssociation.nsh source not found: $source"
        }
    } else {
        Write-Warning "NSIS Include directory not found, skipping FileAssociation.nsh"
    }

    Write-Step "Prepare complete"
    Write-Ok "BOOST_ROOT = $env:BOOST_ROOT"
    Write-Ok "SSL_DIR    = $env:SSL_DIR"
}

# ============================================================================
# Build
# ============================================================================

function Invoke-Build {
    Write-Step "Building klogg"
    Setup-Msvc

    if (-not (Test-Path $QtRoot)) { throw "Qt not found: $QtRoot" }
    $env:CMAKE_PREFIX_PATH = $QtRoot

    $env:BOOST_ROOT = Join-Path $DepsDir $BoostDir
    $ragelExe = Join-Path $DepsDir 'ragel\ragel.exe'
    if (Test-Path $ragelExe) { $env:PATH = "$(Split-Path $ragelExe);$env:PATH" }

    if (-not (Test-Path $BuildRoot)) { New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null }
    $cpmCache = Join-Path $BuildRoot '_cpm_cache'

    Write-Step "CMake configure"
    $msys2Flags = Get-CMakeMsys2Flags
    $cmakeArgs = @(
        '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$BuildType",
        '-DKLOGG_GENERIC_CPU=ON',
        '-DKLOGG_USE_HYPERSCAN=ON',
        '-DKLOGG_USE_SENTRY=OFF',
        '-DKLOGG_BUILD_TESTS=OFF',
        "-DCPM_SOURCE_CACHE=$cpmCache",
        '-Wno-dev'
    ) + $msys2Flags + @($ProjectRoot)

    Push-Location $BuildRoot
    try {
        & cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    } finally { Pop-Location }

    Write-Step "CMake build"
    & cmake --build $BuildRoot --config $BuildType
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    Write-Step "Build complete"
    Write-Ok "Binaries: $BuildRoot\output"
}

# ============================================================================
# Release
# ============================================================================

function Invoke-Release {
    Write-Step "Creating release packages"
    Setup-Msvc

    $releaseDir = Join-Path $BuildRoot 'release'
    $artifactsDir = Join-Path $BuildRoot 'artifacts'

    if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
    if (-not (Test-Path $artifactsDir)) { New-Item -ItemType Directory -Path $artifactsDir -Force | Out-Null }

    $outputDir = Join-Path $BuildRoot 'output'

    Write-Step "Copying binaries"
    foreach ($exe in @('klogg.exe', 'klogg_portable.exe', 'klogg_grep.exe')) {
        $src = Join-Path $outputDir $exe
        if (Test-Path $src) { Copy-Item $src $releaseDir -Force; Write-Ok $exe }
    }
    $tbbDll = Join-Path $outputDir 'tbb12.dll'
    if (Test-Path $tbbDll) { Copy-Item $tbbDll $releaseDir -Force; Write-Ok 'tbb12.dll' }

    Write-Step "Copying MSVC runtime"
    if ($env:VCToolsInstallDir) {
        $msvcBinDir = Join-Path $env:VCToolsInstallDir "bin\Hostx64\x64"
        if ($Arch -eq 'x86') { $msvcBinDir = Join-Path $env:VCToolsInstallDir "bin\Hostx86\x86" }
        foreach ($dll in @('msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','vcruntime140.dll','vcruntime140_1.dll')) {
            $src = Join-Path $msvcBinDir $dll
            if (Test-Path $src) { Copy-Item $src $releaseDir -Force; Write-Ok $dll }
        }
    } else { Write-Warning "VCToolsInstallDir not set, MSVC runtime DLLs skipped" }

    Write-Step "Copying OpenSSL"
    $sslBinDir = Join-Path $DepsDir 'openssl\x64\bin'
    foreach ($dll in @('libcrypto-1_1-x64.dll','libssl-1_1-x64.dll')) {
        $src = Join-Path $sslBinDir $dll
        if (Test-Path $src) { Copy-Item $src $releaseDir -Force; Write-Ok $dll }
    }

    Write-Step "Copying Qt"
    foreach ($dll in @("${QtMajor}Core.dll","${QtMajor}Gui.dll","${QtMajor}Network.dll",
                       "${QtMajor}Widgets.dll","${QtMajor}Concurrent.dll","${QtMajor}Xml.dll",
                       "${QtMajor}Core5Compat.dll")) {
        $src = Join-Path $QtRoot "bin\$dll"
        if (Test-Path $src) { Copy-Item $src $releaseDir -Force }
    }
    $platformsDir = Join-Path $releaseDir 'platforms'
    $stylesDir = Join-Path $releaseDir 'styles'
    New-Item -ItemType Directory -Path $platformsDir -Force | Out-Null
    New-Item -ItemType Directory -Path $stylesDir -Force | Out-Null
    Copy-Item (Join-Path $QtRoot 'plugins\platforms\qwindows.dll') $platformsDir -Force
    foreach ($s in @('qmodernwindowsstyle.dll','qwindowsvistastyle.dll')) {
        $src = Join-Path $QtRoot "plugins\styles\$s"
        if (Test-Path $src) { Copy-Item $src $stylesDir -Force }
    }
    Write-Ok "Qt runtime"

    foreach ($file in @('COPYING','NOTICE','README.md','DOCUMENTATION.md')) {
        $src = Join-Path $ProjectRoot $file
        if (Test-Path $src) { Copy-Item $src $releaseDir -Force }
    }
    $docHtml = Join-Path $BuildRoot 'generated\documentation.html'
    if (Test-Path $docHtml) { Copy-Item $docHtml $releaseDir -Force }

    Write-Step "Creating portable zip"
    $portableZip = Join-Path $artifactsDir "klogg-$KloggVersion-$Arch-$QtMajor-portable.zip"
    if (Test-Path $portableZip) { Remove-Item $portableZip -Force }
    $sevenZip = Get-Command '7z' -ErrorAction SilentlyContinue
    if ($sevenZip) { & $sevenZip.Source a -tzip $portableZip (Join-Path $releaseDir '*') | Out-Null }
    else { Compress-Archive -Path (Join-Path $releaseDir '*') -DestinationPath $portableZip }
    Write-Ok "Portable: $([math]::Round((Get-Item $portableZip).Length/1MB, 1)) MB"

    Write-Step "Creating NSIS installer"
    $makensis = Get-Command 'makensis' -ErrorAction SilentlyContinue
    if (-not $makensis) {
        foreach ($path in @('C:\Program Files (x86)\NSIS\makensis.exe','C:\Program Files\NSIS\makensis.exe',
                            'D:\env\tools\nsis\makensis.exe',(Join-Path $env:USERPROFILE 'scoop\apps\nsis\current\makensis.exe'))) {
            if (Test-Path $path) { $makensis = @{ Source = $path }; Write-Host "  Found NSIS: $path"; break }
        }
    }
    if ($makensis) {
        $nsisScript = Join-Path $ProjectRoot 'klogg_local.nsi'
        if (-not (Test-Path $nsisScript)) { $nsisScript = Join-Path $ProjectRoot 'packaging\windows\klogg.nsi' }
        $projectRelease = Join-Path $ProjectRoot 'release'
        if ((Test-Path $releaseDir) -and ($releaseDir -ne $projectRelease)) {
            if (Test-Path $projectRelease) { Remove-Item $projectRelease -Recurse -Force }
            Copy-Item $releaseDir $projectRelease -Recurse -Force
        }
        Push-Location $ProjectRoot
        try {
            & $makensis.Source -NOCD "-DVERSION=$KloggVersion" "-DPLATFORM=$Arch" "-DQT_MAJOR=$QtMajor" $nsisScript
            if ($LASTEXITCODE -ne 0) { throw "NSIS failed" }
        } finally {
            Pop-Location
            if (Test-Path $projectRelease) { Remove-Item $projectRelease -Recurse -Force -ErrorAction SilentlyContinue }
        }
        $installer = Join-Path $ProjectRoot "klogg-$KloggVersion-$Arch-$QtMajor-setup.exe"
        if (Test-Path $installer) {
            Move-Item $installer (Join-Path $artifactsDir (Split-Path $installer -Leaf)) -Force
            Write-Ok "Installer: $([math]::Round((Get-Item (Join-Path $artifactsDir (Split-Path $installer -Leaf))).Length/1MB, 1)) MB"
        }
    } else { Write-Warning "makensis not found, skipping installer" }

    Write-Step "Release artifacts"
    Get-ChildItem $artifactsDir | ForEach-Object { Write-Host "  $($_.Name) ($([math]::Round($_.Length/1MB, 1)) MB)" }
}

# ============================================================================
# Clean
# ============================================================================

function Invoke-Clean {
    Write-Step "Cleaning build artifacts (preserving downloads)"
    if (Test-Path $BuildRoot) {
        $keepDirs = @('_deps', '_cpm_cache')
        Get-ChildItem $BuildRoot | ForEach-Object {
            if ($keepDirs -notcontains $_.Name) {
                Remove-Item $_.FullName -Recurse -Force
                Write-Host "  Removed: $($_.Name)"
            } else {
                Write-Host "  Kept:    $($_.Name)"
            }
        }
    }
    foreach ($pattern in @('klogg-*-setup.exe')) {
        Get-ChildItem $ProjectRoot -Filter $pattern -ErrorAction SilentlyContinue | ForEach-Object {
            Remove-Item $_.FullName -Force
            Write-Host "  Removed: $($_.Name)"
        }
    }
    Write-Ok "Clean complete"
}

# ============================================================================
# Clangd
# ============================================================================

function Invoke-Clangd {
    Write-Step "Generating .clangd configuration"
    Setup-Msvc

    $clExe = (Get-Command cl -ErrorAction Stop).Source -replace '\\', '/'
    $includePaths = ($env:INCLUDE -split ';' | Where-Object { $_ }) -replace '\\', '/'
    $buildDir = ($BuildRoot -replace '\\', '/')

    $addLines = $includePaths | ForEach-Object { "    - -I$_" }
    $addLines += @('    - -Wno-c++98-compat','    - -Wno-c++98-compat-pedantic',
                   '    - -Wno-missing-prototypes','    - -Wno-switch-default',
                   '    - -D_HAS_CXX17=1','    - -D_HAS_CXX20=1','    - /std:c++latest')
    $addBlock = $addLines -join "`n"

    $clangdContent = @"
CompileFlags:
  CompilationDatabase: $buildDir
  Compiler: $clExe
  Add:
$addBlock
"@
    $clangdPath = Join-Path $ProjectRoot '.clangd'
    Set-Content -Path $clangdPath -Value $clangdContent -NoNewline
    Write-Ok "Generated: $clangdPath"
    Write-Host "`n$clangdContent"
}

# ============================================================================
# Main
# ============================================================================

switch ($Action) {
    'prepare' { Invoke-Prepare }
    'build'   { Invoke-Prepare; Invoke-Build }
    'release' { Invoke-Prepare; Invoke-Build; Invoke-Release }
    'clean'   { Invoke-Clean }
    'clangd'  { Invoke-Clangd }
}
