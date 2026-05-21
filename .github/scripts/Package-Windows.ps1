[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-Windows-${Target}-Portable"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/build_${Target}/Output/${ProductName}-*-Windows-*-Portable.zip"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."

    $InstallDir = "${ProjectRoot}/release/${Configuration}/${ProductName}"
    $OutputDir  = "${ProjectRoot}/build_${Target}/Output"
    $StagingDir = "${OutputDir}/_staging"

    # Ensure output folder exists
    if (-not (Test-Path $OutputDir)) { New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null }

    # Clean previous staging/zip artifacts
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue -Path $StagingDir
    Remove-Item -Force -ErrorAction SilentlyContinue -Path "${OutputDir}/${OutputName}.zip"

    # Create OBS Program Files portable structure:
    #   obs-plugins/64bit/  <- DLL + PDB
    #   data/obs-plugins/<name>/  <- locale and other data
    $PluginDest = "${StagingDir}/obs-plugins/64bit"
    $DataDest   = "${StagingDir}/data/obs-plugins/${ProductName}"

    New-Item -ItemType Directory -Force -Path $PluginDest | Out-Null
    New-Item -ItemType Directory -Force -Path $DataDest   | Out-Null

    # Copy DLL and PDB
    Copy-Item -Path "${InstallDir}/bin/64bit/*" -Destination $PluginDest -Recurse -Force

    # Copy data folder (locale, icons, etc.)
    if (Test-Path "${InstallDir}/data") {
        Copy-Item -Path "${InstallDir}/data/*" -Destination $DataDest -Recurse -Force
    }

    # Compress CONTENTS of staging (wildcard = no wrapping folder in zip root)
    $CompressArgs = @{
        Path             = "${StagingDir}/*"
        CompressionLevel = 'Optimal'
        DestinationPath  = "${OutputDir}/${OutputName}.zip"
        Verbose          = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs

    # Clean up staging folder
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue -Path $StagingDir

    Log-Group
}

Package
