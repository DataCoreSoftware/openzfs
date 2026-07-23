# *****************************************************************************
# Copyright (c) 2026 DataCore Software Corporation. All rights reserved.
# *****************************************************************************

<#
	.SYNOPSIS
		Runs CodeQL static analysis over the ZFSin kernel driver target only
		(not the full CMake build) and reports the Must-Fix findings required
		for the HLK "Static Tools Logo Test" (WHCP certification).

	.DESCRIPTION
		1. Builds a CodeQL database by tracing a CMake+Ninja build of ONLY the
		   `ZFSin` target (the driver .sys and everything statically linked
		   into it: splkern, zlibkern, icpkern, luakern, zfskern, zfskern_os,
		   zcommonkern, nvpairkern, unicodekern, zstdkern). This deliberately
		   excludes the user-mode tools (zfs.exe, zpool.exe, zfsinstaller.exe,
		   etc.) and user-mode libraries (libzfs, libnvpair, libzpool, ...) -
		   several of those share source files with the driver (e.g.
		   module/zfs/*.c is also compiled into libzpool) but under different
		   macros/headers, and mixing both into one database produces
		   confusing multi-context findings for the same source line.
		2. Analyzes the database with the WHCP `mustfix.qls` suite, writing
		   SARIF to the repo root.
		3. Prints a summary grouped by rule/API.

	.PREREQUISITES
		- CodeQL CLI 2.20.1 unpacked to -CodeQLHome (WHCP matrix version).
		- Query packs downloaded: microsoft/windows-drivers@1.8.0 and
		  microsoft/cpp-queries@0.0.4.
		- VS2019 (vcvars64.bat) + WDK 10.0.19041 + OpenSSL-Win64 + the
		  prebuilt ISA-L static libs under lib\ISA-L\<Debug|Release> - all
		  already required by this repo's normal CMake build.

	.EXAMPLE
		.\Invoke-CodeQLZFSinAnalysis.ps1
#>

PARAM
(
	[Parameter(HelpMessage = "Directory containing the CodeQL CLI (codeql.exe)")]
	[string]$CodeQLHome = "C:\codeql-home\codeql",

	[Parameter(HelpMessage = "WHCP query suite to run")]
	[ValidateSet("mustfix", "recommended", "mustrun")]
	[string]$Suite = "mustfix",

	[Parameter(HelpMessage = "CMake build configuration")]
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",

	[Parameter(HelpMessage = "Path to vcvars64.bat (VS2019)")]
	[string]$VcVars64 = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat",

	[Parameter(HelpMessage = "Directory for the CodeQL database")]
	[string]$DatabasePath,

	[Parameter(HelpMessage = "Output SARIF path")]
	[string]$SarifPath,

	[Parameter(HelpMessage = "Reuse an existing CodeQL database instead of rebuilding it")]
	[switch]$ReuseDatabase
)

$ErrorActionPreference = "Stop"

# Repo root is two levels up from contrib\windows\codeql.
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path

if (!$DatabasePath) { $DatabasePath = Join-Path $repoRoot "out\CodeQL\databases\ZFSin" }
if (!$SarifPath)     { $SarifPath     = Join-Path $repoRoot "ZFSin.codeql.sarif" }

$buildDir = Join-Path $repoRoot "out\build\codeql-zfsin"

function Invoke-Tool([string]$Exe, [string[]]$ToolArgs)
{
	$prevEap = $script:ErrorActionPreference
	$script:ErrorActionPreference = "Continue"
	try
	{
		& $Exe @ToolArgs 2>&1 | ForEach-Object { Write-Host $_.ToString() }
		return $LASTEXITCODE
	}
	finally
	{
		$script:ErrorActionPreference = $prevEap
	}
}

$codeqlExe = Join-Path $CodeQLHome "codeql.exe"
if (!(Test-Path $codeqlExe))
{
	$nested = Join-Path $CodeQLHome "codeql\codeql.exe"
	if (Test-Path $nested) { $codeqlExe = $nested }
}
if (!(Test-Path $codeqlExe))
{
	throw "codeql.exe not found under '$CodeQLHome'."
}

if (!(Test-Path $VcVars64))
{
	throw "vcvars64.bat not found at '$VcVars64'. Pass -VcVars64 explicitly."
}

$suiteSpec = "microsoft/windows-drivers:windows-driver-suites\$Suite.qls"

Write-Host "Repo root      : $repoRoot"
Write-Host "CodeQL         : $codeqlExe"
Write-Host "Query suite    : $suiteSpec"
Write-Host "Build dir      : $buildDir"
Write-Host "Database       : $DatabasePath"
Write-Host "SARIF output   : $SarifPath"
Write-Host ""

# Fail early if the WHCP query packs are missing.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$qlpacks = (& $codeqlExe resolve packs 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
$ErrorActionPreference = $prevEap
if ($qlpacks -notmatch "microsoft/windows-drivers")
{
	throw "Query pack microsoft/windows-drivers not found. Run: codeql pack download microsoft/windows-drivers@1.8.0 (and microsoft/cpp-queries@0.0.4)."
}

New-Item -ItemType Directory -Force (Split-Path $DatabasePath -Parent) > $null

if ($ReuseDatabase -and (Test-Path (Join-Path $DatabasePath "codeql-database.yml")))
{
	Write-Host "Reusing existing database $DatabasePath"
}
else
{
	# Build script: vcvars64 -> configure (only if needed) -> build ONLY the
	# ZFSin target (not the default `all` target, which would also build the
	# unrelated user-mode tools/libraries).
	$buildScript = Join-Path $env:TEMP "build-zfsin-codeql.cmd"
	@"
@echo off
setlocal
call "$VcVars64"
if errorlevel 1 exit /b 1
set "CMAKE=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
cd /d "$repoRoot"
if not exist "$buildDir\CMakeCache.txt" (
	"%CMAKE%" -S . -B "$buildDir" -G Ninja -DCMAKE_BUILD_TYPE=$Configuration -DCMAKE_MAKE_PROGRAM="%NINJA%"
	if errorlevel 1 exit /b 1
)
"%CMAKE%" --build "$buildDir" --target ZFSin --clean-first
if errorlevel 1 exit /b 1
exit /b 0
"@ | Out-File -FilePath $buildScript -Encoding ASCII

	if (Test-Path $DatabasePath) { Remove-Item -Recurse -Force $DatabasePath }
	$code = Invoke-Tool $codeqlExe @("database", "create", $DatabasePath, "--language=cpp",
			"--source-root=$repoRoot", "--command=$buildScript", "--overwrite")
	if ($code -ne 0) { throw "codeql database create failed (exit $code)" }
}

$code = Invoke-Tool $codeqlExe @("database", "analyze", $DatabasePath, $suiteSpec,
		"--format=sarifv2.1.0", "--output=$SarifPath", "--rerun")
if ($code -ne 0) { throw "codeql database analyze failed (exit $code)" }

$sarif = Get-Content $SarifPath -Raw | ConvertFrom-Json
$findings = @()
foreach ($run in $sarif.runs) { if ($run.results) { $findings += $run.results } }

Write-Host ""
Write-Host "================ ZFSin ($Suite) ================" -ForegroundColor Cyan
if ($findings.Count -eq 0)
{
	Write-Host "clean - 0 findings" -ForegroundColor Green
}
else
{
	Write-Host "$($findings.Count) finding(s):" -ForegroundColor Yellow
	$findings | Group-Object ruleId | Sort-Object Count -Descending | ForEach-Object {
		Write-Host ("  {0,4} x {1}" -f $_.Count, $_.Name) -ForegroundColor Yellow
	}
	if ($Suite -eq "mustfix")
	{
		Write-Host ""
		Write-Host "FAILS certification until fixed" -ForegroundColor Red
	}
}

exit $findings.Count
