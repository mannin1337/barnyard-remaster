[CmdletBinding()]
param(
    [string]$LegacySdkRoot = $env:BARNYARD_LEGACY_SDK,
    [string]$UpstreamDirectory,
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
if (-not $LegacySdkRoot) { throw 'Use -LegacySdkRoot to specify the directory containing your Bink and FMOD SDKs.' }
$sdk = (Resolve-Path -LiteralPath $LegacySdkRoot).Path
foreach ($item in 'bink/include/bink.h','bink/include/radbase.h','bink/lib/x86/binkw32.lib','fmod/include/fmod/fmod.h','fmod/lib/x86/fmodvc.lib') {
    if (-not (Test-Path -LiteralPath (Join-Path $sdk $item) -PathType Leaf)) { throw "Required SDK file not found: $item" }
}
$upstream = if ($UpstreamDirectory) { (Resolve-Path -LiteralPath $UpstreamDirectory).Path }
$previousSdk = $env:BARNYARD_LEGACY_SDK
$process = [Diagnostics.Process]::GetCurrentProcess()
$oldPriority = $process.PriorityClass
Push-Location -LiteralPath $PSScriptRoot
try {
    $process.PriorityClass = 'BelowNormal'
    $env:BARNYARD_LEGACY_SDK = $sdk.Replace('\','/')
    $python = Get-Command py -ErrorAction SilentlyContinue
    $argsPython = @()
    if ($python) { $argsPython += '-3' } else { $python = Get-Command python }
    $argsPython += @('-B','BuildTools/bootstrap.py')
    if ($upstream) { $argsPython += @('--upstream-directory',$upstream) }
    & $python.Source @argsPython
    if ($LASTEXITCODE -ne 0) { throw 'Could not prepare the upstream build dependencies.' }
    & './Vendor/premake/premake5.exe' vs2022 --arch=x86 --renderer=DX11
    if ($LASTEXITCODE -ne 0) { throw 'Premake could not generate the Visual Studio projects.' }
    if (-not $ConfigureOnly) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $msbuild = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\Current\Bin\MSBuild.exe' | Select-Object -First 1
        if (-not $msbuild) { throw 'Install the Visual Studio 2022 C++ Build Tools and the Windows SDK.' }
        & $msbuild OpenBarnyard.sln '-t:Engine\OpenBarnyard' '-p:Configuration=Release' '-p:Platform=Win32' '-p:CL_MPCount=2' '-m:1' '-low:true' '-nologo' '-verbosity:minimal'
        if ($LASTEXITCODE -ne 0) { throw 'The Win32 Direct3D 11 release build failed.' }
    }
} finally {
    $env:BARNYARD_LEGACY_SDK = $previousSdk
    $process.PriorityClass = $oldPriority
    Pop-Location
}
