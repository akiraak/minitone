# APO の DLL（x64）をビルドする。Windows 側の PowerShell で動かす。
# 要るもの: Visual Studio の C++ ビルドツール、Windows SDK。
# 出力: apo\build\equalizer_apo.dll。ビルドのあと、テスト（apo\test\dsp_test.cpp、host_test.cpp）も走らせる。
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe が見つからない。Visual Studio の C++ ビルドツールを入れること。' }

$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'C++ ビルドツールの入った Visual Studio が見つからない。' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$flags = '/nologo /std:c++17 /O2 /MT /W4 /WX /EHs-c- /GR- /utf-8 /DUNICODE /D_UNICODE /DNDEBUG /LD'
$sources = 'src\EqApo.cpp src\Dll.cpp src\Dsp.cpp src\SettingsStore.cpp'
$link = '/link /DEF:src\equalizer_apo.def /MANIFEST:NO /NODEFAULTLIB:atls.lib /OUT:build\equalizer_apo.dll /IMPLIB:build\equalizer_apo.lib ' +
        'audiobaseprocessingobject.lib audiomediatypecrt.lib audioeng.lib legacy_stdio_definitions.lib ole32.lib advapi32.lib uuid.lib'

# DLL を音のエンジン無しで読み込んで確かめるテスト。ビルドのたびに走らせる。
$testFlags = '/nologo /std:c++17 /O2 /MT /W4 /WX /EHs-c- /GR- /utf-8 /DUNICODE /D_UNICODE'
$testLink = '/link /NODEFAULTLIB:atls.lib audiomediatypecrt.lib audioeng.lib legacy_stdio_definitions.lib ole32.lib uuid.lib advapi32.lib'

# cmd は UNC パス（WSL のフォルダ）をカレントにできないので、pushd で一時的にドライブを割り当てる。
# 引用符の扱いを避けるため、一時のバッチファイルに書いて実行する。
$bat = Join-Path $env:TEMP 'equalizer_apo_build.bat'
$lines = @(
    '@echo off',
    "call `"$vcvars`" >nul || exit /b 1",
    "pushd `"$root`" || exit /b 1",
    'if not exist build mkdir build',
    "cl $flags /Fobuild\ $sources $link || (popd & exit /b 1)",
    'if not exist build\dsp mkdir build\dsp',
    "cl $testFlags /Fobuild\dsp\ /Febuild\dsp_test.exe test\dsp_test.cpp src\Dsp.cpp || (popd & exit /b 1)",
    'build\dsp_test.exe || (popd & exit /b 1)',
    "cl $testFlags /Fobuild\ /Febuild\host_test.exe test\host_test.cpp $testLink || (popd & exit /b 1)",
    'build\host_test.exe build\equalizer_apo.dll',
    'set RESULT=%ERRORLEVEL%',
    'popd',
    'exit /b %RESULT%'
)
Set-Content -Path $bat -Value $lines -Encoding ASCII
cmd.exe /d /c $bat
$exit = $LASTEXITCODE
Remove-Item $bat -ErrorAction SilentlyContinue
if ($exit -ne 0) { throw "ビルドかテストに失敗した（exit=$exit）。" }

Get-Item (Join-Path $root 'build\equalizer_apo.dll') | ForEach-Object { '{0} ({1} bytes)' -f $_.Name, $_.Length }

# 管理者の PowerShell から WSL のフォルダが見えないことがあるので、
# DLL と登録・解除のスクリプトを Windows 側のフォルダにもそろえて置く。
$stage = Join-Path $env:LOCALAPPDATA 'akiraak-equalizer-dev'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item (Join-Path $root 'build\equalizer_apo.dll') $stage -Force
Copy-Item (Join-Path $root 'scripts\*.ps1') $stage -Force
"登録用の一式: $stage"

